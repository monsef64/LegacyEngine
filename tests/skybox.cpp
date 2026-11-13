#include "project/project_io.hpp"
#include "editor/editor_history.hpp"
#include "editor/level_objects.hpp"
#include "editor/play_mode.hpp"
#include "scripting/lua_console.hpp"
#include "scripting/script_exports.hpp"
#include <cassert>
#include "scene/skybox_transition.hpp"
int main(){
    {
        World fadeWorld;auto a=fadeWorld.create(),b=fadeWorld.create();Skybox day,night;day.color={1,1,1};night.color={.1f,.2f,.3f};night.enabled=false;
        fadeWorld.add(a,day);fadeWorld.add(b,night);fadeWorld.add(a,ObjectName{"Skybox1"});fadeWorld.add(b,ObjectName{"Skybox2"});
        LuaConsole fadeConsole;fadeConsole.bindWorld(fadeWorld);assert(fadeConsole.bindObject(a));
        assert(!fadeConsole.execute("fadeSkybox(self,GameObject.find('Skybox2'),0)"));
        assert(fadeConsole.execute("fadeSkybox(GameObject.find('Skybox1'),GameObject.find('Skybox2'),2)"));
        updateSkyboxTransitions(fadeWorld,.25f);assert(fadeWorld.get<SkyboxTransition>(a).weight()==.5f);
        assert(fadeWorld.get<Skybox>(a).enabled && !fadeWorld.get<Skybox>(b).enabled);
        assert(beginSkyboxFade(fadeWorld,a,b,1)!=nullptr);
        updateSkyboxTransitions(fadeWorld,.25f);
        assert(!fadeWorld.has<SkyboxTransition>(a));assert(!fadeWorld.get<Skybox>(a).enabled && fadeWorld.get<Skybox>(b).enabled);
        assert(fadeWorld.get<Skybox>(a).color.x==1 && fadeWorld.get<Skybox>(b).color.x==.1f);
        assert(fadeConsole.execute("fadeSkybox(GameObject.find('Skybox1'),1)"));
        assert(fadeWorld.has<SkyboxTransition>(b));updateSkyboxTransitions(fadeWorld,1);
        assert(fadeWorld.get<Skybox>(a).enabled && !fadeWorld.get<Skybox>(b).enabled);
        assert(fadeConsole.execute("self:getComponent(Skybox):fadeTo(GameObject.find('Skybox2'):getComponent(Skybox),0.5)"));
        fadeWorld.destroy(b);updateSkyboxTransitions(fadeWorld,1);assert(!fadeWorld.has<SkyboxTransition>(a) && fadeWorld.get<Skybox>(a).enabled);
    }

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
    {
        World w;auto e=w.create();w.add(e,Transform{});w.add(e,Skybox{});LuaConsole c;c.bindWorld(w);assert(c.bindObject(e));
        assert(c.execute("getComponent(Skybox):setCubemap('Sky/Evening.cubemap')"));
        assert(w.get<Skybox>(e).cubemap=="Sky/Evening.cubemap" && w.get<Skybox>(e).type==0);
        assert(c.execute("getComponent(Skybox):setHDR('Sky/Studio.hdr')"));assert(w.get<Skybox>(e).type==1);
        assert(!c.execute("getComponent(Skybox):setCubemap('../outside.cubemap')"));
    }

}
