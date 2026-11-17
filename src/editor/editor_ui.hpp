// Bridge between the engine loop and the Qt editor shell.
//
// The engine keeps owning the frame loop, the SDL window, input, audio and the
// GL context. The Qt shell owns the widgets. Each engine frame goes through
// three steps:
//
//   1. beginFrame()   takes the actions the user produced since the last
//                     frame and reports the viewport state to the shell.
//   2. the engine consumes those actions through ActionSink and mutates the
//                     world at a point it controls, so undo entries and
//                     physics resets stay consistent.
//   3. presentFrame() pushes the resulting state back into the widgets and
//                     pumps Qt's event queue.
//
// Nothing in the Qt layer touches the world directly.
#pragma once

#include "editor/editor_geometry.hpp"
#include "editor/editor_history.hpp"
#include "editor/editor_settings.hpp"
#include "editor/play_mode.hpp"
#include "editor/transform_edit.hpp"
#include "ui/qt/qt_editor_window.hpp"

#include <QImage>
#include <QSize>
#include <functional>
#include <string>
#include <vector>

struct SDL_Window;
class AssetBrowser;
class AnimationWindow;
class LightmappingWindow;
class ModelPreviewWindow;
class Renderer;
class ScriptEditor;
class World;
struct Entity;
class FrameProfiler;
class TerrainsEditorTag;

namespace ui {
namespace qt {
class EditorWindow;
}
} // namespace ui

// What the shell knows about the picture it is about to be handed.
struct EditorView {
    QSize sceneSurface{1, 1};
    QSize gameSurface{1, 1};
    QImage sceneImage;
    QImage gameImage;
    ui::qt::EditorWindow::PointerState scenePointer{};
    ui::qt::EditorWindow::PointerState gamePointer{};
    bool sceneFocused = false;
    bool hierarchyFocused = false;
};

struct EditorUIState {
    bool playActive = false, playing = false, paused = false;
    bool editAllowed = true;
    bool separateGame = false, gameWindowActive = false;
    bool showFps = false, showColliders = false, showProfiler = false;
    bool levelsEnabled = true;
    bool prefabMode = false;
    bool terrainSelected = false;
    Entity selection{};
    float fps = 60.0f, dt = 1.0f / 60.0f;
    double frameMs = 0.0;
    std::string levelName, projectName, status, detail, currentLevel, prefabPath;
    std::vector<std::string> levelNames, recentProjects;
    const FrameProfiler* profiler = nullptr;
};

class EditorUI {
  public:
    // Everything the engine must do when the user asks for it. The engine
    // supplies these once, at start-up.
    struct ActionSink {
        std::function<void(int)> createObject;
        std::function<void()> undo, redo, duplicate, copy, paste, parentToggle;
        std::function<void()> play, pause, stop, separateGameWindow, toggleGameFullscreen;
        std::function<void()> openProject, saveProject, saveProjectAs;
        std::function<void(const std::string&)> openRecent;
        std::function<void()> saveLevel, newLevel, renameLevel, deleteLevel;
        std::function<void(const std::string&)> openLevel;
        std::function<void()> exit;
        std::function<void(int)> theme;
        std::function<void()> backup, fixProject, openInVSCode, openSettingsFolder, importFiles;
        std::function<void()> openAnimation, saveAnimation, bakeLighting;
        std::function<void()> prefabSave, prefabExit;
        std::function<void(const std::string&)> spawnModel, spawnPrefab, editPrefab, editCubemap,
            editAnimation, previewModel, openScript;
    };

    explicit EditorUI(SDL_Window* window);
    bool ready() const;
    void showError(const std::string& title, const std::string& message);
    void consoleMessage(const std::string& text);

    void beginFrame(EditorView& view, const EditorUIState& state);
    void handleActions(const ActionSink& sink);
    void presentFrame(const EditorView& view, const EditorUIState& state);

    // The shell, for code that has to reach a specific panel.
    ui::qt::EditorWindow* shell() const {
        return shell_;
    }

  private:
    ui::qt::EditorWindow* shell_ = nullptr;
    SDL_Window* window_ = nullptr;
    ui::qt::PendingActions actions_{};
};
