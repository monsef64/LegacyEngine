#pragma once
#include "core/components.hpp"
#include "core/ecs.hpp"
#include <unordered_set>

// Transforms remain world-space for rendering, physics and Lua. The snapshot
// carries the last parent pose so parent deltas can be applied to children.
struct Parent {
    Entity entity;
    Transform previous;
};
inline Quat composeRotation(Quat a, Quat b) {
    return normalize(Quat{a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                          a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                          a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                          a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z});
}
inline Vec3 rotateVector(Quat q, Vec3 v) {
    q = normalize(q);
    const Vec3 u{q.x, q.y, q.z};
    const Vec3 t = cross(u, v) * 2;
    return v + t * q.w + cross(u, t);
}
inline bool canParent(World& world, Entity child, Entity parent) {
    if (!world.alive(child) || !world.has<Transform>(child)) {
        return false;
    }
    if (parent == Entity{}) {
        return true;
    }
    if (!world.alive(parent) || !world.has<Transform>(parent)) {
        return false;
    }
    std::unordered_set<std::uint32_t> seen;
    for (Entity e = parent; world.alive(e);) {
        if (e == child || seen.size() >= 128 || !seen.insert(e.index).second) {
            return false;
        }
        const auto* link = world.tryGet<Parent>(e);
        if (!link) {
            break;
        }
        e = link->entity;
    }
    return true;
}
inline bool setParent(World& world, Entity child, Entity parent) {
    if (!canParent(world, child, parent)) {
        return false;
    }
    if (parent == Entity{}) {
        world.remove<Parent>(child);
    } else {
        world.add(child, Parent{parent, world.get<Transform>(parent)});
    }
    return true; // reparenting preserves the current world pose
}
inline Entity getParent(World& world, Entity child) {
    const auto* link = world.tryGet<Parent>(child);
    return link && world.alive(link->entity) ? link->entity : Entity{};
}
inline void updateHierarchy(World& world) {
    std::unordered_set<std::uint32_t> done, visiting;
    auto visit = [&](auto&& self, Entity child) -> void {
        if (done.count(child.index)) {
            return;
        }
        auto* link = world.tryGet<Parent>(child);
        if (!link || !world.has<Transform>(child)) {
            done.insert(child.index);
            return;
        }
        if (!visiting.insert(child.index).second) {
            world.remove<Parent>(child);
            return;
        }
        const Entity parent = link->entity;
        if (!world.alive(parent) || !world.has<Transform>(parent)) {
            world.remove<Parent>(child);
        } else {
            self(self, parent);
            // Re-fetch after recursion: removing a Parent can move its pool.
            link = world.tryGet<Parent>(child);
            if (link) {
                const Transform current = world.get<Transform>(parent), old = link->previous;
                if (current.position == old.position && current.rotation == old.rotation &&
                    current.scale == old.scale) {
                    visiting.erase(child.index);
                    done.insert(child.index);
                    return;
                }
                auto& transform = world.get<Transform>(child);
                const Quat inverse{
                    -old.rotation.x, -old.rotation.y, -old.rotation.z, old.rotation.w};
                const auto ratio = [](float a, float b) {
                    return std::abs(b) > 1e-6f ? a / b : 1.0f;
                };
                const Vec3 scaling{ratio(current.scale.x, old.scale.x),
                                   ratio(current.scale.y, old.scale.y),
                                   ratio(current.scale.z, old.scale.z)};
                const Vec3 relative =
                    rotateVector(inverse, transform.position - old.position) * scaling;
                transform.position = current.position + rotateVector(current.rotation, relative);
                transform.rotation =
                    composeRotation(composeRotation(current.rotation, inverse), transform.rotation);
                transform.scale = transform.scale * scaling;
                link->previous = current;
            }
        }
        visiting.erase(child.index);
        done.insert(child.index);
    };
    world.each<Transform>([&](Entity e, Transform&) { visit(visit, e); });
}
