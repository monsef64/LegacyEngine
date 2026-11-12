#include "editor/rotation_editor.hpp"
#include "editor/asset_browser.hpp"
#include "editor/editor_input.hpp"
#include "scripting/script_components.hpp"
#include "project/project_io.hpp"
#include "editor/editor_history.hpp"
#include "editor/level_objects.hpp"
#include "rendering/mesh.hpp"
#include <cassert>

int main() {
    for(Vec3 angles:{Vec3{10,20,30},Vec3{25,80,-40},Vec3{-45,-60,70}}) {
        auto q=rotationFromEuler(angles),round=rotationFromEuler(rotationToEuler(q));
        assert(std::abs(q.x*round.x+q.y*round.y+q.z*round.z+q.w*round.w)>.99999f);
    }
    RotationEditorState state;Entity selected{1,0};
    state.sync(selected,rotationFromEuler({20,80,30}));
    for(float y=80;y<=110;y+=.5f) {
        auto angles=state.angles;angles.y=y;auto q=state.apply(angles);state.sync(selected,q);
        assert(std::abs(state.angles.x-20)<.001f && std::abs(state.angles.z-30)<.001f);
    }
    assert(sceneCameraSpeed(SDL_KMOD_SHIFT,3)==15);
    assert(sceneCameraSpeed(SDL_KMOD_NONE,3)==5);
    assert(adjustSceneCameraBoost(3,1)>3 && adjustSceneCameraBoost(3,-1)<3);
    assert(cameraModifiersHeld(SDL_KMOD_CTRL|SDL_KMOD_SHIFT));
    assert(!cameraModifiersHeld(SDL_KMOD_SHIFT));
    assert(makeCameraModel().topology==MeshData::Topology::Lines);
    const auto temp=std::filesystem::temp_directory_path()/
        ("legacy-multiscript-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    AssetManager assets;assets.root=temp;
    assets.writeScript("A.ls","function onUpdate(dt) self:translate(dt,0,0) end");
    assets.writeScript("B.ls","function onUpdate(dt) self:translate(0,dt,0) end");
    World world;auto e=world.create();world.add(e,Transform{});
    assert(attachScriptAsset(world,e,"A.ls"));assert(attachScriptAsset(world,e,"B.ls"));
    assert(world.get<LuaScript>(e).additional.size()==1);
    assert(attachScriptAsset(world,e,"B.ls"));assert(world.get<LuaScript>(e).additional.size()==1);
    MeshHandle meshes[]{MeshHandle{1},MeshHandle{2},MeshHandle{3},MeshHandle{4}};
    ScriptComponents runtime;runtime.start(world,assets,meshes);runtime.update(.5f);
    assert((world.get<Transform>(e).position==Vec3{.5f,.5f,0}));
    world.get<LuaScript>(e).additional[0].enabled=false;runtime.update(.5f);
    assert((world.get<Transform>(e).position==Vec3{1,.5f,0}));runtime.stop();
    auto copy=duplicateLevelObject(world,e);assert(world.get<LuaScript>(copy).additional.size()==1);
    EditorHistory history(world);world.get<LuaScript>(e).additional.clear();history.commit(world,e);
    Entity selection;history.undo(world,selection);assert(world.get<LuaScript>(e).additional.size()==1);
    const ProjectIO::Meshes catalog{MeshHandle{1},MeshHandle{2},MeshHandle{3},MeshHandle{4},MeshHandle{5},MeshHandle{6}};
    ProjectIO::Loaded loaded;ProjectIO::decode(ProjectIO::encode(world,"",OrbitCamera{},catalog),loaded,catalog);
    assert(loaded.world.get<LuaScript>(e).additional[0].path=="B.ls");
    assert(!loaded.world.get<LuaScript>(e).additional[0].enabled);
    assert(AssetManager::script("hello.ls") && AssetManager::script("old.lua"));
    auto parent=world.create();world.add(parent,ObjectName{"Group"});world.add(parent,Transform{});
    auto child=world.create();world.add(child,ObjectName{"Main Camera"});world.add(child,Transform{});setParent(world,child,parent);
    const auto beforeSearch=world.entities().size();
    auto matches=hierarchyMatches(world,levelObjects(world),"CAMERA");
    assert(matches.count(hierarchyKey(child)) && matches.count(hierarchyKey(parent)));
    assert(!matches.count(hierarchyKey(e)));
    assert(hierarchyMatches(world,levelObjects(world),"no-such-name").empty());
    assert(world.entities().size()==beforeSearch);
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
    io.DisplaySize=ImVec2(1000,700);io.DeltaTime=1.0f/60;io.Fonts->Build();
    AssetBrowser browser;browser.assets.root=temp;ScriptEditor editor;assert(editor.source().empty());
    int request=-1;EditorRect hierarchy{500,20,400,500};
    auto frame=[&] {
        ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(20,20));ImGui::SetNextWindowSize(ImVec2(420,500));
        browser.draw(nullptr,world,e,editor,true,&history);
        drawLevelObjects(world,selection,true,&hierarchy,nullptr,&request);
        ImGui::Render();
    };
    frame();frame();
    auto rightClick=[&](float x,float y){io.AddMousePosEvent(x,y);frame();io.AddMouseButtonEvent(1,true);frame();io.AddMouseButtonEvent(1,false);frame();};
    rightClick(700,430);assert(ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId));
    io.AddKeyEvent(ImGuiKey_Escape,true);frame();io.AddKeyEvent(ImGuiKey_Escape,false);frame();
    rightClick(200,450);assert(ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId));
    ImGui::DestroyContext();
    std::filesystem::remove_all(temp);
}
