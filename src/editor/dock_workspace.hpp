#pragma once
#include "editor/editor_layout.hpp"
#include <imgui_internal.h>

inline void drawWorkspace(const EditorRect& toolbar, bool& reset) {
    const auto* viewport = ImGui::GetMainViewport();
    const ImVec2 pos(viewport->Pos.x, toolbar.y + toolbar.height);
    const ImVec2 size(viewport->Size.x, std::max(1.0f, viewport->Size.y - pos.y + viewport->Pos.y));
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("Workspace",
                 nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoNavFocus);
    const ImGuiID id = ImGui::GetID("EditorDockspaceReferenceV3");
    if (reset || !ImGui::DockBuilderGetNode(id)) {
        reset = false;
        for (bool& open : panelOpen) {
            open = true;
        }
        ImGui::DockBuilderRemoveNode(id);
        ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(id, size);
        ImGuiID center = id;
        ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, .28f, nullptr, &center);
        ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, .34f, nullptr, &center);
        ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, .29f, nullptr, &center);
        ImGui::DockBuilderDockWindow("Hierarchy", left);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Scene", center);
        ImGui::DockBuilderDockWindow("Game", center);
        ImGui::DockBuilderDockWindow("Assets", bottom);
        ImGui::DockBuilderDockWindow("Script Editor", bottom);
        ImGui::DockBuilderDockWindow("Console", bottom);
        const char* bottomTabs[]{"Assets", "Script Editor", "Console"};
        for (short order = 0; order < 3; ++order) {
            const auto tabId = ImHashStr(bottomTabs[order]);
            if (auto* window = ImGui::FindWindowByID(tabId)) {
                window->DockOrder = order;
            }
            if (auto* settings = ImGui::FindWindowSettingsByID(tabId)) {
                settings->DockOrder = order;
            }
        }
        ImGui::DockBuilderFinish(id);
        if (auto* node = ImGui::DockBuilderGetNode(center)) {
            node->SelectedTabId = ImHashStr("Scene");
        }
        if (auto* node = ImGui::DockBuilderGetNode(bottom)) {
            node->SelectedTabId = ImHashStr("Assets");
        }
    }
    ImGui::DockSpace(id, ImVec2(0, 0));
    ImGui::End();
    ImGui::PopStyleVar(2);
}
