// The components the engine itself understands. Attach them to entities with
// World::add() (see ecs.hpp) and change their values whenever you like: the
// renderer and the physics pick the changes up on the next frame.

#pragma once

#include "audio/audio_source.hpp"
#include "core/ecs.hpp"
#include "core/math.hpp"
#include "lighting/light.hpp"
#include "materials/material.hpp"
#include "rendering/mesh.hpp"
#include "rendering/skybox.hpp"
#include <string>
#include <vector>

// Human-readable label used by the level object list.
struct ObjectName {
    std::string value;
};

// Where the entity is, how it is turned and how big it is.
struct Transform {
    Vec3 position;                   // metres
    Quat rotation;                   // see axisAngle() in math.hpp
    Vec3 scale = {1.0f, 1.0f, 1.0f}; // 2 is twice the size of the mesh
};

// Draws a mesh at the entity's Transform.
struct MeshRenderer {
    MeshHandle mesh;                 // from Renderer::createMesh()
    Vec3 color = {1.0f, 1.0f, 1.0f}; // red, green and blue, each from 0 to 1
    bool checkerboard = false;       // repeating 1 m squares in local XZ
    float specular = 0.0f;           // highlight strength (0 is matte)
    std::string material;            // project-relative .mat; empty uses legacy Diffuse values
    MaterialProperties materialValues;
    unsigned materialOverrides = 0; // per-instance script overrides; never edits shared asset
};

enum class ColliderShape { Box, Sphere, Capsule, Cone };

// A solid shape around the entity that other colliders cannot pass through. It
// follows the entity's Transform, scale included.
//
// On its own, a collider makes an obstacle that never moves by itself (a floor,
// a wall). Add a RigidBody as well for an object that falls and gets pushed.
struct Collider {
    Vec3 size = {1.0f, 1.0f, 1.0f}; // metres, before the Transform's scale
    Vec3 center;                    // of the box, measured from the entity's origin
    float friction = 0.5f;          // 0 slides like ice, 1 grips
    float bounciness = 0.0f;        // 0 does not bounce, 1 bounces back all the way
    ColliderShape shape = ColliderShape::Box;
    // Sphere: radius. Capsule: radius and distance between hemisphere centres, along Y.
    // Use uniform Transform scale for round shapes (non-uniform scales use the largest radial
    // scale).
    float radius = 0.5f;
    float height = 1.0f;
};

// Backward-compatible name: existing box scene code remains valid.
using BoxCollider = Collider;

// Hands the entity over to the physics simulation, which then moves its
// Transform: it falls, and colliders stop it and push it around.
//
// Game code stays in charge: assign to the Transform or to the velocities below
// and the simulation carries on from the new values.
struct RigidBody {
    float mass = 1.0f;      // kilograms; at 0, gravity and pushes do not move it
    bool useGravity = true; // switch off to keep it floating
    Vec3 linearVelocity;    // metres per second
    Vec3 angularVelocity;   // radians per second around each axis
};

// Primary LegacyScript and additional attachments, all relative to project assets.
struct ScriptField {
    std::string name, type, text;
    float number = 0;
    bool boolean = false;
    Entity entity{};
};
struct EditorComponentPayload {
    Entity entity;
    int type;
};
struct ScriptAttachment {
    std::string path;
    bool enabled = true;
    std::vector<ScriptField> fields;
};
struct LuaScript {
    std::string path;
    bool enabled = true;
    std::vector<ScriptAttachment> additional;
    std::vector<ScriptField> fields;
};
inline std::vector<ScriptAttachment> scriptAttachments(const LuaScript& scripts) {
    std::vector<ScriptAttachment> result;
    if (!scripts.path.empty()) {
        result.push_back({scripts.path, scripts.enabled, scripts.fields});
    }
    result.insert(result.end(), scripts.additional.begin(), scripts.additional.end());
    return result;
}

// Cameras look along local -Z; rotation lives in the GameObject Transform.
struct Camera {
    bool active = false, orthographic = false;
    float fieldOfView = 60, nearClip = .1f, farClip = 1000, orthographicSize = 10;
    float viewportX = 0, viewportY = 0, viewportWidth = 1, viewportHeight = 1;
    Entity targetEntity{}; // generation-safe target, or a fixed world-space point
    Vec3 target, followOffset;
    bool tracking = false, following = false;
    float shakeIntensity = 0, shakeRemaining = 0, shakePhase = 0;
    float aspect = 16.0f / 9; // Current Game viewport aspect, supplied by the editor.
};

// Absent means active, preserving old scenes. Parent deactivation propagates.
struct ObjectActivity {
    bool enabled = true;
};
struct Parent {
    Entity entity;
    Transform previous;
};
inline bool isActive(const World& world, Entity entity) {
    for (unsigned depth = 0; depth < 128 && world.alive(entity); ++depth) {
        if (const auto* state = world.tryGet<ObjectActivity>(entity); state && !state->enabled) {
            return false;
        }
        const auto* parent = world.tryGet<Parent>(entity);
        if (!parent) {
            return true;
        }
        entity = parent->entity;
    }
    return false;
}
inline void setObjectActive(World& world, Entity entity, bool enabled) {
    world.add(entity, ObjectActivity{enabled});
}

// Asset linkage belongs to an instance root, never to the prefab asset itself.
struct PrefabInstance {
    std::string asset;
};
struct DestroyAfter {
    float seconds = 0;
};

// Dynamic upright capsule controller. Commands are applied at fixed physics steps.
struct CharacterController {
    float speed = 4.5f, sprintMultiplier = 1.7f, jumpSpeed = 6, groundProbe = .12f;
    bool canMove = true, canJump = true, canSprint = true;
    bool isGrounded = false, isJumping = false, isMoving = false, isSprinting = false;
    Vec3 movement;
    bool sprintRequested = false, jumpRequested = false;
};
struct FPSCamera {
    bool isFreecam = true, mouseLook = true;
    float sensitivity = .12f, speed = 6;
    float yaw = 0, pitch = 0;
    bool initialized = false;
};
