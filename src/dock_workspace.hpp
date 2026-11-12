#pragma once
#include "editor_layout.hpp"
#include <imgui_internal.h>

inline void drawWorkspace(const EditorRect& toolbar,bool& reset) {
    const auto* viewport=ImGui::GetMainViewport();
    const ImVec2 pos(viewport->Pos.x,toolbar.y+toolbar.height);
    const ImVec2 size(viewport->Size.x,std::max(1.0f,viewport->Size.y-pos.y+viewport->Pos.y));
    ImGui::SetNextWindowPos(pos);ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0);
    ImGui::Begin("Workspace",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|
        ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoBringToFrontOnFocus|ImGuiWindowFlags_NoNavFocus);
    const ImGuiID id=ImGui::GetID("EditorDockspace");
    if(reset || !ImGui::DockBuilderGetNode(id)) {
        reset=false;for(bool& open:panelOpen)open=true;
        ImGui::DockBuilderRemoveNode(id);ImGui::DockBuilderAddNode(id,ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(id,size);
        ImGuiID center=id;
        ImGuiID left=ImGui::DockBuilderSplitNode(center,ImGuiDir_Left,.20f,nullptr,&center);
        ImGuiID right=ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,.29f,nullptr,&center);
        ImGuiID bottom=ImGui::DockBuilderSplitNode(center,ImGuiDir_Down,.34f,nullptr,&center);
        ImGuiID console=ImGui::DockBuilderSplitNode(bottom,ImGuiDir_Right,.45f,nullptr,&bottom);
        ImGui::DockBuilderDockWindow("Hierarchy",left);ImGui::DockBuilderDockWindow("Inspector",right);
        ImGui::DockBuilderDockWindow("Scene",center);ImGui::DockBuilderDockWindow("Assets",bottom);
        ImGui::DockBuilderDockWindow("Lua Editor",bottom);ImGui::DockBuilderDockWindow("Lua Console",console);ImGui::DockBuilderDockWindow("Game",console);
        ImGui::DockBuilderFinish(id);
    }
    ImGui::DockSpace(id,ImVec2(0,0));ImGui::End();ImGui::PopStyleVar(2);
}
