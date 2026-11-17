#pragma once
#include "core/components.hpp"
#include "core/ecs.hpp"
#include "core/hierarchy.hpp"
#include "effects/laser.hpp"
#include "effects/light_shafts.hpp"
#include "particles/particle_system.hpp"
#include "rendering/skybox.hpp"
#include "terrain/terrain.hpp"
#include <cstddef>

// Object creation, shared by the Qt Create menu and the Hierarchy context menu.
//
// IDs: 0 empty; 1..4 solids; 5 camera; 6 audio; 7..9 lights; 10 plane;
// 11 skybox; 12 character; 13 FPS camera; 14 raycast; 15 particles; 16 laser;
// 17 light shafts; 18 terrain.
inline constexpr int editorObjectTypeCount = 19;
inline bool validEditorObjectType(int item) {
    return item >= 0 && item < editorObjectTypeCount;
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
                               "Terrain"};
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
    if (item == 18) {
        world.add(entity, Terrain{});
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
