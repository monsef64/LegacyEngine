#include "levels/level_system.hpp"
#include "scripting/lua_console.hpp"
#include "rendering/shader_data.hpp"
#include "project/rendering_settings.hpp"
#include <cassert>
#include <thread>
int main(){
    auto root=std::filesystem::temp_directory_path()/("legacy-levels-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    for(const char* file:{"lit.vert","lit.frag","unlit.vert","unlit.frag","skybox.vert","skybox.frag"})assert(loadShaderData(file).find("#version 330 core")!=std::string::npos);
    AssetManager assets;assets.root=root/"Assets";RenderingSettings settings;settings.vsync=false;settings.save(assets);
    assert(RenderingSettings::file(assets)==root/"Settings/Rendering.json");assert(readJsonFile(RenderingSettings::file(assets),4096)["vsync"]==false);
    settings.shadows={1,2048};settings.save(assets);
    RenderingSettings loaded;loaded.load(assets);assert(loaded.shadows.mode==1 && loaded.shadows.resolution==2048);
    writeJsonFile(RenderingSettings::file(assets),Json{{"version",1},{"vsync",true}});
    loaded.root.clear();loaded.load(assets);assert(loaded.shadows.mode==0 && loaded.shadows.resolution==1024);
    writeJsonFile(RenderingSettings::file(assets),Json{{"version",2},{"vsync",true},{"shadowMode",0},{"shadowResolution",999}});
    loaded.root.clear();loaded.load(assets);assert(!loaded.error.empty() && loaded.shadows.valid());
    ProjectIO::Meshes meshes{};LevelSystem levels(meshes);levels.setRoot(root);LevelSystem::active=&levels;
    World world;auto entity=world.create();world.add(entity,Transform{});
    levels.saveCurrent(world,"saved=42",OrbitCamera{});assert(levels.current()=="Main");
    levels.create("Second");assert(levels.names().size()==2);
    bool rejected=false;try{levels.create("../escape");}catch(...){rejected=true;}assert(rejected);
    assert(!levels.load("missing"));assert(levels.current()=="Main");
    auto failure=levels.takeFailure();assert(failure && failure->name=="missing" && !failure->error.empty());assert(!levels.takeFailure());
    LuaConsole console;assert(console.execute("assert(getCurrentLevel()=='Main');assert(nextLevel());assert(getCurrentLevel()=='Main');assert(not previousLevel())"));
    auto transition=levels.poll();assert(transition && transition->name=="Second" && transition->level->world.entities().empty());
    levels.commit(transition->name,true);
    assert(console.execute("assert(not nextLevel());assert(previousLevel())"));transition=levels.poll();assert(transition && transition->level->source=="saved=42");levels.commit(transition->name,true);
    assert(console.execute("assert(loadLevelAsync('Second'));assert(not loadLevel('Main'))"));
    for(int i=0;i<2000 && !(transition=levels.poll());++i)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(transition && transition->name=="Second");levels.commit(transition->name,true);
    assert(console.execute("assert(unloadLevel());assert(getCurrentLevel()=='Second')"));transition=levels.poll();assert(transition && transition->name.empty());levels.commit("",false);
    assert(console.execute("assert(getCurrentLevel()==nil);assert(setCurrentLevel('Main'))"));transition=levels.poll();assert(transition);levels.commit("Main",false);
    assert(console.execute("assert(restartLevel())"));transition=levels.poll();assert(transition && transition->level->source=="saved=42");
    {std::ofstream file(root/"Levels/Second.level");file<<"invalid";}
    assert(levels.load("Second",true));for(int i=0;i<2000 && levels.busy();++i){levels.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    assert(!levels.busy() && !levels.error.empty() && levels.current()=="Main");
    assert(levels.load("Main",true));levels.cancel();assert(!levels.poll());
    levels.suspended=true;assert(!levels.load("Main"));levels.suspended=false;
    LevelSystem reopened(meshes);reopened.setRoot(root);assert(reopened.current()=="Second" && reopened.names().size()==2);
    levels.renameCurrent("Renamed Main");assert(levels.current()=="Renamed Main");
    assert(std::filesystem::exists(root/"Levels/Renamed Main.level") && !std::filesystem::exists(root/"Levels/Main.level"));
    LevelSystem renamed(meshes);renamed.setRoot(root);assert(renamed.current()=="Renamed Main");
    rejected=false;try{levels.renameCurrent("Second");}catch(...){rejected=true;}assert(rejected && levels.current()=="Renamed Main");
    std::filesystem::remove_all(root);
}
