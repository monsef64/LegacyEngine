#include "editor/editor_layout.hpp"
#include "editor/dock_workspace.hpp"
#include "scene/editor_grid.hpp"
#include "editor/scene_orientation.hpp"
#include "editor/inspector.hpp"
#include "editor/create_menu.hpp"
#include "editor/script_editor.hpp"
#include "physics/physics.hpp"
#include <imgui_internal.h>
#include <cstdio>
#include <cstdlib>

static void require(bool ok,const char* text){if(!ok){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
int main(){
    GridSettings grid;require(grid.valid(),"default grid settings valid");grid.cells=0;require(!grid.valid(),"grid size is bounded");grid.cells=512;grid.spacing=.01f;grid.plane=2;require(grid.valid(),"grid supports alternate planes and spacing");grid.color.x=2;require(!grid.valid(),"grid color validated");

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
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.ConfigFlags|=ImGuiConfigFlags_DockingEnable;
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
    io.AddMousePosEvent(layout.inspector.x+30,layout.inspector.y+125);frame();
    io.AddKeyEvent(ImGuiMod_Ctrl,true);frame();
    io.AddMouseButtonEvent(0,true);frame();io.AddMouseButtonEvent(0,false);frame();
    io.AddKeyEvent(ImGuiKey_A,true);frame();io.AddKeyEvent(ImGuiKey_A,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);frame();
    io.AddInputCharactersUTF8("7");frame();
    io.AddKeyEvent(ImGuiKey_Enter,true);frame();io.AddKeyEvent(ImGuiKey_Enter,false);frame();
    require(world.get<Transform>(object).position.x==7,"Inspector edits live Transform position");
    // Removal works from a collapsed component header, with no transport buttons.
    world.add(object,AudioSource{});
    ImVec2 headerPosition;
    auto componentFrame=[&]{
        ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(50,50));ImGui::SetNextWindowSize(ImVec2(400,250));
        ImGui::Begin("Component context test");
        if(world.has<AudioSource>(object)){
            headerPosition=ImGui::GetCursorScreenPos();ImGui::SetNextItemOpen(false,ImGuiCond_Always);
            componentHeader<AudioSource>(world,object,"AudioSource");
        }
        ImGui::End();ImGui::Render();
    };
    componentFrame();componentFrame();
    io.AddMousePosEvent(headerPosition.x+80,headerPosition.y+7);componentFrame();
    io.AddMouseButtonEvent(1,true);componentFrame();io.AddMouseButtonEvent(1,false);componentFrame();
    require(!GImGui->OpenPopupStack.empty(),"right click collapsed header opens component menu");
    auto* popup=GImGui->OpenPopupStack.back().Window;require(popup!=nullptr,"component menu is rendered");
    io.AddMousePosEvent(popup->Pos.x+30,popup->Pos.y+ImGui::GetStyle().WindowPadding.y+7);componentFrame();
    io.AddMouseButtonEvent(0,true);componentFrame();io.AddMouseButtonEvent(0,false);componentFrame();
    require(!world.has<AudioSource>(object),"context menu removes component");
    bool reset=true;dockedEditor=true;
    for(int i=0;i<3;++i){
        ImGui::NewFrame();drawWorkspace({0,0,1280,40},reset);
        for(const char* name:panelNames){ImGui::Begin(name);ImGui::TextUnformatted("Panel");ImGui::End();}
        ImGui::Render();
    }
    auto* hierarchy=ImGui::FindWindowByName("Hierarchy");auto* inspector=ImGui::FindWindowByName("Inspector");
    auto* scene=ImGui::FindWindowByName("Scene");auto* game=ImGui::FindWindowByName("Game");
    auto* assets=ImGui::FindWindowByName("Assets");auto* console=ImGui::FindWindowByName("Console");
    require(scene->DockId==game->DockId && assets->DockId==console->DockId,"Scene/Game and Assets/Console share tabs");
    require(assets->Pos.x<scene->Pos.x && assets->Pos.y>hierarchy->Pos.y,"assets spans below hierarchy and scene");
    require(inspector->Pos.x>scene->Pos.x && inspector->Size.y>scene->Size.y,"Inspector occupies full right column");
    const auto* tabs=assets->DockNode->TabBar;
    require(tabs && tabs->Tabs.Size==3,"three bottom workspace tabs");
    require(tabs->Tabs[0].ID==assets->TabId && tabs->Tabs[1].ID==ImGui::FindWindowByName("Script Editor")->TabId && tabs->Tabs[2].ID==console->TabId,"bottom order Assets Script Editor Console");
    OrbitCamera orientation;
    ImGui::NewFrame();ImGui::Begin("Orientation test");drawSceneOrientation(orientation,{0,0,400,300},true);ImGui::End();ImGui::Render();
    ImGui::DestroyContext();dockedEditor=false;
    // Objects stay alive below the former cleanup cutoff, with their body intact.
    Physics physics;world.get<Transform>(object).position={20,-300,0};
    for(int i=0;i<600;++i)physics.update(world,1.0f/60);
    require(world.alive(object) && world.has<RigidBody>(object),"fallen objects retained as ordinary objects");
    std::puts("Editor panel geometry, Inspector editing and retained fallen-object tests passed");
}
