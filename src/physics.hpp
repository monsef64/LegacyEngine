// The physics simulation, done by ReactPhysics3D.
//
// It reads the components of the world (see components.hpp):
//
//   * Transform + BoxCollider              an obstacle that stays where it is
//   * Transform + BoxCollider + RigidBody  an object that falls and collides
//
// and moves the Transform of every entity that has a RigidBody. Nothing needs
// to be registered: add, change or remove the components, or destroy the
// entity, and the simulation follows on the next update().
//
// Three things to know about it:
//
//   * ReactPhysics3D lets things that rest on each other overlap by up to
//     1 cm, which keeps them steady. A 1 m cube lying on the ground can have
//     its centre at y = 0.49 instead of 0.5.
//   * It is made for things of everyday size. A box that is smaller than about
//     2 cm on every side sinks into what it lands on, or falls through it.
//   * An entity with a number in its Transform, BoxCollider or RigidBody that
//     is NaN, infinite or beyond a million (the result of a division by zero,
//     say, or of falling for minutes) is left out of the simulation and
//     reported on stderr. It takes part again once the number is repaired.

#pragma once

#include "ecs.hpp"
#include "math.hpp"

#include <memory>

// What Physics::raycast() found.
struct RayHit {
    Entity entity;          // the entity whose BoxCollider the ray met
    Vec3   point;           // where it met it
    Vec3   normal;          // unit vector pointing straight out of the surface there
    float  distance = 0.0f; // metres from the origin of the ray to `point`
};

class Physics {
public:
    Physics();
    ~Physics();
    Physics(const Physics&) = delete;
    Physics& operator=(const Physics&) = delete;

    // Advances the simulation. Call it once per frame with the time that has
    // passed since the previous call.
    void update(World& world, float seconds);

    // The two questions below are about the BoxColliders as the last update()
    // left them: what has been added, moved or destroyed since then is not
    // taken into account.

    // Finds the first BoxCollider that a ray meets within `maxDistance`
    // metres: what is under the mouse pointer, for example (see
    // OrbitCamera::rayThrough()). Returns false, and leaves `hit` alone, if it
    // meets none. A collider that the ray starts inside of does not count.
    bool raycast(const Ray& ray, float maxDistance, RayHit& hit) const;

    // Whether a box would reach into a BoxCollider, or touch one: whether
    // there is no room for a new object, for example. `center` is the middle
    // of the box and `size` the length of its edges, which lie along the X, Y
    // and Z axes.
    bool checkBox(Vec3 center, Vec3 size) const;

private:
    struct Impl; // keeps ReactPhysics3D out of the rest of the engine
    std::unique_ptr<Impl> impl_;
};
