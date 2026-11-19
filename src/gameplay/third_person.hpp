#pragma once
#include "gameplay/first_person.hpp"
#include "scene/scene_camera.hpp"

inline void updateTPSCameras(World& world, Physics& physics, float dt, InputState& input) {
    world.each<TPSCamera, Camera, Transform>(
        [&](Entity e, TPSCamera& c, Camera& camera, Transform& t) {
            if (!c.enabled || !camera.active || !isActive(world, e)) {
                return;
            }
            Entity target = c.target != Entity{} ? c.target : getParent(world, e);
            if (target == e || !world.has<Transform>(target)) {
                c.initialized = false;
                return;
            }
            // Never follow a descendant: that would feed this camera's motion back into itself.
            auto ancestor = target;
            for (unsigned depth = 0; world.alive(ancestor) && depth < 128; ++depth) {
                if (ancestor == e) {
                    return;
                }
                ancestor = getParent(world, ancestor);
            }
            if (input.enabled && input.mouseEnabled) {
                if (c.mouseLook && (input.mouseLocked || input.mouse[SDL_BUTTON_RIGHT])) {
                    c.yaw = std::fmod(c.yaw - input.dx * c.sensitivity, 360.f);
                    c.pitch =
                        std::clamp(c.pitch - input.dy * c.sensitivity, c.minPitch, c.maxPitch);
                }
                c.distance = std::clamp(
                    c.distance - input.wheelY * c.zoomSpeed, c.minDistance, c.maxDistance);
            }
            const Vec3 pivot = world.get<Transform>(target).position + c.offset;
            const auto rotation = rotationFromEuler({c.pitch, c.yaw, 0});
            const Vec3 right = rotateVector(rotation, {1, 0, 0});
            const Vec3 desired = pivot + rotateVector(rotation, {c.shoulder, 0, c.distance});
            auto filter = [&](Entity other) {
                if (other == e || !isActive(world, other)) {
                    return false;
                }
                if (const auto* collider = world.tryGet<Collider>(other);
                    collider && collider->isTrigger) {
                    return false;
                }
                if (world.has<Water>(other)) {
                    return false;
                }
                auto p = other;
                for (unsigned depth = 0; world.alive(p) && depth < 128; ++depth) {
                    if (p == target) {
                        return false;
                    }
                    p = getParent(world, p);
                }
                return true;
            };
            Vec3 position = desired;
            if (c.initialized && c.smoothing > 0) {
                position = c.previousPosition +
                           (desired - c.previousPosition) * (1 - std::exp(-c.smoothing * dt));
            }
            if (c.collision) {
                const Vec3 delta = position - pivot;
                const float length = std::sqrt(dot(delta, delta));
                // Cover the near-plane diagonal as well as the user collision radius.
                const float aspect =
                    camera.aspect * camera.viewportWidth / std::max(.001f, camera.viewportHeight);
                const float halfHeight =
                    camera.orthographic
                        ? camera.orthographicSize * .5f
                        : camera.nearClip * std::tan(radians(camera.fieldOfView) * .5f);
                const float nearRadius = std::sqrt(camera.nearClip * camera.nearClip +
                                                   halfHeight * halfHeight * (1 + aspect * aspect));
                RayHit hit;
                if (length > .0001f &&
                    physics.sphereCast({pivot, delta * (1 / length)},
                                       length,
                                       std::max(c.collisionRadius, nearRadius + .02f),
                                       hit,
                                       filter)) {
                    position = pivot + delta * (std::max(0.f, hit.distance - .03f) / length);
                }
            }
            t.position = position;
            aimCamera(t, pivot + right * c.shoulder);
            c.resolvedDistance = std::sqrt(dot(position - pivot, position - pivot));
            c.initialized = true;
            c.previousPosition = position;
            if (auto* parent = world.tryGet<Parent>(e)) {
                parent->previous = world.get<Transform>(parent->entity);
            }
        });
}
