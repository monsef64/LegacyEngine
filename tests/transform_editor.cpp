#include "editor/transform_editor.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static void require(bool ok,const char* message)
{
    if (!ok) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); }
}

int main()
{
    // Round-trip all rotation branches, including half-turns and non-uniform scale.
    for (Vec3 axis : {Vec3{1,0,0},Vec3{0,1,0},Vec3{0,0,1},Vec3{1,2,3}}) {
        for (float angle : {0.0f,0.3f,1.8f,kPi,4.1f}) {
            const Quat q=axisAngle(axis,angle);
            const Mat4 matrix=transformMatrix({2,3,4},q,{2,3,4});
            Transform edited;
            require(applyGizmoMatrix(edited,matrix,TransformTool::Rotate,false),"rotation accepted");
            const float alignment=edited.rotation.x*q.x+edited.rotation.y*q.y+edited.rotation.z*q.z+edited.rotation.w*q.w;
            require(std::abs(alignment)>0.99999f,"matrix to quaternion round-trip");
            require(edited.position==Vec3{} && edited.scale==Vec3{1,1,1},"rotation leaves other channels intact");
        }
    }
    Transform transform;
    const Mat4 moved=transformMatrix({2,3,4},axisAngle({0,1,0},1),{2,3,4});
    require(applyGizmoMatrix(transform,moved,TransformTool::Move,false),"move accepted");
    require(transform.position==Vec3{2,3,4} && transform.scale==Vec3{1,1,1} && transform.rotation==Quat{},"move only changes position");
    require(applyGizmoMatrix(transform,moved,TransformTool::Scale,false),"box scale accepted");
    require(transform.scale==Vec3{2,3,4},"box scale is independent per axis");
    for (int axis=0;axis<3;++axis) {
        Transform round;
        Vec3 scale{1,1,1};
        if (axis==0) scale.x=2;
        if (axis==1) scale.y=2;
        if (axis==2) scale.z=2;
        require(applyGizmoMatrix(round,transformMatrix({}, {},scale),TransformTool::Scale,true),"round scale accepted");
        require(round.scale==Vec3{2,2,2},"round collider stays matched to uniform visual scale");
    }
    Mat4 bad=moved;
    bad.m[12]=NAN;
    const Transform before=transform;
    require(!applyGizmoMatrix(transform,bad,TransformTool::Move,false),"invalid matrix rejected");
    require(transform.position==before.position,"invalid edit leaves object unchanged");
    require(applyGizmoMatrix(transform,transformMatrix({}, {},{0.001f,200,1}),TransformTool::Scale,false),"scale clamps");
    require(transform.scale==Vec3{0.05f,100,1},"scale has positive usable limits");

    // CPU-only MyGUI-backed adapter integration: actual hit testing and a drag gesture.
    ui::createContext();
    auto& io=ui::io();

    io.DisplaySize=ui::Vec2(1280,720);
    io.DeltaTime=1.0f/60;

    World world;
    const Entity e=world.create();
    world.add(e,Transform{});
    world.add(e,Collider{});
    RigidBody body; body.mass=2; body.useGravity=false;
    world.add(e,body);
    OrbitCamera camera;
    TransformEditor editor;
    auto frame=[&](bool enabled=true) {
        ui::newFrame();
        ui::setNextWindowPos({0, 0});
        ui::setNextWindowSize({1280, 720});
        ui::beginWindow("Scene", nullptr,
                        ui::WindowFlags_NoDecoration | ui::WindowFlags_NoMove |
                            ui::WindowFlags_NoResize | ui::WindowFlags_NoSavedSettings);
        editor.draw(world, e, enabled, camera, 1280.0f / 720, nullptr, ui::windowDrawList());
        ui::endWindow();
        ui::render();
    };
    frame(); frame();
    // Locate one of the translation handles using the adapter hit-test result.
    ui::Vec2 handle{};
    bool found=false;
    for (int y=300;y<460 && !found;y+=5) {
        for (int x=550;x<740 && !found;x+=5) {
            ui::injectMousePos(static_cast<float>(x),static_cast<float>(y));
            frame();
            if (editor.capturesMouse()) { handle=ui::Vec2(static_cast<float>(x),static_cast<float>(y)); found=true; }
        }
    }
    require(found,"translation gizmo has draggable handles");
    ui::injectMouseButton(0,true);
    frame();
    require(editor.dragging(),"handle press begins manipulation");
    require(!world.has<RigidBody>(e),"dynamic body is held during drag");
    ui::injectMousePos(handle.x+35,handle.y+20);
    frame();
    const Vec3 position=world.get<Transform>(e).position;
    require(dot(position,position)>1e-5f,"axis drag changes object position");
    ui::injectMouseButton(0,false);
    frame();
    require(!editor.dragging() && world.has<RigidBody>(e),"release restores simulation");
    require(world.get<RigidBody>(e).mass==2 && !world.get<RigidBody>(e).useGravity,"body settings preserved");
    // Change operations through the same editor state used by the toolbar.
    for (int tool=1;tool<=2;++tool) {
        editor.setTool(static_cast<TransformTool>(tool));
        frame();
        const Transform beforeDrag=world.get<Transform>(e);
        found=false;
        for (int y=250;y<500 && !found;y+=5) {
            for (int x=500;x<800 && !found;x+=5) {
                ui::injectMousePos(static_cast<float>(x),static_cast<float>(y)); frame();
                if (editor.capturesMouse()) { handle=ui::Vec2(static_cast<float>(x),static_cast<float>(y)); found=true; }
            }
        }
        require(found,"rotation/scale handles can be hit");
        ui::injectMouseButton(0,true); frame();
        require(editor.dragging(),"rotation/scale press begins manipulation");
        ui::injectMousePos(handle.x+30,handle.y+15); frame();
        const Transform afterDrag=world.get<Transform>(e);
        if (tool==1) require(afterDrag.rotation!=beforeDrag.rotation,"rotation ring changes rotation");
        else require(afterDrag.scale!=beforeDrag.scale,"scale handle changes dimensions");
        ui::injectMouseButton(0,false); frame();
        require(world.has<RigidBody>(e),"rotation/scale release restores physics");
    }
    frame(false);
    require(!editor.capturesMouse(),"spawn mode disables transform handles");
    editor.finish(world);
    ui::destroyContext();
    std::puts("Transform maths and MyGUI gizmo drag tests passed");
}
