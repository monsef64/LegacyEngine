#pragma once
#include "core/hierarchy.hpp"
#include "editor/rotation_editor.hpp"
#include "input/input.hpp"
#include "physics/physics.hpp"

inline void updateCharacterControllers(World& world, Physics& physics) {
    world.each<CharacterController, Transform, RigidBody, Collider>(
        [&](Entity e, CharacterController& c, Transform& t, RigidBody& body, Collider& collider) {
            if (!isActive(world, e)) {
                return;
            }
            const float halfHeight =
                collider.shape == ColliderShape::Capsule
                    ? (collider.height * .5f + collider.radius) * std::abs(t.scale.y)
                : collider.shape == ColliderShape::Sphere
                    ? collider.radius * std::abs(t.scale.y)
                    : collider.size.y * .5f * std::abs(t.scale.y);
            const Vec3 origin = t.position + rotateVector(t.rotation, collider.center * t.scale);
            auto groundHits = physics.raycastAll(
                {origin, {0, -1, 0}}, halfHeight + c.groundProbe, [&](Entity target) {
                    return target != e && world.has<Collider>(target) &&
                           !world.get<Collider>(target).isTrigger;
                });
            c.isGrounded = body.linearVelocity.y <= .1f && !groundHits.empty() &&
                           groundHits.front().normal.y > .55f;
            Vec3 direction = rotateVector(t.rotation, c.movement);
            direction.y = 0;
            const float length = std::sqrt(dot(direction, direction));
            if (length > 1) {
                direction = direction * (1 / length);
            }
            c.isMoving = c.canMove && dot(direction, direction) > 1e-6f;
            c.isSprinting = c.isMoving && c.canSprint && c.sprintRequested;
            const float speed = c.speed * (c.isSprinting ? c.sprintMultiplier : 1);
            body.linearVelocity.x = c.isMoving ? direction.x * speed : 0;
            body.linearVelocity.z = c.isMoving ? direction.z * speed : 0;
            if (c.jumpRequested && c.canJump && c.isGrounded) {
                body.linearVelocity.y = c.jumpSpeed;
                c.isGrounded = false;
            }
            c.jumpRequested = false;
            c.isJumping = !c.isGrounded && body.linearVelocity.y > 0;
            body.angularVelocity = {};
        });
}
inline void updateFPSCameras(World& world, float dt, InputState& input) {
    world.each<FPSCamera, Camera, Transform>(
        [&](Entity e, FPSCamera& fps, Camera& camera, Transform&) {
            if (!isActive(world, e) || !camera.active) {
                return;
            }
            auto local = localTransform(world, e);
            const Entity parent = getParent(world, e);
            const bool looking =
                input.enabled && input.mouseEnabled && fps.mouseLook &&
                (input.mouseLocked || (parent == Entity{} && input.mouse[SDL_BUTTON_RIGHT]));
            if (looking) {
                const Vec3 forward = rotateVector(local.rotation, {0, 0, -1});
                float pitch = std::asin(std::clamp(forward.y, -1.f, 1.f)) * 180 / kPi;
                float yaw = std::atan2(-forward.x, -forward.z) * 180 / kPi;
                pitch = std::clamp(pitch - input.dy * fps.sensitivity, -89.f, 89.f);
                const float delta = -input.dx * fps.sensitivity;
                if (parent != Entity{} && world.has<CharacterController>(parent)) {
                    auto& character = world.get<Transform>(parent);
                    character.rotation = composeRotation(
                        character.rotation, axisAngle({0, 1, 0}, radians(yaw + delta)));
                    updateHierarchy(world);
                    local = localTransform(world, e);
                    yaw = 0;
                } else {
                    yaw += delta;
                }
                local.rotation = rotationFromEuler({pitch, yaw, 0});
            }
            if (parent == Entity{} && fps.isFreecam && input.enabled) {
                Vec3 direction{
                    float(input.keys[SDL_SCANCODE_D]) - float(input.keys[SDL_SCANCODE_A]),
                    float(input.keys[SDL_SCANCODE_E]) - float(input.keys[SDL_SCANCODE_Q]),
                    float(input.keys[SDL_SCANCODE_S]) - float(input.keys[SDL_SCANCODE_W])};
                const float length = std::sqrt(dot(direction, direction));
                if (length > 1) {
                    direction = direction * (1 / length);
                }
                local.position =
                    local.position + rotateVector(local.rotation, direction) * (fps.speed * dt);
            }
            setLocalTransform(world, e, local);
        });
}
