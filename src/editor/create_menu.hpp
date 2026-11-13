#pragma once
#include "core/components.hpp"
#include "core/ecs.hpp"
#include <imgui.h>

// -1: nothing, 0: empty, 1..4: Capsule, Cone, Cube, Sphere.
inline int drawCreateItems() {
    int item=-1;
    if(ImGui::MenuItem("Empty GameObject"))item=0;
    if(ImGui::MenuItem("Camera"))item=5;
    if(ImGui::MenuItem("Audio Source"))item=6;
    if(ImGui::BeginMenu("Light")){
        if(ImGui::MenuItem("Directional Light"))item=7;
        if(ImGui::MenuItem("Point Light"))item=8;
        if(ImGui::MenuItem("Spot Light"))item=9;
        ImGui::EndMenu();
    }
    if(ImGui::BeginMenu("Shape")) {
        const char* names[]{"Capsule","Cone","Cube","Sphere"};
        for(int i=0;i<4;++i)if(ImGui::MenuItem(names[i]))item=i+1;
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
