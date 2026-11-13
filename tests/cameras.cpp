#include "scene/scene_camera.hpp"
#include "scene/camera_gizmo.hpp"
#include "project/project_io.hpp"
#include "editor/editor_history.hpp"
#include "scripting/lua_console.hpp"
#include "editor/play_mode.hpp"
#include "editor/level_objects.hpp"
#include "rendering/mesh.hpp"
#include <cassert>
#include <cstdio>

static bool close(float a,float b,float tolerance=.002f){return std::abs(a-b)<tolerance;}
int main() {
    OrbitCamera sceneCamera;const float initialYaw=sceneCamera.yaw,initialPitch=sceneCamera.pitch;
    sceneCamera.focus({20,4,-7},2,1.5f);
    assert(close(sceneCamera.target.x,20) && close(sceneCamera.target.y,4) && close(sceneCamera.target.z,-7));
    assert(sceneCamera.yaw==initialYaw && sceneCamera.pitch==initialPitch && sceneCamera.distance>2);
    const float wideDistance=sceneCamera.distance;sceneCamera.focus({20,4,-7},2,.5f);assert(sceneCamera.distance>wideDistance);
    sceneCamera.focus({0,0,0},10000,1);assert(sceneCamera.distance==150);
    sceneCamera.focus({0,0,0},0,1);assert(sceneCamera.distance>=2);

    Camera gizmo;gizmo.nearClip=.2f;gizmo.farClip=20;gizmo.aspect=2;
    auto wire=cameraFrustumWire(gizmo);assert(wire.topology==MeshData::Topology::Lines && wire.vertices.size()==24);
    float maxX=0,maxY=0;for(const auto& vertex:wire.vertices){maxX=std::max(maxX,std::abs(vertex.position.x));maxY=std::max(maxY,std::abs(vertex.position.y));assert(vertex.position.z<=0);}
    assert(close(maxX/maxY,2));gizmo.fieldOfView=30;float narrow=std::abs(cameraFrustumWire(gizmo).vertices[8].position.x);assert(narrow<maxX);
    gizmo.orthographic=true;gizmo.orthographicSize=4;wire=cameraFrustumWire(gizmo);assert(close(std::abs(wire.vertices[0].position.y),2));
    Transform transform;Camera camera;camera.aspect=1.5f;
    aimCamera(transform,{2,3,-8});
    auto front=rotateVector(transform.rotation,{0,0,-1});
    assert(dot(front,normalize(Vec3{2,3,-8}))>.999f);
    transform.position={2,1,4};aimCamera(transform,{0,0,0});
    for(bool ortho:{false,true}) {
        camera.orthographic=ortho;camera.viewportX=.1f;camera.viewportY=.2f;camera.viewportWidth=.6f;camera.viewportHeight=.7f;
        Vec3 point{0,0,0};auto screen=cameraWorldToScreen(transform,camera,point);
        assert(close(screen.x,.4f) && close(screen.y,.55f));
        auto restored=cameraScreenToWorld(transform,camera,screen);
        assert(close(restored.x,point.x) && close(restored.y,point.y) && close(restored.z,point.z));
        auto center=cameraScreenToWorld(transform,camera,{.4f,.55f,0});
        assert(close(dot(center-transform.position,rotateVector(transform.rotation,{0,0,-1})),camera.nearClip));
    }
    World world;Entity target=world.create(),entity=world.create();world.add(target,Transform{{1,2,3}});world.add(entity,Transform{});
    Camera follow;follow.targetEntity=target;follow.tracking=true;follow.following=true;follow.followOffset={0,3,5};world.add(entity,follow);
    activateCamera(world,entity);assert(activeCamera(world)==entity);
    updateCameras(world,.1f);assert((world.get<Transform>(entity).position==Vec3{1,5,8}));
    world.get<Camera>(entity).shakeIntensity=.2f;world.get<Camera>(entity).shakeRemaining=1;
    auto stable=world.get<Transform>(entity).position;updateCameras(world,.25f);
    assert(world.get<Transform>(entity).position==stable);assert(world.get<Camera>(entity).shakeRemaining==.75f);
    auto model=makeCameraModel();assert(!model.vertices.empty());
    const ProjectIO::Meshes meshes{MeshHandle{1},MeshHandle{2},MeshHandle{3},MeshHandle{4},MeshHandle{5},MeshHandle{6}};
    ProjectIO::Loaded loaded;ProjectIO::decode(ProjectIO::encode(world,"",OrbitCamera{},meshes),loaded,meshes);
    assert(activeCamera(loaded.world)!=Entity{});assert(loaded.world.get<Camera>(entity).targetEntity==target);
    auto copy=duplicateLevelObject(world,entity);assert(!world.get<Camera>(copy).active);
    EditorHistory history(world);world.get<Camera>(entity).fieldOfView=90;history.commit(world,entity);
    Entity selection;assert(history.undo(world,selection));assert(world.get<Camera>(entity).fieldOfView==60);
    PlayMode play;play.play(world);world.get<Camera>(entity).farClip=99;play.stop(world);assert(world.get<Camera>(entity).farClip==1000);
    world.destroy(target);updateCameras(world,.1f);assert(!world.get<Camera>(entity).tracking);
    LuaConsole lua;World luaWorld;lua.bindWorld(luaWorld);
    auto execute=[&](const char* source){bool ok=lua.execute(source);if(!ok)for(auto& line:lua.lines())std::fprintf(stderr,"%s\n",line.text.c_str());assert(ok);};
    execute(R"(
        cam=createCamera('Main Camera')
        assert(getActiveCamera()==cam)
        assert(cam.getComponent(Camera)~=nil)
        setCameraPosition(cam,{x=0,y=0,z=5})
        setCameraRotation(cam,{x=0,y=0,z=0})
        lookAt(cam,{x=0,y=0,z=0})
        setFieldOfView(cam,70)
        setNearClipPlane(cam,0.2)
        setFarClipPlane(cam,500)
        setViewport(cam,0.1,0.1,0.8,0.8)
        for _,mode in ipairs({'Perspective','Orthographic'}) do
            setProjectionMode(cam,mode)
            local screen=worldToScreen(cam,{x=0,y=0,z=0})
            assert(math.abs(screen.x-0.5)<0.001 and math.abs(screen.y-0.5)<0.001)
            local point=screenToWorld(cam,screen)
            assert(math.abs(point.z)<0.01)
        end
        setCameraRotation(cam,{x=0,y=0,z=0,w=1})
        actor=createGameObject('Actor')
        setCameraTarget(cam,actor)
        setFollowTarget(cam,actor,{x=0,y=2,z=5})
        setCameraShake(cam,0.1,1)
        second=createCamera('Second')
        setActiveCamera(second)
        assert(getActiveCamera()==second)
        cam.getComponent(Camera).active=true
        assert(getActiveCamera()==cam)
        setActiveCamera(nil)
        assert(getActiveCamera()==nil)
        setActiveCamera(cam)
    )");
    lua.update(.1f);updateCameras(luaWorld,.1f);
    assert(!lua.execute("setFieldOfView(cam,180)"));
    assert(!lua.execute("setNearClipPlane(cam,9999)"));
    assert(!lua.execute("setViewport(cam,0,0,2,1)"));
    assert(!lua.execute("setCameraShake(cam,-1,1)"));
    assert(!lua.execute("setCameraPosition(actor,{x=0,y=0,z=0})"));
    execute("destroyGameObject(cam); assert(getActiveCamera()==nil)");
    assert(!lua.execute("setFieldOfView(cam,50)"));
    lua.shutdown();
}
