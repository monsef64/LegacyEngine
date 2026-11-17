#include "editor/transform_editor.hpp"
#include "core/hierarchy.hpp"
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

    // Editor state the Qt gizmo reads: tool, space, pivot, snap and the
    // held-body protocol that keeps a dynamic object still mid-drag.
    TransformEditor editor;
    require(editor.tool()==TransformTool::Move,"default tool is move");
    editor.setTool(TransformTool::Rotate);
    require(editor.tool()==TransformTool::Rotate,"tool changes");
    require(!editor.capturesMouse(),"idle editor does not capture the mouse");
    editor.setSnapStep(0.6f);
    require(editor.snapStep()==0.5f,"snap step rounds to a quarter");
    editor.setSnapStep(NAN);
    require(editor.snapStep()==0.5f,"non-finite snap step falls back");
    editor.setSnapStep(-10000.0f);
    require(editor.snapStep()==0.25f,"snap step clamps low");
    editor.setLocalSpace(true);
    require(editor.localSpace(),"local space toggle");
    editor.setCenterPivot(true);
    require(editor.centerPivot(),"pivot toggle");

    // A dynamic body is taken out of simulation for the drag and put back,
    // with its original settings, when the gesture finishes.
    World world;
    const Entity e=world.create();
    world.add(e,Transform{});
    RigidBody body;body.mass=2;body.useGravity=false;
    world.add(e,body);
    editor.finish(world);
    require(world.has<RigidBody>(e),"finish with no gesture leaves the body alone");
    require(world.get<RigidBody>(e).mass==2,"body settings survive");

    // Centring the gizmo: a mesh-less object reports its own position, and an
    // attached child's position widens the bounds to the midpoint.
    editor.meshBounds=[](MeshHandle,Vec3&,Vec3&){return false;};
    require(editor.selectionCenter(world,e)==Vec3{},"mesh-less object centres on itself");
    const Entity child=world.create();
    world.add(child,Transform{{10,0,0}});
    setParent(world,child,e);
    require(editor.selectionCenter(world,e)==Vec3{5,0,0},
            "child positions widen the gizmo bounds");
    std::puts("Transform maths and editor state tests passed");
}
