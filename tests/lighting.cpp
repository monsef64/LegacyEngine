#include "lighting/lighting.hpp"
#include "project/project_io.hpp"
#include "editor/level_objects.hpp"
#include "editor/editor_history.hpp"
#include "editor/play_mode.hpp"
#include "scripting/lua_console.hpp"
#include <cassert>
#include <limits>

int main(){
    World world;const auto e=world.create();world.add(e,Transform{});world.add(e,DirectionalLight{});
    auto point=world.create();world.add(point,Transform{{0,0,-3}});PointLight p;p.color={1,.2f,.1f};p.range=12;world.add(point,p);
    auto spot=world.create();world.add(spot,Transform{{0,2,-2}});world.add(spot,SpotLight{});
    const Mat4 vp=multiply(perspective(kPi/2,1,.1f,100),lookAt({0,0,5},{0,0,0},{0,1,0}));
    auto lights=Lighting::gather(world,vp,{0,0,5});assert(lights.size()==3);
    int layers=0;for(const auto& l:lights){assert(l.shadowLayer>=0);layers+=l.type==1?6:1;
        for(int face=0;face<(l.type==1?6:1);++face){const auto m=Lighting::shadowMatrix(l,{0,0,5},face);for(float f:m.m)assert(std::isfinite(f));}}
    assert(layers==8);
    assert(Lighting::visibleSphere(vp,{0,0,0},1));assert(!Lighting::visibleSphere(vp,{1000,0,0},1));
    world.get<PointLight>(point).enabled=false;assert(Lighting::gather(world,vp,{0,0,5}).size()==2);world.get<PointLight>(point).enabled=true;
    const auto signature=sceneSignature(world);world.get<PointLight>(point).intensity=2;assert(sceneSignature(world)!=signature);
    const auto duplicate=duplicateLevelObject(world,point);assert(world.has<PointLight>(duplicate));world.destroy(duplicate);
    PlayMode play;play.play(world);world.get<PointLight>(point).range=99;play.stop(world);assert(world.get<PointLight>(point).range==12);
    ProjectIO::Meshes meshes{};ProjectIO::Loaded loaded;
    world.get<PointLight>(point).shadowQuality=1;
    const auto json=ProjectIO::encode(world,"",OrbitCamera{},meshes);ProjectIO::decode(json,loaded,meshes);
    assert(loaded.world.has<DirectionalLight>(e) && loaded.world.has<SpotLight>(spot));
    assert(loaded.world.get<PointLight>(point).color.y==.2f && loaded.world.get<PointLight>(point).range==12);
    assert(loaded.world.get<PointLight>(point).shadowQuality==1);
    auto older=parseJson(json);older["objects"][1]["components"]["PointLight"].erase("shadowQuality");ProjectIO::Loaded oldLoaded;ProjectIO::decode(older.dump(),oldLoaded,meshes);assert(oldLoaded.world.get<PointLight>(point).shadowQuality==3);
    assert(lightShadowResolution(world.get<PointLight>(point))==256);
    world.get<PointLight>(point).shadowQuality=0;for(const auto& l:Lighting::gather(world,vp,{0,0,5}))if(l.entity==point)assert(l.shadowLayer==-1);world.get<PointLight>(point).shadowQuality=3;
    auto malformed=parseJson(json);malformed["objects"][1]["components"]["PointLight"]["outerAngle"]=100;
    bool rejected=false;try{ProjectIO::Loaded bad;ProjectIO::decode(malformed.dump(),bad,meshes);}catch(...){rejected=true;}assert(rejected);
    LuaConsole console;console.bindWorld(world);console.bindObject(point);
    assert(console.execute("local l=self:getComponent(PointLight); l.intensity=3; l.color.y=0.5; l.shadows=false; self:setPosition(1,2,3)"));
    assert(world.get<PointLight>(point).intensity==3 && world.get<PointLight>(point).color.y==.5f && !world.get<PointLight>(point).shadows);
    assert(console.execute("local o=GameObject.new('Lamp'); o:addComponent(SpotLight).range=15; o:addComponent(DirectionalLight).enabled=false; assert(#findGameObjectsByComponent(SpotLight)==2)"));
    assert(console.execute("local l=self:getComponent(PointLight); l:setColor(0.2,0.4,1); l:setIntensity(5); l:setRange(30); l:setEnabled(true); l:setShadows(true); l:setShadowQuality(2); l:setSpotAngles(15,35); assert(l.shadowQuality==2)"));
    assert(world.get<PointLight>(point).range==30 && world.get<PointLight>(point).shadowQuality==2);
    assert(!console.execute("self:getComponent(PointLight):setShadowQuality(4)"));assert(world.get<PointLight>(point).shadowQuality==2);
    // Budget stays bounded even when many visible dynamic lights exist.
    for(int i=0;i<40;++i){auto id=world.create();world.add(id,Transform{{0,0,-1}});world.add(id,PointLight{});}
    lights=Lighting::gather(world,vp,{0,0,5});assert(lights.size()==Lighting::maxLights);
    layers=0;for(const auto& l:lights)if(l.shadowLayer>=0){const int n=l.type==1?6:1;assert(l.shadowLayer+n<=Lighting::maxShadowLayers);layers+=n;}assert(layers<=Lighting::maxShadowLayers);
    auto invalid=PointLight{};invalid.intensity=std::numeric_limits<float>::quiet_NaN();assert(!validLight(invalid));
}
