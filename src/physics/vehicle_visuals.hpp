#pragma once
#include "core/hierarchy.hpp"

namespace Vehicles {
// These reserved direct-child names bind generated visuals without serialized
// entity handles. Hierarchy duplication/prefabs preserve child names.
inline constexpr const char* wheelNames[]{
    "Vehicle Wheel FL", "Vehicle Wheel FR", "Vehicle Wheel RL", "Vehicle Wheel RR"};
inline Transform wheelPose(const Vehicle& v, int i) {
    Transform t;
    t.position = {(i % 2 ? 1.f : -1.f) * v.trackWidth * .5f,
                  v.mountHeight - v.suspensionLength * (1 - std::clamp(v.compression[i], 0.f, 1.f)),
                  (i < 2 ? -1.f : 1.f) * v.wheelBase * .5f};
    const float steer = i < 2 ? -v.wheelSteerAngle : 0;
    t.rotation = composeRotation(axisAngle({0, 1, 0}, steer), axisAngle({1, 0, 0}, -v.wheelRoll));
    // Built-in sphere has radius .5: flatten the axle dimension into a tire.
    t.scale = {v.wheelRadius * .7f, v.wheelRadius * 2, v.wheelRadius * 2};
    return t;
}
inline void createWheelMeshes(World& world, Entity root, MeshHandle sphere) {
    const auto* vehicle = world.tryGet<Vehicle>(root);
    if (!vehicle || !world.has<Transform>(root) || !valid(*vehicle)) {
        return;
    }
    const auto config = *vehicle;
    for (int i = 0; i < 4; ++i) {
        Entity wheel;
        for (auto e : world.entities()) {
            const auto* name = world.tryGet<ObjectName>(e);
            if (getParent(world, e) == root && name && name->value == wheelNames[i]) {
                wheel = e;
                break;
            }
        }
        if (wheel != Entity{}) {
            continue; // Safe to repeat; don't replace custom visuals.
        }
        wheel = world.create();
        world.add(wheel, ObjectName{wheelNames[i]});
        world.add(wheel, Transform{});
        world.add(wheel, MeshRenderer{sphere, {.055f, .06f, .065f}});
        setParent(world, wheel, root);
        setLocalTransform(world, wheel, wheelPose(config, i));
        auto hub = world.create();
        world.add(hub, ObjectName{"Wheel hub"});
        world.add(hub, Transform{});
        world.add(hub, MeshRenderer{sphere, {.55f, .57f, .6f}});
        setParent(world, hub, wheel);
        Transform hubPose;
        hubPose.scale = {1.08f, .55f, .55f};
        setLocalTransform(world, hub, hubPose);
    }
}
inline void updateWheelMeshes(World& world, float dt) {
    world.each<Vehicle>([&](Entity root, Vehicle& v) {
        if (!v.enabled || !valid(v) || !isActive(world, root)) {
            return;
        }
        const auto* rootPose = world.tryGet<Transform>(root);
        if (!rootPose) {
            return;
        }
        const float radius =
            v.wheelRadius * std::max(std::abs(rootPose->scale.x), std::abs(rootPose->scale.y));
        // Solver-scale resting jitter must not accumulate visible tire rotation.
        const float rollSpeed =
            v.groundedWheels > 0 && std::abs(v.throttle) < .01f && std::abs(v.speed) < .03f
                ? 0.f
                : v.speed;
        if (radius > .001f && std::isfinite(rollSpeed)) {
            v.wheelRoll =
                std::fmod(v.wheelRoll + rollSpeed * std::clamp(dt, 0.f, .25f) / radius, 2 * kPi);
        }
        for (auto e : world.entities()) {
            const auto* name = world.tryGet<ObjectName>(e);
            if (getParent(world, e) != root || !name || !world.has<Transform>(e)) {
                continue;
            }
            for (int i = 0; i < 4; ++i) {
                if (name->value == wheelNames[i]) {
                    setLocalTransform(world, e, wheelPose(v, i));
                    break;
                }
            }
        }
    });
}
} // namespace Vehicles
