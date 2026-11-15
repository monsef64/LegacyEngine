#include "levels/level_system.hpp"
#include "scripting/lua_console.hpp"
#include "rendering/shader_data.hpp"
#include "project/rendering_settings.hpp"
#include <cassert>
#include <thread>
int main(){
    auto root=std::filesystem::temp_directory_path()/("legacy-levels-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    for(const char* file:{"vulkan/lit.vert","vulkan/lit.frag","vulkan/unlit.vert","vulkan/unlit.frag","vulkan/skybox.vert","vulkan/skybox.frag","vulkan/shadow.vert","vulkan/skinning.comp"})assert(loadShaderData(file).find("#version 450")!=std::string::npos);
    AssetManager assets;assets.root=root/"Assets";RenderingSettings settings;settings.vsync=false;settings.save(assets);
    assert(RenderingSettings::file(assets)==root/"Settings/Rendering.json");assert(readJsonFile(RenderingSettings::file(assets),4096)["vsync"]==false);
    settings.shadows={1,2048};settings.fsr3GameQuality=3;settings.save(assets);
    RenderingSettings loaded;bool vsyncApplied=true;int applications=0;
    loaded.applyVsync=[&](bool enabled){vsyncApplied=enabled;++applications;};
    loaded.load(assets);assert(loaded.shadows.mode==1 && loaded.shadows.resolution==2048 && loaded.fsr3GameQuality==3);
    assert(applications==1 && !vsyncApplied && loaded.error.empty());
    loaded.applyVsync=[](bool){throw std::runtime_error("presentation failure");};
    loaded.apply();assert(loaded.error.find("presentation failure")!=std::string::npos);
    loaded.applyVsync=[&](bool enabled){vsyncApplied=enabled;++applications;};
    writeJsonFile(RenderingSettings::file(assets),Json{{"version",1},{"vsync",true}});
    loaded.root.clear();loaded.load(assets);assert(loaded.shadows.mode==0 && loaded.shadows.resolution==1024 && loaded.fsr3GameQuality==0);
    writeJsonFile(RenderingSettings::file(assets),Json{{"version",2},{"vsync",true},{"shadowMode",0},{"shadowResolution",999}});
    loaded.root.clear();loaded.load(assets);assert(!loaded.error.empty() && loaded.shadows.valid());
    ProjectIO::Meshes meshes{};LevelSystem levels(meshes);levels.setRoot(root);LevelSystem::active=&levels;
    World world;auto entity=world.create();world.add(entity,Transform{});
    levels.saveCurrent(world,"saved=42",OrbitCamera{});assert(levels.current()=="Main");
    levels.create("Second");assert(levels.names().size()==2);
    bool rejected=false;try{levels.create("../escape");}catch(...){rejected=true;}assert(rejected);
    assert(!levels.load("missing"));assert(levels.current()=="Main");
    auto failure=levels.takeFailure();assert(failure && failure->name=="missing" && !failure->error.empty());assert(!levels.takeFailure());
    RenderingSettings::active=&loaded;
    LuaConsole console;
    assert(console.execute("assert(Rendering.getGameFsrQuality()==0); Rendering.setGameFsrQuality(2); assert(Rendering.getGameFsrQuality()==2)"));
    assert(readJsonFile(RenderingSettings::file(assets),4096)["fsr3GameQuality"]==2);
    assert(!console.execute("Rendering.setGameFsrQuality(6)"));
    assert(loaded.fsr3GameQuality==2);
    assert(console.execute("assert(getCurrentLevel()=='Main');assert(nextLevel());assert(getCurrentLevel()=='Main');assert(not previousLevel())"));
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
