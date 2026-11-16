#pragma once
#include "core/hierarchy.hpp"
#include "physics/physics.hpp"
namespace Lasers {
inline void update(World& world, const Physics& physics) {
    world.each<Transform, Laser>([&](Entity e, const Transform& t, Laser& laser) {
        laser.ready = false;
        laser.hit = false;
        if (!laser.enabled || !isActive(world, e)) {
            return;
        }
        const Vec3 direction = rotateVector(normalize(t.rotation), {0, 0, -1});
        laser.end = t.position + direction * laser.range;
        RayHit hit;
        if (physics.raycast({t.position, direction}, laser.range, hit, [&](Entity other) {
                return other != e && isActive(world, other);
            })) {
            laser.end = hit.point;
            laser.hitNormal = hit.normal;
            laser.hit = true;
        }
        laser.ready = true;
    });
}
} // namespace Lasers
