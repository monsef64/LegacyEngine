// The physics simulation, done by Jolt.
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
// Fixed-step Jolt integration with interpolated rendered poses. Invalid/nonfinite
// component data is excluded until repaired. Triangle-mesh colliders remain
// static concave geometry; use convex primitives for moving rigid bodies.

#pragma once

#include "core/ecs.hpp"
#include "core/math.hpp"
#include "physics/queries.hpp"

#include <filesystem>
#include <functional>
#include <memory>

class Physics {
  public:
    struct TriggerEvent {
        Entity receiver, other;
        int phase;
    }; // 0 enter, 1 stay, 2 exit
    std::vector<TriggerEvent> triggerEvents;
    std::vector<TriggerEvent> collisionEvents; // solid contacts, same phase numbering
    std::vector<RayHit> raycastAll(const Ray& ray,
                                   float distance,
                                   const std::function<bool(Entity)>& filter = {}) const;
    std::vector<RayHit>
    overlapSphere(Vec3 origin, float radius, const std::function<bool(Entity)>& filter = {}) const;
    Physics();
    ~Physics();
    void setAssetRoot(const std::filesystem::path& root);
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
    bool raycast(const Ray& ray,
                 float maxDistance,
                 RayHit& hit,
                 const std::function<bool(Entity)>& filter = {}) const;

    // Whether a box would reach into a Collider, or touch one: whether
    // there is no room for a new object, for example. `center` is the middle
    // of the box and `size` the length of its edges, which lie along the X, Y
    // and Z axes.
    bool checkBox(Vec3 center, Vec3 size) const;

  private:
    struct Impl; // keeps Jolt out of the rest of the engine
    std::unique_ptr<Impl> impl_;
};
