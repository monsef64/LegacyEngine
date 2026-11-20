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
#include <imgui.h>
#include <sstream>
struct EditorSettings {
    bool external = false, visible = false, alwaysShowColliders = false;
    int theme = 0;
    int externalEditor = 0; // 0: VS Code, 1: Sublime Text 4
    float scrollZoomSpeed = 1.f;
    float raycastThickness = 2.f; // viewport pixels; editor gizmo only
    int maxFps = 0;               // zero disables foreground cap; background pauses
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
            int editor;
            if (in >> editor) {
                externalEditor = std::clamp(editor, 0, 1);
            }
        }
        if (ImGui::GetCurrentContext()) {
            applyEditorPalette(theme);
        }
    }
    std::string signature() const {
        std::ostringstream out;
        out.precision(9);
        out << scrollZoomSpeed << ':' << raycastThickness << ':' << maxFps << ':' << autosaveSeconds
            << ':' << externalEditor << ':' << external << theme << alwaysShowColliders
            << grid.visible << ':' << grid.cells << ':' << grid.plane << ':' << grid.spacing << ':'
            << grid.position.x << ':' << grid.position.y << ':' << grid.position.z << ':'
            << grid.color.x << ':' << grid.color.y << ':' << grid.color.z;
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
                << raycastThickness << ' ' << maxFps << ' ' << scrollZoomSpeed << ' '
                << externalEditor;
        }
    }
    bool openSublime(const std::filesystem::path& folder,
                     const std::filesystem::path& script = {}) {
        std::string executable = "subl";
#ifdef _WIN32
        for (const char* variable : {"ProgramFiles", "LOCALAPPDATA"}) {
            if (const char* base = SDL_getenv(variable)) {
                for (const char* suffix : {"Sublime Text/sublime_text.exe",
                                           "Programs/Sublime Text/sublime_text.exe",
                                           "Sublime Text 4/sublime_text.exe"}) {
                    const auto candidate = std::filesystem::path(base) / suffix;
                    if (std::filesystem::is_regular_file(candidate)) {
                        executable = candidate.string();
                        break;
                    }
                }
                if (executable != "subl") {
                    break;
                }
            }
        }
#elif defined(__APPLE__)
        const std::filesystem::path bundled =
            "/Applications/Sublime Text.app/Contents/SharedSupport/bin/subl";
        if (std::filesystem::is_regular_file(bundled)) {
            executable = bundled.string();
        }
#endif
        const auto project = std::filesystem::absolute(folder).string();
        const auto file =
            script.empty() ? std::string{} : std::filesystem::absolute(script).string();
        const char* args[]{executable.c_str(),
                           "--add",
                           project.c_str(),
                           file.empty() ? nullptr : file.c_str(),
                           nullptr};
        if (auto* process = SDL_CreateProcess(args, false)) {
            SDL_DestroyProcess(process);
            error.clear();
            return true;
        }
        error = "Cannot launch Sublime Text 4. Install the 'subl' command in PATH. " +
                std::string(SDL_GetError());
        return false;
    }
    bool openExternal(const std::filesystem::path& folder, bool isFile = false) {
        std::error_code ec;
        std::filesystem::create_directories(isFile ? folder.parent_path() : folder, ec);
        if (ec) {
            error = ec.message();
            return false;
        }
        if (externalEditor == 1) {
            return openSublime(isFile ? folder.parent_path() : folder,
                               isFile ? folder : std::filesystem::path{});
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
        if (externalEditor == 1) {
            return openSublime(project, script);
        }
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
        const auto area = ImGui::GetMainViewport()->WorkSize;
        ImGui::SetNextWindowSize(ImVec2(std::min(540.0f, area.x), std::min(560.0f, area.y)),
                                 ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(std::min(360.0f, area.x), std::min(380.0f, area.y)), area);
        if (ImGui::Begin("Editor Settings", &visible, ImGuiWindowFlags_NoDocking)) {
            ImGui::SetNextItemWidth(-120);
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
            ImGui::TextDisabled(
                "VSync takes priority. Limit 0 = uncapped; unfocused engine updates/rendering are paused.");
            if (EditorValues::InputInt("Autosave delay (seconds)", &autosaveSeconds)) {
                autosaveSeconds = std::clamp(autosaveSeconds, 0, 3600);
                save();
            }
            ImGui::TextDisabled(
                "0 disables autosave. Only previously saved projects are autosaved.");
            ImGui::SeparatorText("Scene Grid");
            auto next = grid;
            bool changed = EditorValues::Checkbox("Show grid", &next.visible);
            changed |= EditorValues::Combo("Plane", &next.plane, "XZ\0XY\0YZ\0");
            changed |= ImGui::DragInt("Cells", &next.cells, 1, 2, 512);
            changed |= EditorValues::DragFloat("Spacing", &next.spacing, .05f, .01f, 1000);
            changed |= ImGui::DragFloat3("Grid position (X/Y/Z)", &next.position.x, .05f);
            changed |= EditorValues::ColorEdit3("Grid color", &next.color.x);
            if (changed && next.valid()) {
                grid = next;
                save();
            }
            ImGui::SeparatorText("Editor");
            int choice = external ? externalEditor + 1 : 0;
            ImGui::SetNextItemWidth(-120);
            if (EditorValues::Combo(
                    "Code editor", &choice, "Internal\0VS Code\0Sublime Text 4\0")) {
                external = choice != 0;
                if (external) {
                    externalEditor = choice - 1;
                }
                save();
                if (external) {
                    openExternal(folder);
                }
            }
            ImGui::TextWrapped(
                "External mode opens scripts in the selected editor. Install 'code' or 'subl' in PATH; standard Windows installations are also detected. Sublime language support is in extensions/sublime-legacyscript.");
            if (external && ImGui::Button("Open project in external editor")) {
                openExternal(folder);
            }
            if (!error.empty()) {
                ImGui::TextWrapped("%s", error.c_str());
            }
        }
        ImGui::End();
    }
};
