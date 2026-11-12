#pragma once
#include <imgui.h>
// Palette-only changes never rebuild the font atlas.
inline void applyEditorPalette(int theme){
    static ImVec4 original[ImGuiCol_COUNT];static bool captured=false;
    if(!captured){for(int i=0;i<ImGuiCol_COUNT;++i)original[i]=ImGui::GetStyle().Colors[i];captured=true;}
    for(int i=0;i<ImGuiCol_COUNT;++i){auto color=original[i];if(theme==1){float grey=.2126f*color.x+.7152f*color.y+.0722f*color.z;color.x=color.y=color.z=grey;}ImGui::GetStyle().Colors[i]=color;}
}
