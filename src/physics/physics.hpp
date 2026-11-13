// The physics simulation, done by ReactPhysics3D.
//
// It reads the components of the world (see components.hpp):
//
//   * Transform + Collider              an obstacle that stays where it is
//   * Transform + Collider + RigidBody  an object that falls and collides
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
//   * An entity with a number in its Transform, Collider or RigidBody that
//     is NaN, infinite or beyond a million (the result of a division by zero,
//     say, or of falling for minutes) is left out of the simulation and
//     reported on stderr. It takes part again once the number is repaired.

#pragma once

#include "core/ecs.hpp"
#include "core/math.hpp"
#include "physics/queries.hpp"

#include <functional>
#include <memory>

class Physics {
  public:
    struct TriggerEvent {
        Entity receiver, other;
        int phase;
    }; // 0 enter, 1 stay, 2 exit
    std::vector<TriggerEvent> triggerEvents;
    std::vector<RayHit> raycastAll(const Ray& ray,
                                   float distance,
                                   const std::function<bool(Entity)>& filter = {}) const;
    std::vector<RayHit>
    overlapSphere(Vec3 origin, float radius, const std::function<bool(Entity)>& filter = {}) const;
    Physics();
    ~Physics();
    void reset(); // Discard solver bodies, contacts and accumulated time.
    Physics(const Physics&) = delete;
    Physics& operator=(const Physics&) = delete;

    // Advances the simulation. Call it once per frame with the time that has
    // passed since the previous call. beforeStep runs immediately before each
    // fixed solver step; its ECS changes are synchronized before simulation.
    void update(World& world, float seconds, const std::function<void(float)>& beforeStep = {});

    // The two questions below are about the colliders as the last update()
    // left them: what has been added, moved or destroyed since then is not
    // taken into account.

    // Finds the first Collider that a ray meets within `maxDistance`
    // metres: what is under the mouse pointer, for example (see
    // OrbitCamera::rayThrough()). Returns false, and leaves `hit` alone, if it
    // meets none. A collider that the ray starts inside of does not count.
    bool raycast(const Ray& ray, float maxDistance, RayHit& hit) const;

    // Whether a box would reach into a Collider, or touch one: whether
    // there is no room for a new object, for example. `center` is the middle
    // of the box and `size` the length of its edges, which lie along the X, Y
    // and Z axes.
    bool checkBox(Vec3 center, Vec3 size) const;

  private:
    struct Impl; // keeps ReactPhysics3D out of the rest of the engine
    std::unique_ptr<Impl> impl_;
};
