#pragma once
#include <imgui.h>
enum class PlaybackIcon { Play, Pause, Stop };
inline bool playbackButton(const char* id,const char* tooltip,PlaybackIcon icon,bool active=false) {
    if(active)ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_HeaderActive));
    const bool clicked=ImGui::Button(id,ImVec2(30,24));
    if(active)ImGui::PopStyleColor();
    const ImVec2 a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
    const ImVec2 center((a.x+b.x)*.5f,(a.y+b.y)*.5f);
    auto* draw=ImGui::GetWindowDrawList();const auto color=ImGui::GetColorU32(ImGuiCol_Text);
    if(icon==PlaybackIcon::Play)draw->AddTriangleFilled(ImVec2(center.x-4,center.y-6),ImVec2(center.x-4,center.y+6),ImVec2(center.x+6,center.y),color);
    else if(icon==PlaybackIcon::Stop)draw->AddRectFilled(ImVec2(center.x-5,center.y-5),ImVec2(center.x+5,center.y+5),color,1);
    else {
        draw->AddRectFilled(ImVec2(center.x-5,center.y-6),ImVec2(center.x-2,center.y+6),color,1);
        draw->AddRectFilled(ImVec2(center.x+2,center.y-6),ImVec2(center.x+5,center.y+6),color,1);
    }
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("%s",tooltip);
    return clicked;
}
