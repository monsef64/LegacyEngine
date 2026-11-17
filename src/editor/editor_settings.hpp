#pragma once
#include "editor/editor_palette.hpp"
#include "editor/value_context.hpp"
#include "scene/editor_grid.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include "ui/mygui_ui.hpp"
#include <sstream>
struct EditorSettings {
    bool external = false, visible = false, alwaysShowColliders = false;
    int theme = 0;
    float scrollZoomSpeed = 1.f;
    float raycastThickness = 2.f; // viewport pixels; editor gizmo only
    int maxFps = 120;             // zero disables foreground cap; background still sleeps
    int autosaveSeconds = 120;    // zero disables autosave
    GridSettings grid;
    std::string error;
    std::filesystem::path file() const {
        const char* pref = SDL_GetPrefPath("LegacyEngine", "Editor");
        return pref ? std::filesystem::path(pref) / "settings.txt" : std::filesystem::path{};
    }
    EditorSettings() {
        auto path = file();
        if (!path.empty()) {
            std::ifstream in(path);
            in >> external;
            if (!(in >> theme)) {
                theme = 0;
            }
            theme = std::clamp(theme, 0, 3);
            if (!(in >> alwaysShowColliders)) {
                alwaysShowColliders = false;
            }
        }
        {
            std::ifstream in(file());
            std::string ignored;
            std::getline(in, ignored);
            GridSettings loaded;
            if (in >> loaded.visible >> loaded.cells >> loaded.plane >> loaded.spacing >>
                    loaded.position.x >> loaded.position.y >> loaded.position.z >> loaded.color.x >>
                    loaded.color.y >> loaded.color.z &&
                loaded.valid()) {
                grid = loaded;
            }
        }
        {
            std::ifstream in(file());
            std::string line;
            std::getline(in, line);
            std::getline(in, line);
            int delay;
            if (in >> delay) {
                autosaveSeconds = std::clamp(delay, 0, 3600);
            }
        }
        {
            std::ifstream in(file());
            std::string line;
            for (int i = 0; i < 3; ++i) {
                std::getline(in, line);
            }
            float thickness;
            int fps;
            if (in >> thickness && std::isfinite(thickness)) {
                raycastThickness = std::clamp(thickness, 1.f, 8.f);
            }
            if (in >> fps) {
                maxFps = std::clamp(fps, 0, 240);
            }
            float zoom;
            if (in >> zoom && std::isfinite(zoom)) {
                scrollZoomSpeed = std::clamp(zoom, .1f, 5.f);
            }
        }
        if (ui::hasContext()) {
            applyEditorPalette(theme);
        }
    }
    std::string signature() const {
        std::ostringstream out;
        out.precision(9);
        out << scrollZoomSpeed << ':' << raycastThickness << ':' << maxFps << ':' << autosaveSeconds
            << ':' << external << theme << alwaysShowColliders << grid.visible << ':' << grid.cells
            << ':' << grid.plane << ':' << grid.spacing << ':' << grid.position.x << ':'
            << grid.position.y << ':' << grid.position.z << ':' << grid.color.x << ':'
            << grid.color.y << ':' << grid.color.z;
        return out.str();
    }
    void save() {
        auto path = file();
        if (!path.empty()) {
            std::ofstream out(path);
            out << external << ' ' << theme << ' ' << alwaysShowColliders << '\n'
                << grid.visible << ' ' << grid.cells << ' ' << grid.plane << ' ' << grid.spacing
                << ' ' << grid.position.x << ' ' << grid.position.y << ' ' << grid.position.z << ' '
                << grid.color.x << ' ' << grid.color.y << ' ' << grid.color.z << '\n'
                << autosaveSeconds << '\n'
                << raycastThickness << ' ' << maxFps << ' ' << scrollZoomSpeed;
        }
    }
    bool openExternal(const std::filesystem::path& folder, bool isFile = false) {
        std::error_code ec;
        std::filesystem::create_directories(isFile ? folder.parent_path() : folder, ec);
        if (ec) {
            error = ec.message();
            return false;
        }
        // URI launch avoids shell interpolation and works with the VS Code URL handler.
        auto raw = std::filesystem::absolute(folder).generic_string();
        std::string encoded;
        for (unsigned char c : raw) {
            if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == ':') {
                encoded += c;
            } else {
                char hex[4];
                std::snprintf(hex, 4, "%%%02X", c);
                encoded += hex;
            }
        }
        if (!SDL_OpenURL(("vscode://file/" + encoded).c_str())) {
            error = SDL_GetError();
            return false;
        }
        error.clear();
        return true;
    }
    bool openScript(const std::filesystem::path& project, const std::filesystem::path& script) {
        const auto folder = std::filesystem::absolute(project).string();
        const auto file = std::filesystem::absolute(script).string();
        // Pass arguments directly: no shell quoting/interpolation, one VS Code
        // request opens the project workspace and selects the script within it.
        std::string executable = "code";
#ifdef _WIN32
        // Windows' code.cmd cannot be launched with CreateProcess directly.
        // Use the real GUI executable, not cmd.exe or interpolated shell text.
        if (const char* local = SDL_getenv("LOCALAPPDATA")) {
            const auto candidate =
                std::filesystem::path(local) / "Programs/Microsoft VS Code/Code.exe";
            if (std::filesystem::is_regular_file(candidate)) {
                executable = candidate.string();
            }
        }
        if (executable == "code") {
            if (const char* programs = SDL_getenv("ProgramFiles")) {
                const auto candidate =
                    std::filesystem::path(programs) / "Microsoft VS Code/Code.exe";
                if (std::filesystem::is_regular_file(candidate)) {
                    executable = candidate.string();
                }
            }
        }
#endif
        const char* args[]{
            executable.c_str(), "--reuse-window", folder.c_str(), "--goto", file.c_str(), nullptr};
        if (auto* process = SDL_CreateProcess(args, false)) {
            SDL_DestroyProcess(process);
            error.clear();
            return true;
        }
        error = "Cannot launch VS Code. Install the 'code' command in PATH. " +
                std::string(SDL_GetError());
        return false;
    }
    void draw(const std::filesystem::path& folder) {
        if (!visible) {
            return;
        }
        const auto area = ui::mainViewport()->WorkSize;
        ui::setNextWindowSize(ui::Vec2(std::min(540.0f, area.x), std::min(560.0f, area.y)),
                                 ui::Cond_FirstUseEver);
        ui::setNextWindowSizeConstraints(
            ui::Vec2(std::min(360.0f, area.x), std::min(380.0f, area.y)), area);
        if (ui::beginWindow("Editor Settings", &visible, ui::WindowFlags_NoDocking)) {
            ui::setNextItemWidth(-120);
            if (EditorValues::Combo(
                    "Theme", &theme, "Legacy Dark\0Grey\0Unity 5 Classic\0Unity Dark Classic\0")) {
                applyEditorPalette(theme);
                save();
            }
            if (EditorValues::Checkbox("Always Show Colliders", &alwaysShowColliders)) {
                save();
            }
            if (EditorValues::SliderFloat(
                    "Raycast line thickness", &raycastThickness, 1, 8, "%.1f px")) {
                save();
            }
            if (EditorValues::SliderInt("FPS limit (VSync off)", &maxFps, 0, 240, "%d")) {
                save();
            }
            ui::textDisabled(
                "VSync takes priority. Limit 0 = uncapped; background target = 30 FPS.");
            if (EditorValues::InputInt("Autosave delay (seconds)", &autosaveSeconds)) {
                autosaveSeconds = std::clamp(autosaveSeconds, 0, 3600);
                save();
            }
            ui::textDisabled(
                "0 disables autosave. Only previously saved projects are autosaved.");
            ui::separatorText("Scene Grid");
            auto next = grid;
            bool changed = EditorValues::Checkbox("Show grid", &next.visible);
            changed |= EditorValues::Combo("Plane", &next.plane, "XZ\0XY\0YZ\0");
            changed |= ui::dragInt("Cells", &next.cells, 1, 2, 512);
            changed |= EditorValues::DragFloat("Spacing", &next.spacing, .05f, .01f, 1000);
            changed |= ui::dragFloat3("Grid position (X/Y/Z)", &next.position.x, .05f);
            changed |= EditorValues::ColorEdit3("Grid color", &next.color.x);
            if (changed && next.valid()) {
                grid = next;
                save();
            }
            ui::separatorText("Editor");
            int choice = external ? 1 : 0;
            ui::setNextItemWidth(-120);
            if (EditorValues::Combo("Code editor", &choice, "Internal\0External (VS Code)\0")) {
                external = choice == 1;
                save();
                if (external) {
                    openExternal(folder);
                }
            }
            ui::textWrapped(
                "External mode opens the selected script in VS Code when double-clicked. VS Code's code command must be in PATH (standard Windows installations are also detected).");
            if (external && ui::button("Open project in VS Code")) {
                openExternal(folder);
            }
            if (!error.empty()) {
                ui::textWrapped("%s", error.c_str());
            }
        }
        ui::endWindow();
    }
};
