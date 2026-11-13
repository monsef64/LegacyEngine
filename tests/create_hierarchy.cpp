#include "editor/create_menu.hpp"
#include "core/hierarchy.hpp"
#include "physics/physics.hpp"
#include <cstdio>
#include <cstdlib>

static void require(bool ok,const char* text){if(!ok){std::fprintf(stderr,"FAIL: %s\n",text);std::exit(1);}}
static bool near(Vec3 a,Vec3 b){return dot(a-b,a-b)<1e-7f;}
int main(){
    World world;
    Entity createdPlane=createEditorObject(world,10,{1,0,2},{1},{2},{3},{4},{5});
    require(world.has<MeshRenderer>(createdPlane) && world.get<MeshRenderer>(createdPlane).mesh.id==5,"Plane menu item uses plane geometry");
    require(world.has<Collider>(createdPlane) && !world.has<RigidBody>(createdPlane) && world.get<Collider>(createdPlane).size.y==.1f,"Plane is static with thin box collision");
    Entity createdSky=createEditorObject(world,11,{},{1},{2},{3},{4},{5});
    require(world.has<Skybox>(createdSky) && world.has<Transform>(createdSky) && !world.has<MeshRenderer>(createdSky),"Skybox menu creates component GameObject");
    world.destroy(createdPlane);world.destroy(createdSky);
    Entity createdCamera=createEditorObject(world,5,{},{1},{2},{3},{4},{5});
    require(world.has<Camera>(createdCamera) && world.has<AudioListener>(createdCamera),"Camera menu creation includes AudioListener");world.destroy(createdCamera);
    Entity parent=createLevelObject(world,"Parent",{}, {},ColliderShape::Box,true);
    Entity child=createLevelObject(world,"Child",{1,0,0},{1},ColliderShape::Box,false);
    require(world.has<Transform>(parent) && !world.has<MeshRenderer>(parent),"empty object has transform without rendering");
    require(setParent(world,child,parent),"valid parent accepted");
    require(near(world.get<Transform>(child).position,{1,0,0}),"parent assignment preserves world pose");
    world.get<Transform>(parent).position={2,0,0};updateHierarchy(world);
    require(near(world.get<Transform>(child).position,{3,0,0}),"parent translation inherited");
    world.get<Transform>(parent).rotation=axisAngle({0,1,0},kPi*0.5f);updateHierarchy(world);
    require(near(world.get<Transform>(child).position,{2,0,-1}),"parent rotation inherited around pivot");
    world.get<Transform>(parent).scale={2,2,2};updateHierarchy(world);
    require(near(world.get<Transform>(child).position,{2,0,-2}) && world.get<Transform>(child).scale==Vec3{2,2,2},"parent scale inherited");
    Entity grandchild=createLevelObject(world,"Grandchild",{2,0,-3},{1},ColliderShape::Box,true);
    require(setParent(world,grandchild,child),"nested parent accepted");
    require(!setParent(world,parent,grandchild) && !setParent(world,child,child),"cycles and self parenting rejected");
    const Transform before=world.get<Transform>(child);
    for(int i=0;i<100;++i)updateHierarchy(world);
    require(world.get<Transform>(child).position==before.position && world.get<Transform>(child).rotation==before.rotation,"idle hierarchy does not drift");
    world.destroy(parent);updateHierarchy(world);
    require(getParent(world,child)==Entity{} && near(world.get<Transform>(child).position,before.position),"destroyed parent detaches child without moving it");
    require(setParent(world,grandchild,{}),"unparent accepted");
    require(getParent(world,grandchild)==Entity{},"unparent clears link");
    Entity localParent=world.create(),localChild=world.create();
    world.add(localParent,Transform{{3,4,5},axisAngle({0,1,0},.7f),{2,3,-4}});
    world.add(localChild,Transform{});require(setParent(world,localChild,localParent),"local parent accepted");
    Transform local{{1,2,3},axisAngle({1,0,0},.3f),{.5f,2,1}};
    setLocalTransform(world,localChild,local);
    auto recovered=localTransform(world,localChild);
    require(near(local.position,recovered.position) && near(local.scale,recovered.scale),"parent-relative position and scale roundtrip");
    require(near(rotateVector(local.rotation,{0,0,-1}),rotateVector(recovered.rotation,{0,0,-1})),"local rotation roundtrip");
    auto worldPose=world.get<Transform>(localChild);updateHierarchy(world);
    require(near(worldPose.position,world.get<Transform>(localChild).position),"local edit not reapplied by hierarchy");
    world.get<Transform>(localParent).scale.x=0;updateHierarchy(world);
    require(std::isfinite(localTransform(world,localChild).position.x),"zero parent scale is safe");
    // Cone triangles and the collision hull share the exact same perimeter.
    const auto mesh=makeCone(0.5f,1);
    require(mesh.vertices.size()==static_cast<std::size_t>(kConeSegments*6),"cone triangles complete");
    for(std::size_t i=0;i<mesh.vertices.size();i+=3){
        const auto a=mesh.vertices[i],b=mesh.vertices[i+1],c=mesh.vertices[i+2];
        require(dot(cross(b.position-a.position,c.position-a.position),a.normal)>0,"cone outward winding");
    }
    Physics physics;
    Entity cone=createLevelObject(world,"Cone",{0,3,0},{4},ColliderShape::Cone);
    world.remove<RigidBody>(cone);physics.update(world,0);
    RayHit hit;
    require(physics.raycast({{0,6,0},{0,-1,0}},10,hit) && hit.entity==cone && std::abs(hit.point.y-3.5f)<0.001f,"cone tip ray hit");
    require(!physics.raycast({{0.4f,3.4f,2},{0,0,-1}},2.1f,hit),"cone empty upper corner is not a box");
    require(physics.checkBox({0,3,0},{0.05f,0.05f,0.05f}),"cone interior overlaps");
    require(!physics.checkBox({0.4f,3.4f,0},{0.01f,0.01f,0.01f}),"cone narrow tip does not overlap box corner");
    Entity floor=createLevelObject(world,"Floor",{0,-0.5f,0},{1},ColliderShape::Box);
    world.remove<RigidBody>(floor);world.get<Collider>(floor).size={10,1,10};world.add(cone,RigidBody{});
    for(int i=0;i<600;++i)physics.update(world,1.0f/60);
    require(std::abs(world.get<Transform>(cone).position.y-0.5f)<0.05f,"cone rests on matching base collider");
    world.destroy(cone);physics.update(world,0);
    std::puts("Create objects, parenting, cone geometry and collider tests passed");
}
