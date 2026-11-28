#pragma once
#include "editor/editor_theme.hpp"
#include "editor/recent_projects.hpp"
#include "platform/project_dialog.hpp"
#include <SDL3/SDL.h>
#include <functional>
#include <glad/gl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

// A standalone startup loop: no dockspace, scene, tool windows or editor shortcuts
// are submitted until a project has been successfully created/opened and warmed.
inline bool
launchProject(SDL_Window* window,
              RecentProjects& recent,
              const std::function<bool(const ProjectDialog::Result&, std::string&)>& open) {
    ProjectDialog picker;
    std::string error;
    SDL_SetWindowTitle(window, "LegacyEngine | Projects");
    while (true) {
        const Uint64 started = SDL_GetTicksNS();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                return false;
            }
            ImGui_ImplSDL3_ProcessEvent(&event);
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(.035f, .045f, .065f, 1));
        ImGui::Begin("##ProjectLauncher",
                     nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking);
        auto* draw = ImGui::GetWindowDrawList();
        const auto origin = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        draw->AddRectFilledMultiColor(origin,
                                      ImVec2(origin.x + size.x, origin.y + 210),
                                      IM_COL32(25, 43, 69, 255),
                                      IM_COL32(17, 29, 49, 255),
                                      IM_COL32(9, 12, 18, 255),
                                      IM_COL32(9, 12, 18, 255));
        const float margin = std::max(18.f, std::min(64.f, size.x * .06f));
        ImGui::SetCursorPos(ImVec2(margin, 32));
        ImGui::BeginChild("Content",
                          ImVec2(std::max(1.f, size.x - margin * 2), std::max(1.f, size.y - 50)),
                          false);
        ImGui::TextColored(ImVec4(.43f, .72f, 1, 1), "L E G A C Y E N G I N E");
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::SetWindowFontScale(size.x < 600 ? 1.3f : 1.8f);
        ImGui::TextWrapped("Make something extraordinary.");
        ImGui::SetWindowFontScale(1.f);
        ImGui::TextDisabled("Start a new world, or pick up where you left off.");
        ImGui::Dummy(ImVec2(0, 22));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(18, 14));
        ImGui::BeginDisabled(picker.pending());
        const float available = ImGui::GetContentRegionAvail().x;
        const bool compact = available < 540;
        const float buttonWidth = compact ? available : std::min(280.f, (available - 16) * .5f);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.16f, .38f, .67f, 1));
        if (ImGui::Button("\xef\x81\xa7  New Project", ImVec2(buttonWidth, 60))) {
            picker.show(window, true, {});
        }
        ImGui::PopStyleColor();
        if (!compact) {
            ImGui::SameLine(0, 16);
        }
        if (ImGui::Button("\xef\x81\xbc  Open Project", ImVec2(buttonWidth, 60))) {
            picker.show(window, false, {});
        }
        ImGui::Dummy(ImVec2(0, 25));
        ImGui::TextUnformatted("RECENT PROJECTS");
        ImGui::SameLine();
        ImGui::TextDisabled("/ %d", int(recent.paths().size()));
        ImGui::Separator();
        if (recent.paths().empty()) {
            ImGui::Dummy(ImVec2(0, 20));
            ImGui::TextDisabled("Your next project starts here.");
            ImGui::TextWrapped(
                "Create a project or open a .legacy file. Recently opened projects will appear in this list.");
        }
        for (const auto& path : recent.paths()) {
            std::error_code ec;
            const bool exists = std::filesystem::is_regular_file(path, ec);
            ImGui::PushID(path.c_str());
            const auto position = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(
                position,
                ImVec2(position.x + available, position.y + 63),
                IM_COL32(18, 26, 39, 255),
                8.f);
            if (ImGui::Selectable("##project", false, ImGuiSelectableFlags_None, ImVec2(0, 65))) {
                if (exists) {
                    picker.openRecent(path);
                } else {
                    error = "Project not found. Use Open Project to locate its new location.";
                }
            }
            draw = ImGui::GetWindowDrawList();
            draw->AddText(ImVec2(position.x + 12, position.y + 12),
                          IM_COL32(115, 177, 242, 255),
                          "\xef\x86\xb2");
            const float textX = position.x + 48;
            draw->PushClipRect(ImVec2(textX, position.y),
                               ImVec2(position.x + available - 8, position.y + 65),
                               true);
            draw->AddText(ImVec2(textX, position.y + 6),
                          exists ? IM_COL32(226, 234, 246, 255) : IM_COL32(179, 133, 123, 255),
                          std::filesystem::path(path).stem().string().c_str());
            draw->AddText(
                ImVec2(textX, position.y + 32), IM_COL32(130, 146, 169, 255), path.c_str());
            draw->PopClipRect();
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s%s", path.c_str(), exists ? "" : "\nFile not found");
            }
            ImGui::PopID();
        }
        ImGui::EndDisabled();
        if (!error.empty()) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, .55f, .45f, 1));
            ImGui::TextWrapped("%s", error.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::PopStyleVar(2);
        ImGui::EndChild();
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ProjectDialog::drawAll();
        ImGui::Render();
        finishEditorTheme(ImGui::GetDrawData());
        int width = 1, height = 1;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width, height);
        glClearColor(.035f, .045f, .065f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
        if (auto result = picker.poll()) {
            if (!result->error.empty()) {
                error = result->error;
            } else if (!result->path.empty()) {
                error.clear();
                if (open(*result, error)) {
                    return true;
                }
            }
        }
        // Launcher has no simulation. Avoid spinning with VSync disabled/backgrounded.
        const auto elapsed = SDL_GetTicksNS() - started;
        if (elapsed < 16666667) {
            SDL_DelayNS(16666667 - elapsed);
        }
    }
}
