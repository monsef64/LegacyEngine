#include "project/project_io.hpp"
#include "editor/editor_history.hpp"
#include "editor/level_objects.hpp"
#include "editor/play_mode.hpp"
#include "scripting/lua_console.hpp"
#include "scripting/script_exports.hpp"
#include <cassert>
int main(){
    World world;auto e=world.create();world.add(e,ObjectName{"Sky"});world.add(e,Skybox{});
    LuaConsole console;console.bindWorld(world);assert(console.bindObject(e));
    assert(console.execute(R"(
        local sky=self:getComponent(Skybox)
        sky:setType('Textures');sky:setColor(0.5,0.6,0.7);sky:setEnabled(true)
        sky:setTextures('right.png','left.png','top.png','bottom.png','front.png','back.png')
        sky:setFace('right','new.png');assert(sky.right=='new.png')
        assert(#findGameObjectsByComponent(Skybox)==1)
        sky:setType('HDR');sky:setHDR('Environment/studio.hdr');assert(sky.type==1)
        sky.hdr='Environment/new.hdr';assert(sky.hdr=='Environment/new.hdr')
        local other=GameObject.new('Other');other:addComponent(Skybox):setEnabled(false)
    )"));
    assert(world.get<Skybox>(e).color.y==.6f && world.get<Skybox>(e).hdr=="Environment/new.hdr");
    assert(!console.execute("self:getComponent(Skybox):setHDR('../escape.hdr')"));
    assert(!console.execute("self:getComponent(Skybox):setFace('bad','right.png')"));
    assert(!console.execute("self:getComponent(Skybox):setFace('right','file.hdr')"));
    assert(!console.execute("self:getComponent(Skybox).type=3"));
    assert(!console.execute("self:getComponent(Skybox).hdr='image.png'"));
    assert(!console.execute("self:getComponent(Skybox):setColor(-1,1,1)"));
    auto signature=sceneSignature(world);world.get<Skybox>(e).color.x=.2f;assert(sceneSignature(world)!=signature);
    ProjectIO::Meshes meshes{};ProjectIO::Loaded loaded;auto data=ProjectIO::encode(world,"",OrbitCamera{},meshes);ProjectIO::decode(data,loaded,meshes);
    assert(loaded.world.get<Skybox>(e).type==1 && loaded.world.get<Skybox>(e).front=="front.png");
    auto dup=duplicateLevelObject(world,e);assert(world.get<Skybox>(dup).hdr==world.get<Skybox>(e).hdr);world.destroy(dup);
    PlayMode play;play.play(world);world.get<Skybox>(e).enabled=false;play.stop(world);assert(world.get<Skybox>(e).enabled);
    std::vector<ScriptField> fields;syncScriptExports("@export panorama Texture \"studio.hdr\"\n@export sky Skybox",fields);assert(fields.size()==2 && fields[0].text=="studio.hdr");fields[1].entity=e;
    assert(exportHasComponent(world,e,"Skybox"));assert(console.bindExports(fields));assert(console.execute("sky:setHDR(panorama);assert(sky.hdr=='studio.hdr')"));
    assert(console.execute("stale=self:getComponent(Skybox);self:removeComponent(Skybox)"));assert(!console.execute("stale:setEnabled(true)"));
}
