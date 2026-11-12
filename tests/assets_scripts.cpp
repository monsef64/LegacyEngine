#include "editor/level_objects.hpp"
#include "editor/asset_browser.hpp"
#include "scripting/script_components.hpp"
#include "project/project_io.hpp"
#include "editor/editor_history.hpp"
#include "editor/play_mode.hpp"
#include "editor/dock_workspace.hpp"
#include <cassert>

int main() {
    const auto temp=std::filesystem::temp_directory_path()/
        ("legacy-assets-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temp/"external");
    std::filesystem::create_directories(temp/"second");
    {std::ofstream out(temp/"external"/"Move.lua");out<<"function onStart() self.getComponent(Transform).position.x=1 end\nfunction onUpdate(dt) self.getComponent(Transform).position.x=self.getComponent(Transform).position.x+dt end\nfunction onFixedUpdate(dt) self.getComponent(Transform).position.z=dt end\nfunction onDestroy() self.getComponent(Transform).position.y=7 end\n";}
    {std::ofstream out(temp/"second"/"Move.lua");out<<"print('second')";}
    AssetManager assets;assets.root=temp/"Assets";
    assert(assets.import(temp/"external"/"Move.lua")=="Move.lua");
    assert(assets.import(temp/"second"/"Move.lua")=="Move (2).lua");
    assert(assets.files().size()==2);
    bool rejected=false;try{assets.resolve("../outside.lua");}catch(...){rejected=true;}assert(rejected);
    rejected=false;try{assets.resolve((temp/"external"/"Move.lua").string());}catch(...){rejected=true;}assert(rejected);
    assets.copyTo(temp/"OtherAssets");assert(std::filesystem::exists(temp/"OtherAssets"/"Move.lua"));
    {std::ofstream out(temp/"OtherAssets"/"Move.lua");out<<"conflict";}
    rejected=false;try{assets.copyTo(temp/"OtherAssets");}catch(...){rejected=true;}assert(rejected);
    World world;Entity one=world.create(),two=world.create();
    world.add(one,Transform{});world.add(two,Transform{{100,0,0}});
    assert(attachScriptAsset(world,one,"Move.lua"));assert(attachScriptAsset(world,two,"Move.lua"));
    assert(!attachScriptAsset(world,one,"image.png"));assert(!attachScriptAsset(world,one,"../Move.lua"));
    const MeshHandle meshes[]{MeshHandle{1},MeshHandle{2},MeshHandle{3},MeshHandle{4}};
    PlayMode play;play.play(world);ScriptComponents runtime;runtime.start(world,assets,meshes);
    assert(world.get<Transform>(one).position.x==1 && world.get<Transform>(two).position.x==1);
    runtime.update(.25f);runtime.fixedUpdate(.5f);
    assert(world.get<Transform>(one).position.x==1.25f && world.get<Transform>(two).position.x==1.25f);
    assert(world.get<Transform>(one).position.z==.5f);
    world.get<LuaScript>(one).enabled=false;runtime.update(.25f);
    assert(world.get<Transform>(one).position.x==1.25f && world.get<Transform>(one).position.y==7);
    assert(world.get<Transform>(two).position.x==1.5f);
    runtime.stop();play.stop(world);
    assert(world.get<Transform>(two).position.x==100 && world.get<Transform>(one).position.y==0);
    auto copy=duplicateLevelObject(world,one);assert(world.get<LuaScript>(copy).path=="Move.lua");
    EditorHistory history(world);world.remove<LuaScript>(one);history.commit(world,one);
    Entity selected;history.undo(world,selected);assert(world.has<LuaScript>(one));
    const ProjectIO::Meshes catalog{MeshHandle{1},MeshHandle{2},MeshHandle{3},MeshHandle{4},MeshHandle{5},MeshHandle{6}};
    ProjectIO::Loaded loaded;ProjectIO::decode(ProjectIO::encode(world,"",OrbitCamera{},catalog),loaded,catalog);
    assert(loaded.world.get<LuaScript>(one).path=="Move.lua");
    ProjectIO::Loaded legacy;ProjectIO::decode("LegacyEngineProject 1\n\"\"\n0 0 0 0 0 10 1\n0\n",legacy,catalog);
    world.get<LuaScript>(one).path="missing.lua";runtime.start(world,assets,meshes);
    bool missing=false;for(const auto& message:runtime.messages())if(message.find("missing.lua")!=std::string::npos)missing=true;
    assert(missing);runtime.stop();
    // Exercise docking, reset/restore, close visibility, and file icon geometry.
    ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
    io.ConfigFlags|=ImGuiConfigFlags_DockingEnable;
    io.DisplaySize=ImVec2(1280,720);io.DeltaTime=1.0f/60;io.Fonts->Build();dockedEditor=true;
    for(int frame=0;frame<3;++frame) {
        ImGui::NewFrame();bool reset=frame==2;
        if(frame==2)panelOpen[0]=false;
        drawWorkspace(EditorRect{0,20,1280,38},reset);
        if(frame==2)assert(panelOpen[0]);
        if(beginEditorPanel("Assets")) {
            fileIcon("code.lua");fileIcon("image.png");fileIcon("model.glb");fileIcon("sound.wav");fileIcon("notes.txt");
        }
        ImGui::End();ImGui::Render();
    }
    ImGui::DestroyContext();dockedEditor=false;
    std::filesystem::remove_all(temp);
}
