#include "scripting/lua_console.hpp"
#include "editor/asset_browser.hpp"
#include "project/project_assets.hpp"
#include "project/project_io.hpp"
#include <cassert>
#include <cstdio>

int main() {
    World world;LuaConsole lua;lua.bindWorld(world);
    auto run=[&](const char* source){const bool ok=lua.execute(source);if(!ok)for(const auto& line:lua.lines())std::fprintf(stderr,"%s\n",line.text.c_str());assert(ok);};
    run(R"(
        player=GameObject.new('Player')
        player:setPosition(1,2,3)
        assert(player:getPosition().x==1 and player:getPosition().z==3)
        player.setPosition(2,3,4)
        player:translate(1,2,3)
        assert(player:getPosition().x==3 and player:getPosition().y==5)
        player:setPosition({x=0,y=1,z=0})
        player:setScale(2,2,2)
        assert(player:getScale().x==2)
        player:setRotation(0,90,0)
        assert(player:getRotation().w~=nil)
        player:setName('Hero')
        assert(GameObject.find('Hero')==player and player:getName()=='Hero')
        parent=GameObject.new('Group')
        player:setParent(parent)
        assert(player:getParent()==parent)
        player:setParent(nil)
        assert(player:getParent()==nil)
        cam=Camera.new('Main Camera')
        cam:setPosition(0,3,8)
        cam:lookAt(player)
        cam.lookAt(player)
        cam:setActive()
        assert(Camera.getActive()==cam)
        assert(cam:getComponent(Camera)~=nil)
        cam:setFieldOfView(70)
        cam:setNearClipPlane(0.1)
        cam:setFarClipPlane(500)
        cam:setProjectionMode('perspective')
        cam:setViewport(0,0,1,1)
        cam:setRotation(0,0,0,1)
        cam:setTarget(player)
        cam:setFollowTarget(player,0,3,8)
        cam:setShake(0.1,1)
        cam:setTarget(nil)
        cam:setShake(0,0)
        local screen=cam:worldToScreen(0,1,0)
        local point=cam:screenToWorld(screen.x,screen.y,screen.z)
        assert(math.abs(point.y-1)<0.01)
        cam:setActive(false)
        assert(Camera.getActive()==nil)
        cam:setActive(true)
        setCameraPosition(cam,0,3,8)
        setCameraRotation(cam,0,0,0)
        lookAt(cam,0,1,0)
        assert(cam:getPosition().z==8)
        oldGetter=cam.getPosition
    )");
    assert(!lua.execute("player:setPosition(1,2)"));
    assert(!lua.execute("player:setPosition(1,2,math.huge)"));
    assert(!lua.execute("player.setParent(player)"));
    assert(!lua.execute("cam:setRotation(0,0,0,0)"));
    run("cam:destroy(); assert(Camera.getActive()==nil)");
    assert(!lua.execute("oldGetter()"));
    lua.shutdown();
    const auto temp=std::filesystem::temp_directory_path()/
        ("legacy-owned-assets-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temp);
    const auto first=temp/"First"/"First.legacy",second=temp/"Second"/"Second.legacy";
    assert(projectAssetsPath(first)!=projectAssetsPath(second));
    std::filesystem::path staging;
    {
        AssetBrowser browser;staging=browser.assets.root;
        assert(staging.parent_path().parent_path()==std::filesystem::temp_directory_path());
        assert(staging!=std::filesystem::current_path()/"Assets");
        browser.assets.writeScript("Move.lua","print('first')");
        browser.assets.copyTo(projectAssetsPath(first));
        AssetManager other;other.root=projectAssetsPath(second);other.writeScript("Move.lua","print('second')");
        assert(other.readScript("Move.lua")=="print('second')");
        assert(browser.assets.readScript("Move.lua")=="print('first')");
    }
    assert(!std::filesystem::exists(staging));
    AssetManager saved;saved.root=projectAssetsPath(first);
    assert(saved.readScript("Move.lua")=="print('first')");
    const ProjectIO::Meshes meshes{MeshHandle{1},MeshHandle{2},MeshHandle{3},MeshHandle{4},MeshHandle{5},MeshHandle{6}};
    World project;OrbitCamera camera;std::string error,source,directory;
    assert(ProjectIO::save(first.string(),project,"",camera,meshes,error));
    auto renamed=first.parent_path()/"Renamed.legacy";std::filesystem::rename(first,renamed);
    assert(ProjectIO::load(renamed.string(),project,source,camera,meshes,error,&directory));
    assert(directory=="Assets");
    assert(std::filesystem::exists(renamed.parent_path()/directory/"Move.lua"));
    // Legacy projects still locate their original sibling Assets directory.
    std::filesystem::create_directories(temp/"Assets");
    assert(existingProjectAssetsPath(temp/"Legacy.legacy")==temp/"Assets");
    assert(existingProjectAssetsPath(second)==projectAssetsPath(second));
    // Never accept an asset-directory reference outside the project folder.
    auto invalid=ProjectIO::encode(project,"",camera,meshes,"../EngineAssets");
    ProjectIO::Loaded loaded;bool rejected=false;try{ProjectIO::decode(invalid,loaded,meshes);}catch(...){rejected=true;}assert(rejected);
    std::filesystem::remove_all(temp);
}
