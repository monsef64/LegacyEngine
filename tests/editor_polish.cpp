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
    bool focusHierarchy=false,focusAssets=false;
    auto frame=[&] {
        ImGui::NewFrame();ImGui::SetNextWindowPos(ImVec2(20,20));ImGui::SetNextWindowSize(ImVec2(420,500));
        if(focusAssets){ImGui::SetNextWindowFocus();focusAssets=false;}
        browser.draw(nullptr,world,e,editor,true,&history);
        if(focusHierarchy){ImGui::SetNextWindowFocus();focusHierarchy=false;}
        drawLevelObjects(world,selection,true,&hierarchy,nullptr,&request);
        ImGui::Render();
    };
    frame();frame();
    auto rightClick=[&](float x,float y){io.AddMousePosEvent(x,y);frame();io.AddMouseButtonEvent(1,true);frame();io.AddMouseButtonEvent(1,false);frame();};
    rightClick(700,430);assert(ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId));
    io.AddKeyEvent(ImGuiKey_Escape,true);frame();io.AddKeyEvent(ImGuiKey_Escape,false);frame();
    rightClick(200,450);assert(ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId));
    io.AddKeyEvent(ImGuiKey_Escape,true);frame();io.AddKeyEvent(ImGuiKey_Escape,false);frame();
    selection=child;focusHierarchy=true;frame();
    io.AddKeyEvent(ImGuiKey_F2,true);frame();io.AddKeyEvent(ImGuiKey_F2,false);frame();
    assert(ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId));
    io.AddInputCharactersUTF8("Renamed Camera");frame();
    io.AddKeyEvent(ImGuiKey_Enter,true);frame();io.AddKeyEvent(ImGuiKey_Enter,false);frame();
    assert(world.get<ObjectName>(child).value=="Renamed Camera");
    browser.selected="A.ls";focusAssets=true;frame();
    io.AddKeyEvent(ImGuiKey_F2,true);frame();io.AddKeyEvent(ImGuiKey_F2,false);frame();
    assert(ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId));
    io.AddInputCharactersUTF8("Renamed.ls");frame();
    io.AddKeyEvent(ImGuiKey_Enter,true);frame();io.AddKeyEvent(ImGuiKey_Enter,false);frame();
    assert(browser.selected=="Renamed.ls" && std::filesystem::exists(temp/"Renamed.ls"));
    assert(world.get<LuaScript>(e).path=="Renamed.ls");
    std::filesystem::create_directories(temp/"Folder"/"Nested");
    browser.assets.writeScript("Folder/Nested/Test.ls","");
    world.get<LuaScript>(e).path="Folder/Nested/Test.ls";
    AudioSource source;source.clip="Folder/Nested/sound.wav";world.add(e,source);
    MeshRenderer materialMesh;materialMesh.material="Folder/Stone.mat";materialMesh.materialValues.texture="Folder/Nested/image.png";world.add(e,materialMesh);
    MaterialProperties material;material.texture="Folder/Nested/image.png";saveMaterial(browser.assets,"Folder/Stone.mat",material);
    ScriptField exported;exported.name="image";exported.type="Texture";exported.text="Folder/Nested/image.png";world.get<LuaScript>(e).fields.push_back(exported);
    browser.openedScript="Folder/Nested/Test.ls";
    browser.selected="Folder";focusAssets=true;frame();
    io.AddKeyEvent(ImGuiKey_F2,true);frame();io.AddKeyEvent(ImGuiKey_F2,false);frame();
    assert(ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId));
    io.AddInputCharactersUTF8("Renamed Folder");frame();
    io.AddKeyEvent(ImGuiKey_Enter,true);frame();io.AddKeyEvent(ImGuiKey_Enter,false);frame();
    assert(browser.selected=="Renamed Folder" && std::filesystem::exists(temp/"Renamed Folder/Nested/Test.ls"));
    assert(world.get<LuaScript>(e).path=="Renamed Folder/Nested/Test.ls");
    assert(world.get<LuaScript>(e).fields.back().text=="Renamed Folder/Nested/image.png");
    assert(world.get<AudioSource>(e).clip=="Renamed Folder/Nested/sound.wav");
    assert(world.get<MeshRenderer>(e).material=="Renamed Folder/Stone.mat");
    assert(loadMaterial(browser.assets,"Renamed Folder/Stone.mat").texture=="Renamed Folder/Nested/image.png");
    assert(browser.openedScript=="Renamed Folder/Nested/Test.ls");
    browser.currentFolder="Renamed Folder/Nested";
    browser.renameSelected(world,"Final");
    assert(browser.currentFolder=="Final/Nested" && browser.openedScript=="Final/Nested/Test.ls");
    std::filesystem::create_directory(temp/"Taken");
    bool rejected=false;try{browser.renameSelected(world,"Taken");}catch(...){rejected=true;}assert(rejected);
    rejected=false;try{browser.renameSelected(world,"../Escape");}catch(...){rejected=true;}assert(rejected);
    browser.selected="Final/Nested/Test.ls";browser.renameSelected(world,"Player");
    assert(browser.selected=="Final/Nested/Player.ls" && browser.openedScript==browser.selected);
    assert(std::filesystem::exists(temp/"Final/Nested/Player.ls"));
    browser.renameSelected(world,"PlayerTwo.ls");assert(browser.selected=="Final/Nested/PlayerTwo.ls");
    ImGui::DestroyContext();
    std::filesystem::remove_all(temp);
}
