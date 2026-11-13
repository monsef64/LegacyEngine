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
            RayHit hit;
            const Vec3 origin = t.position + rotateVector(t.rotation, collider.center * t.scale);
            c.isGrounded = body.linearVelocity.y <= .1f &&
                           physics.raycast({origin, {0, -1, 0}}, halfHeight + c.groundProbe, hit) &&
                           hit.entity != e && hit.normal.y > .55f;
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
            if (!fps.mouseLook) {
                fps.initialized = false;
            }
            if (fps.mouseLook && !fps.initialized) {
                auto angles = rotationToEuler(local.rotation);
                fps.yaw = angles.y;
                fps.pitch = angles.x;
                fps.initialized = true;
            }
            if (input.enabled && input.mouseEnabled && input.mouseLocked && fps.mouseLook) {
                fps.yaw -= input.dx * fps.sensitivity;
                fps.pitch = std::clamp(fps.pitch - input.dy * fps.sensitivity, -89.f, 89.f);
            }
            const Entity parent = getParent(world, e);
            if (fps.mouseLook && parent != Entity{} && world.has<CharacterController>(parent)) {
                auto& character = world.get<Transform>(parent);
                // Camera yaw is transferred to the upright character; local pitch remains
                // independent.
                character.rotation =
                    composeRotation(character.rotation, axisAngle({0, 1, 0}, fps.yaw * kPi / 180));
                fps.yaw = 0;
                updateHierarchy(world);
                local = localTransform(world, e);
            }
            if (fps.mouseLook) {
                local.rotation = rotationFromEuler({fps.pitch, fps.yaw, 0});
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
