#include "editor/editor_ui.hpp"

#include "assets/cubemap.hpp"
#include "assets/assets.hpp"
#include "editor/animation_window.hpp"
#include "editor/asset_browser.hpp"
#include "editor/asset_preview_renderer.hpp"
#include "editor/create_menu.hpp"
#include "editor/editor_history.hpp"
#include "editor/editor_input.hpp"
#include "editor/fix_project.hpp"
#include "editor/level_objects.hpp"
#include "editor/lightmapping_window.hpp"
#include "editor/model_preview_window.hpp"
#include "editor/play_mode.hpp"
#include "editor/script_editor.hpp"
#include "editor/transform_editor.hpp"
#include "gameplay/raycast.hpp"
#include "levels/level_system.hpp"
#include "models/model_asset.hpp"
#include "platform/engine_log.hpp"
#include "prefabs/prefab.hpp"
#include "project/project_backup.hpp"
#include "project/project_document.hpp"
#include "project/project_io.hpp"
#include "scene/scene_camera.hpp"
#include "scripting/script_components.hpp"
#include "terrain/terrain_editor.hpp"
#include "ui/qt/qt_application.hpp"
#include "ui/qt/qt_editor_window.hpp"
#include "ui/qt/qt_panels.hpp"
#include "ui/qt/qt_viewport.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {

using ui::qt::EditorWindow;

// Opens one of the shipped HTML docs, resolving beside the executable so a
// release package works from any working directory.
void openManualPage(const char* page) {
    const char* base = SDL_GetBasePath();
    auto path = std::filesystem::path(base ? base : ".") / "docs" / page;
    if (!std::filesystem::is_regular_file(path)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "%s",
                     "Offline documentation missing. Build docs with python docs/build.py "
                     "--output build/docs.");
        return;
    }
    auto raw = std::filesystem::absolute(path).generic_string();
    std::string url = "file://";
    if (raw.empty() || raw.front() != '/') {
        url += '/';
    }
    for (unsigned char c : raw) {
        if (std::isalnum(c) || c == '/' || c == ':' || c == '-' || c == '_' || c == '.' ||
            c == '~') {
            url += static_cast<char>(c);
        } else {
            const char* hex = "0123456789ABCDEF";
            url += '%';
            url += hex[c >> 4];
            url += hex[c & 15];
        }
    }
    if (!SDL_OpenURL(url.c_str())) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", SDL_GetError());
    }
}

} // namespace

EditorUI::EditorUI(SDL_Window* window) {
    shell_ = ui::qt::Application::window();
    window_ = window;
}

bool EditorUI::ready() const {
    return shell_ != nullptr;
}

void EditorUI::showError(const std::string& title, const std::string& message) {
    if (shell_) {
        shell_->showError(QString::fromStdString(title), QString::fromStdString(message));
    }
}

void EditorUI::consoleMessage(const std::string& text) {
    if (shell_) {
        shell_->consoleMessage(QString::fromStdString(text));
    }
}

void EditorUI::beginFrame(EditorView& view, const EditorUIState& state) {
    if (!shell_) {
        return;
    }
    // One drain per frame, and it comes first: whatever the user did since the
    // last frame has to be visible to the pointer read below. Draining after
    // that read would either lose the input or apply it twice.
    ui::qt::Application::processEvents();

    actions_ = shell_->actions;
    shell_->actions.clear();

    view.sceneSurface = shell_->scenePixelSize();
    view.gameSurface = shell_->gamePixelSize();
    view.scenePointer = shell_->scenePointer();
    view.gamePointer = shell_->gamePointer();
    view.sceneFocused = shell_->scenePanelFocused();
    view.hierarchyFocused = shell_->hierarchyPanelFocused();
    if (view.sceneFocused && view.hierarchyFocused) {
        // Both cannot own the keyboard; the one the user clicked keeps it.
        view.hierarchyFocused = false;
    }
    shell_->setPlayState(state.playActive, state.playing, state.paused);
    shell_->setFrameInfo(state.fps, state.dt, state.frameMs);
    shell_->setEditable(state.editAllowed);
}

void EditorUI::presentFrame(const EditorView& view, const EditorUIState& state) {
    if (!shell_) {
        return;
    }
    shell_->setSceneSurfaceImage(view.sceneImage);
    shell_->setGameSurfaceImage(view.gameImage);
    if (state.profiler) {
        shell_->setProfilerData(*state.profiler);
    }
    shell_->setLevelName(QString::fromStdString(state.levelName));
    shell_->setProjectName(QString::fromStdString(state.projectName));
    shell_->setStatus(QString::fromStdString(state.status),
                      QString::fromStdString(state.detail));
    shell_->setFrameInfo(state.fps, state.dt, state.frameMs);
    shell_->setEditable(state.editAllowed);
    shell_->setPlayState(state.playActive, state.playing, state.paused);
    shell_->setSeparateGame(state.separateGame, state.gameWindowActive);
    shell_->setFpsOverlay(state.showFps);
    shell_->setCollidersVisible(state.showColliders);
    shell_->setProfilerVisible(state.showProfiler);
    shell_->setRecentProjects(state.recentProjects);
    shell_->setLevelMenu(state.levelsEnabled, state.levelNames, state.currentLevel);
    shell_->setSelectionFromEngine(state.selection);
    shell_->setTerrainToolsVisible(state.terrainSelected);
    shell_->setPrefabMode(state.prefabMode ? QString::fromStdString(state.prefabPath) : QString());
    if (!state.prefabMode) {
        shell_->clearPrefabMode();
    }
    shell_->refresh(state.frameMs);
    // Clear the one-shot press/release/wheel flags now that the engine has
    // acted on them. Events that arrive before the next beginFrame() will set
    // them again and be seen then.
    shell_->consumePointerEvents();
}

void EditorUI::handleActions(const ActionSink& sink) {
    if (!shell_) {
        return;
    }
    const auto& a = actions_;
    if (a.createObject) {
        sink.createObject(*a.createObject);
    }
    if (a.undo) {
        sink.undo();
    }
    if (a.redo) {
        sink.redo();
    }
    if (a.duplicate) {
        sink.duplicate();
    }
    if (a.copy) {
        sink.copy();
    }
    if (a.paste) {
        sink.paste();
    }
    if (a.parentToggle) {
        sink.parentToggle();
    }
    if (a.play) {
        sink.play();
    }
    if (a.pause) {
        sink.pause();
    }
    if (a.stop) {
        sink.stop();
    }
    if (a.separateGameWindow) {
        sink.separateGameWindow();
    }
    if (a.toggleGameFullscreen) {
        sink.toggleGameFullscreen();
    }
    if (a.saveProject) {
        sink.saveProject();
    }
    if (a.saveProjectAs) {
        sink.saveProjectAs();
    }
    if (a.openProject) {
        sink.openProject();
    }
    if (a.openRecent) {
        sink.openRecent(a.recentPath);
    }
    if (a.saveLevel) {
        sink.saveLevel();
    }
    if (a.newLevel) {
        sink.newLevel();
    }
    if (a.renameLevel) {
        sink.renameLevel();
    }
    if (a.openLevel) {
        sink.openLevel(a.levelName);
    }
    if (a.exit) {
        sink.exit();
    }
    if (a.resetLayout) {
        shell_->resetLayout();
    }
    if (a.theme >= 0) {
        sink.theme(a.theme);
    }
    if (a.backup) {
        sink.backup();
    }
    if (a.fixProject) {
        sink.fixProject();
    }
    if (a.openInVSCode) {
        sink.openInVSCode();
    }
    if (a.importFiles) {
        sink.importFiles();
    }
    if (a.openSettingsFolder) {
        sink.openSettingsFolder();
    }
    if (a.openManual == 1) {
        openManualPage("index.html");
    } else if (a.openManual == 2) {
        openManualPage("scripting-reference.html");
    }
    if (a.openGitHub) {
        SDL_OpenURL("https://github.com/monsef64/LegacyEngine");
    }
    if (a.about) {
        shell_->showAbout();
    }
    if (a.prefabSave) {
        sink.prefabSave();
    }
    if (a.prefabExit) {
        sink.prefabExit();
    }
    if (!a.spawnModel.empty()) {
        sink.spawnModel(a.spawnModel);
    }
    if (!a.spawnPrefab.empty()) {
        sink.spawnPrefab(a.spawnPrefab);
    }
    if (!a.editPrefab.empty()) {
        sink.editPrefab(a.editPrefab);
    }
    if (!a.editCubemap.empty()) {
        shell_->openCubemapEditor(QString::fromStdString(a.editCubemap));
        sink.editCubemap(a.editCubemap);
    }
    if (!a.editAnimation.empty()) {
        sink.editAnimation(a.editAnimation);
    }
    if (!a.previewModel.empty()) {
        shell_->openModelPreview(QString::fromStdString(a.previewModel));
        sink.previewModel(a.previewModel);
    }
    if (!a.openScript.empty()) {
        sink.openScript(a.openScript);
    }
    actions_.clear();
}
