#pragma once
#include <algorithm>
#include <cstring>
#include <imgui.h>

struct EditorRect {
    float x=0,y=0,width=0,height=0;
    bool contains(float px,float py) const {return px>=x && py>=y && px<x+width && py<y+height;}
};
struct EditorPixels {
    int x,top,width,height;
    float aspect() const {return static_cast<float>(width)/height;}
    int bottom(int framebufferHeight) const {return framebufferHeight-top-height;}
};
inline EditorPixels scenePixels(const EditorRect& rect,int windowWidth,int windowHeight,int width,int height) {
    const float sx=static_cast<float>(width)/std::max(1,windowWidth),sy=static_cast<float>(height)/std::max(1,windowHeight);
    const int x=static_cast<int>(rect.x*sx),top=static_cast<int>(rect.y*sy);
    return {x,top,std::max(1,static_cast<int>((rect.x+rect.width)*sx)-x),
                   std::max(1,static_cast<int>((rect.y+rect.height)*sy)-top)};
}
struct EditorLayout {
    EditorRect toolbar,hierarchy,scene,inspector,lua,console;
    static EditorLayout calculate(float width,float height,float menuHeight) {
        width=std::max(width,1.0f);height=std::max(height,1.0f);
        const float top=std::min(menuHeight,height),bar=std::min(38.0f,std::max(0.0f,height-top));
        const float y=top+bar,remaining=std::max(1.0f,height-y);
        const float left=width*0.19f,right=width*0.25f,middle=width-left-right;
        const float bottom=remaining*0.34f,sceneHeight=remaining-bottom;
        return {{0,top,width,bar},{0,y,left,remaining},{left,y,middle,sceneHeight},
                {width-right,y,right,remaining},{left,y+sceneHeight,middle*0.55f,bottom},
                {left+middle*0.55f,y+sceneHeight,middle*0.45f,bottom}};
    }
};
// Tests/legacy callers retain the fixed-layout fallback; the application opts in.
inline bool dockedEditor=false;
inline bool panelOpen[7]{true,true,true,true,true,true,true};
inline const char* panelNames[]{"Hierarchy","Inspector","Scene","Lua Editor","Lua Console","Assets","Game"};
inline bool* panelVisibility(const char* name) {
    if(dockedEditor)for(int i=0;i<7;++i)if(!std::strcmp(name,panelNames[i]))return &panelOpen[i];
    return nullptr;
}
inline bool beginEditorPanel(const char* name,ImGuiWindowFlags fallback=0) {
    bool* open=panelVisibility(name);
    return ImGui::Begin(name,open,dockedEditor?0:fallback);
}
inline constexpr ImGuiWindowFlags fixedPanelFlags=ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
inline void placeEditorPanel(const EditorRect& rect) {
    if(dockedEditor)return;
    ImGui::SetNextWindowPos(ImVec2(rect.x,rect.y),ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(std::max(rect.width,1.0f),std::max(rect.height,1.0f)),ImGuiCond_Always);
}
