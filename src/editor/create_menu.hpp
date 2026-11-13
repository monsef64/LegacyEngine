#pragma once
#include "core/components.hpp"
#include "core/ecs.hpp"
#include <imgui.h>

// IDs: -1 none; 0 empty; 1..4 solids; 5 camera; 6 audio; 7..9 lights; 10 plane; 11 skybox.
inline int drawCreateItems() {
    int item=-1;
    if(ImGui::MenuItem("Empty GameObject"))item=0;
    if(ImGui::MenuItem("Camera"))item=5;
    if(ImGui::MenuItem("Audio Source"))item=6;
    if(ImGui::MenuItem("Skybox"))item=11;
    if(ImGui::BeginMenu("Light")){
        if(ImGui::MenuItem("Directional Light"))item=7;
        if(ImGui::MenuItem("Point Light"))item=8;
        if(ImGui::MenuItem("Spot Light"))item=9;
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Shape")) {
        const char* names[]{"Capsule","Cone","Cube","Sphere"};
        for(int i=0;i<4;++i)if(ImGui::MenuItem(names[i]))item=i+1;
        if(ImGui::MenuItem("Plane"))item=10;
        ImGui::EndMenu();
    }
    return item;
}
inline int drawCreateMenu() {
    int item=-1;
    if(ImGui::BeginMainMenuBar()) {
        if(ImGui::BeginMenu("Create")){item=drawCreateItems();ImGui::EndMenu();}
        ImGui::EndMainMenuBar();
    }
    return item;
}
inline Entity createLevelObject(World& world,const char* name,Vec3 position,MeshHandle mesh={},
                                ColliderShape shape=ColliderShape::Box,bool empty=false) {
    const Entity e=world.create();world.add(e,ObjectName{name});world.add(e,Transform{position});
    if(!empty) {
        world.add(e,MeshRenderer{mesh,{0.3f,0.6f,0.9f}});
        Collider collider;collider.shape=shape;world.add(e,collider);world.add(e,RigidBody{});
    }
    return e;
}

inline Entity createEditorObject(World& world,int item,Vec3 position,MeshHandle cube,MeshHandle sphere,MeshHandle capsule,MeshHandle cone,MeshHandle plane){
    static const char* names[]{"Empty GameObject","Capsule","Cone","Cube","Sphere","Camera","Audio Source","Directional Light","Point Light","Spot Light","Plane","Skybox"};
    if(item<0 || item>11)return {};
    const MeshHandle meshes[]{MeshHandle{},capsule,cone,cube,sphere};
    const ColliderShape shapes[]{ColliderShape::Box,ColliderShape::Capsule,ColliderShape::Cone,ColliderShape::Box,ColliderShape::Sphere};
    auto entity=createLevelObject(world,names[item],position,item>0 && item<5?meshes[item]:MeshHandle{},item>0 && item<5?shapes[item]:ColliderShape::Box,item==0 || item>=5);
    if(item==5){Camera camera;bool found=false;world.each<Camera>([&](Entity,const Camera& c){found|=c.active;});camera.active=!found;world.add(entity,camera);}
    if(item==6)world.add(entity,AudioSource{});
    if(item==7)world.add(entity,DirectionalLight{});
    if(item==8)world.add(entity,PointLight{});
    if(item==9)world.add(entity,SpotLight{});
    if(item==10){world.add(entity,MeshRenderer{plane,{.7f,.7f,.7f}});Collider collider;collider.size={16,.1f,16};collider.center={0,-.05f,0};world.add(entity,collider);}
    if(item==11)world.add(entity,Skybox{});
    return entity;
}
