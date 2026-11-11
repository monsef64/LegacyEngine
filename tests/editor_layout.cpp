#include "editor_layout.hpp"
#include "inspector.hpp"
#include "create_menu.hpp"
#include "script_editor.hpp"
#include "physics.hpp"
#include <cstdio>
#include <cstdlib>

static void require(bool ok,const char* text){if(!ok){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
int main(){
    for(ImVec2 size:{ImVec2(800,600),ImVec2(1280,720),ImVec2(1920,1080)}) {
        const auto l=EditorLayout::calculate(size.x,size.y,20);
        require(l.hierarchy.x+l.hierarchy.width==l.scene.x,"hierarchy touches scene without overlap");
        require(std::abs(l.scene.x+l.scene.width-l.inspector.x)<0.01f,"scene touches inspector");
        require(std::abs(l.scene.y+l.scene.height-l.lua.y)<0.01f,"scripts below scene");
        require(std::abs(l.console.x+l.console.width-l.inspector.x)<0.01f,"console fits center column");
        require(!l.scene.contains(l.inspector.x+10,l.inspector.y+30),"inspector clicks outside scene");
        require(l.scene.contains(l.scene.x+l.scene.width*0.5f,l.scene.y+l.scene.height*0.5f),"scene center inside viewport");
    }
    for(int density:{1,2}) {
        const auto l=EditorLayout::calculate(1280,720,20);
        const auto pixels=scenePixels(l.scene,1280,720,1280*density,720*density);
        require(pixels.width>0 && pixels.height>0 && pixels.bottom(720*density)>=0,"Scene rectangle maps to framebuffer pixels");
        require(pixels.x==static_cast<int>(l.scene.x*density),"high-DPI viewport origin consistent");
    }
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
    io.DisplaySize=ImVec2(1280,720);io.DeltaTime=1.0f/60;
    unsigned char* pixels;int w,h;io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);
    World world;
    Entity object=createLevelObject(world,"Selected",{0,1,0},MeshHandle{1});
    const MeshHandle meshes[]{MeshHandle{1},MeshHandle{2},MeshHandle{3},MeshHandle{4}};
    const auto layout=EditorLayout::calculate(1280,720,20);
    auto frame=[&]{ImGui::NewFrame();drawInspector(world,object,layout.inspector,meshes,false);ImGui::Render();};
    frame();frame();
    // Rename through the actual Inspector text field.
    io.AddMousePosEvent(layout.inspector.x+70,layout.inspector.y+35);frame();
    io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
    io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_A,true);frame();
    io.AddKeyEvent(ImGuiKey_A,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);frame();
    io.AddInputCharactersUTF8("Renamed");frame();
    require(world.get<ObjectName>(object).value=="Renamed","Inspector edits live object name");
    io.AddKeyEvent(ImGuiKey_Enter,true);frame();io.AddKeyEvent(ImGuiKey_Enter,false);frame();
    io.AddMousePosEvent(layout.inspector.x+30,layout.inspector.y+105);frame();
    io.AddKeyEvent(ImGuiMod_Ctrl,true);frame();
    io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
    io.AddKeyEvent(ImGuiKey_A,true);frame();io.AddKeyEvent(ImGuiKey_A,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);frame();
    io.AddInputCharactersUTF8("7");frame();
    io.AddKeyEvent(ImGuiKey_Enter,true);frame();io.AddKeyEvent(ImGuiKey_Enter,false);frame();
    require(world.get<Transform>(object).position.x==7,"Inspector edits live Transform position");
    ImGui::DestroyContext();
    // Objects stay alive below the former cleanup cutoff, with their body intact.
    Physics physics;world.get<Transform>(object).position={20,-300,0};
    for(int i=0;i<600;++i)physics.update(world,1.0f/60);
    require(world.alive(object) && world.has<RigidBody>(object),"fallen objects retained as ordinary objects");
    std::puts("Editor panel geometry, Inspector editing and retained fallen-object tests passed");
}
