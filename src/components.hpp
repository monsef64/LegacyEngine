// The components the engine itself understands. Attach them to entities with
// World::add() (see ecs.hpp) and change their values whenever you like: the
// renderer and the physics pick the changes up on the next frame.

#pragma once

#include "math.hpp"
#include "mesh.hpp"
#include <string>

// Human-readable label used by the level object list.
struct ObjectName { std::string value; };

// Where the entity is, how it is turned and how big it is.
struct Transform {
    Vec3 position;                   // metres
    Quat rotation;                   // see axisAngle() in math.hpp
    Vec3 scale = {1.0f, 1.0f, 1.0f}; // 2 is twice the size of the mesh
};

// Draws a mesh at the entity's Transform.
struct MeshRenderer {
    MeshHandle mesh;                     // from Renderer::createMesh()
    Vec3       color = {1.0f, 1.0f, 1.0f}; // red, green and blue, each from 0 to 1
    bool       checkerboard = false;         // repeating 1 m squares in local XZ
    float      specular = 0.0f;              // highlight strength (0 is matte)
};

enum class ColliderShape { Box, Sphere, Capsule };

// A solid shape around the entity that other colliders cannot pass through. It
// follows the entity's Transform, scale included.
//
// On its own, a collider makes an obstacle that never moves by itself (a floor,
// a wall). Add a RigidBody as well for an object that falls and gets pushed.
struct Collider {
    Vec3  size = {1.0f, 1.0f, 1.0f}; // metres, before the Transform's scale
    Vec3  center;                    // of the box, measured from the entity's origin
    float friction   = 0.5f;         // 0 slides like ice, 1 grips
    float bounciness = 0.0f;         // 0 does not bounce, 1 bounces back all the way
    ColliderShape shape = ColliderShape::Box;
    // Sphere: radius. Capsule: radius and distance between hemisphere centres, along Y.
    // Use uniform Transform scale for round shapes (non-uniform scales use the largest radial scale).
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
    float mass       = 1.0f; // kilograms; at 0, gravity and pushes do not move it
    bool  useGravity = true; // switch off to keep it floating
    Vec3  linearVelocity;    // metres per second
    Vec3  angularVelocity;   // radians per second around each axis
};
