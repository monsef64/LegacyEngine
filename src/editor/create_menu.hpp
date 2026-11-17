#pragma once
#include "core/components.hpp"
#include "core/ecs.hpp"
#include "core/hierarchy.hpp"
#include <imgui.h>

// IDs: -1 none; 0 empty; 1..4 solids; 5 camera; 6 audio; 7..9 lights; 10 plane; 11 skybox; 12
// character; 13 FPS camera; 14 raycast; 15 particles; 16 laser; 17 shafts; 18 terrain;
// 19 clouds; 20 fog; 21 river.
inline constexpr int editorObjectTypeCount = 22;
inline bool validEditorObjectType(int item) {
    return item >= 0 && item < editorObjectTypeCount;
}
inline int drawCreateItems() {
    int item = -1;
    if (ImGui::MenuItem("Empty GameObject")) {
        item = 0;
    }
    if (ImGui::MenuItem("Character Controller")) {
        item = 12;
    }
    if (ImGui::BeginMenu("Cameras")) {
        if (ImGui::MenuItem("Camera")) {
            item = 5;
        }
        if (ImGui::MenuItem("FPS")) {
            item = 13;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Environment")) {
        if (ImGui::MenuItem("Clouds")) {
            item = 19;
        }
        if (ImGui::MenuItem("Fog")) {
            item = 20;
        }
        if (ImGui::MenuItem("River")) {
            item = 21;
        }
        if (ImGui::MenuItem("Terrain")) {
            item = 18;
        }
        if (ImGui::MenuItem("Skybox")) {
            item = 11;
        }
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("LightShafts")) {
        item = 17;
    }
    if (ImGui::MenuItem("Laser")) {
        item = 16;
    }
    if (ImGui::MenuItem("Particles")) {
        item = 15;
    }
    if (ImGui::MenuItem("Raycast")) {
        item = 14;
    }
    if (ImGui::BeginMenu("Audio")) {
        if (ImGui::MenuItem("Audio Source")) {
            item = 6;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Lights")) {
        if (ImGui::MenuItem("DirectionalLight")) {
            item = 7;
        }
        if (ImGui::MenuItem("PointLight")) {
            item = 8;
        }
        if (ImGui::MenuItem("SpotLight")) {
            item = 9;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Shape")) {
        const char* names[]{"Capsule", "Cone", "Cube", "Sphere"};
        for (int i = 0; i < 4; ++i) {
            if (ImGui::MenuItem(names[i])) {
                item = i + 1;
            }
        }
        if (ImGui::MenuItem("Plane")) {
            item = 10;
        }
        ImGui::EndMenu();
    }
    return item;
}
inline int drawCreateMenu() {
    int item = -1;
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("Create")) {
            item = drawCreateItems();
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    return item;
}
inline Entity createLevelObject(World& world,
                                const char* name,
                                Vec3 position,
                                MeshHandle mesh = {},
                                ColliderShape shape = ColliderShape::Box,
                                bool empty = false) {
    const Entity e = world.create();
    world.add(e, ObjectName{name});
    world.add(e, Transform{position});
    if (!empty) {
        world.add(e, MeshRenderer{mesh, {1, 1, 1}});
        Collider collider;
        collider.shape = shape;
        world.add(e, collider);
        world.add(e, RigidBody{});
    }
    return e;
}

inline Entity createEditorObject(World& world,
                                 int item,
                                 Vec3 position,
                                 MeshHandle cube,
                                 MeshHandle sphere,
                                 MeshHandle capsule,
                                 MeshHandle cone,
                                 MeshHandle plane) {
    static const char* names[]{"Empty GameObject",
                               "Capsule",
                               "Cone",
                               "Cube",
                               "Sphere",
                               "Camera",
                               "Audio Source",
                               "Directional Light",
                               "Point Light",
                               "Spot Light",
                               "Plane",
                               "Skybox",
                               "Character Controller",
                               "FPS Camera",
                               "Raycast",
                               "Particles",
                               "Laser",
                               "LightShafts",
                               "Terrain",
                               "Clouds",
                               "Fog",
                               "River"};
    static_assert(sizeof(names) / sizeof(names[0]) == editorObjectTypeCount);
    if (!validEditorObjectType(item)) {
        return {};
    }
    const MeshHandle meshes[]{MeshHandle{}, capsule, cone, cube, sphere};
    const ColliderShape shapes[]{ColliderShape::Box,
                                 ColliderShape::Capsule,
                                 ColliderShape::Cone,
                                 ColliderShape::Box,
                                 ColliderShape::Sphere};
    auto entity = createLevelObject(world,
                                    names[item],
                                    position,
                                    item > 0 && item < 5 ? meshes[item] : MeshHandle{},
                                    item > 0 && item < 5 ? shapes[item] : ColliderShape::Box,
                                    item == 0 || item >= 5);
    if (item == 19) {
        world.get<Transform>(entity).position.y += 20;
        world.add(entity, CloudSystem{});
    }
    if (item == 20) {
        world.add(entity, Fog{});
    }
    if (item == 21) {
        world.add(entity, River{});
    }
    if (item == 18) {
        world.add(entity, Terrain{});
        Terrains::openTools = true;
    }
    if (item == 17) {
        world.add(entity, LightShafts{});
    }
    if (item == 16) {
        world.add(entity, Laser{});
    }
    if (item == 15) {
        world.add(entity, ParticleSystem{});
    }
    if (item == 14) {
        world.add(entity, Raycast{});
    }
    if (item == 12) {
        addCharacterController(world, entity);
    }
    if (item == 13) {
        addFPSCamera(world, entity);
    }
    if (item == 5 || item == 13) {
        Camera camera;
        bool found = false;
        world.each<Camera>([&](Entity, const Camera& c) { found |= c.active; });
        camera.active = !found;
        world.add(entity, camera);
        world.add(entity, AudioListener{});
    }
    if (item == 6) {
        world.add(entity, AudioSource{});
    }
    if (item == 7) {
        world.add(entity, DirectionalLight{});
    }
    if (item == 8) {
        world.add(entity, PointLight{});
    }
    if (item == 9) {
        world.add(entity, SpotLight{});
    }
    if (item == 10) {
        world.add(entity, MeshRenderer{plane, {1, 1, 1}});
        Collider collider;
        collider.size = {16, .1f, 16};
        collider.center = {0, -.05f, 0};
        world.add(entity, collider);
    }
    if (item == 11) {
        world.add(entity, Skybox{});
    }
    return entity;
}
