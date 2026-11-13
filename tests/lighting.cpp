#include "lighting/lighting.hpp"
#include "lighting/cascades.hpp"
#include "lighting/shadow_settings.hpp"
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
    Lighting::Frustum frustum(vp);
    for(int x=-30;x<=30;++x)for(int y=-20;y<=20;++y)for(int z=-10;z<10;++z){
        Vec3 center{float(x)+.137f,float(y)+.219f,float(z)+.347f};
        assert(frustum.visible(center,.673f)==Lighting::visibleSphere(vp,center,.673f));
    }
    ShadowSettings settings;
    assert(settings.valid() && settings.mode==0 && settings.lightResolution(3)==1024);
    assert(settings.lightResolution(2)==512 && settings.lightResolution(1)==256 && settings.lightResolution(0)==0);
    for(int resolution:{256,512,1024,2048}) {
        settings.resolution=resolution;assert(settings.valid());
        for(bool ortho:{false,true}) {
            Transform cameraPose{{0,0,5}};Camera camera;camera.orthographic=ortho;camera.farClip=100;
            const auto cameraVP=cameraMatrix(cameraPose,camera);
            const auto cascades=Lighting::buildCascades(cameraVP,cameraPose.position,{0,-1,0},60,resolution);
            const auto inverse=inverseCameraMatrix(cameraVP);
            const auto far=projectCameraPoint(inverse,{0,0,1});
            const float range=dot(far-cascades.origin,cascades.forward);
            float previous=0;
            for(int layer=0;layer<4;++layer) {
                const float end=cascades.splits[layer];assert(end>previous && std::isfinite(end));
                for(float depth:{layer==0?0.f:previous*.9f,end})for(float x:{-1.f,1.f})for(float y:{-1.f,1.f}) {
                    const auto a=projectCameraPoint(inverse,{x,y,-1}),b=projectCameraPoint(inverse,{x,y,1});
                    const auto clip=projectCameraPoint(cascades.matrices[layer],a+(b-a)*(depth/range));
                    assert(std::abs(clip.x)<=1.001f && std::abs(clip.y)<=1.001f && std::abs(clip.z)<=1.001f);
                }
                previous=end;
            }
        }
    }
    std::vector<Lighting::Light> cascadedLights;Lighting::gather(world,vp,{0,0,5},cascadedLights,true);
    int cascadeLayers=0;for(const auto& light:cascadedLights)if(light.shadowLayer>=0){cascadeLayers+=light.shadowCount;assert(light.shadowLayer+light.shadowCount<=Lighting::maxShadowLayers);if(light.type==0)assert(light.shadowCount==4);}
    assert(cascadeLayers<=Lighting::maxShadowLayers);
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
    // Repeated viewport collection reuses capacity and never carries stale lights/layers.
    std::vector<Lighting::Light> scratch;
    Lighting::gather(world,vp,{0,0,5},scratch);
    const auto* storage=scratch.data();const auto capacity=scratch.capacity();
    for(int frame=0;frame<10;++frame){
        Lighting::gather(world,vp,{0,0,5},scratch);
        assert(scratch.size()==lights.size() && scratch.data()==storage && scratch.capacity()==capacity);
        for(std::size_t i=0;i<lights.size();++i){
            assert(scratch[i].entity==lights[i].entity && scratch[i].shadowLayer==lights[i].shadowLayer);
        }
    }
    World empty;Lighting::gather(empty,vp,{0,0,5},scratch);assert(scratch.empty() && scratch.capacity()==capacity);
    Lighting::gather(world,vp,{0,0,5},scratch);assert(scratch.size()==Lighting::maxLights);
    auto invalid=PointLight{};invalid.intensity=std::numeric_limits<float>::quiet_NaN();assert(!validLight(invalid));
}
