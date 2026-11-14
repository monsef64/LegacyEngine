#pragma once
#include "core/components.hpp"
#include "core/hierarchy.hpp"
#include <algorithm>
#include <array>
#include <string>

using AnimationValue = std::array<float, 4>;
enum class AnimationValueKind { Number, Vector, Quaternion, Boolean };
struct AnimationProperty {
    const char* name;
    AnimationValueKind kind;
    float minimum = -1000000, maximum = 1000000;
    int lanes() const {
        return kind == AnimationValueKind::Vector       ? 3
               : kind == AnimationValueKind::Quaternion ? 4
                                                        : 1;
    }
};
// Explicit allowlist: paths/IDs, collider shape enums and object references are not interpolated.
inline const AnimationProperty animationProperties[]{
#define NUMBER(name, lo, hi) {name, AnimationValueKind::Number, lo, hi}
#define VECTOR(name, lo, hi) {name, AnimationValueKind::Vector, lo, hi}
#define BOOL(name) {name, AnimationValueKind::Boolean, 0, 1}
    VECTOR("Transform.position", -1000000, 1000000),
    VECTOR("Transform.scale", .05f, 100),
    {"Transform.rotation", AnimationValueKind::Quaternion, -1, 1},
    BOOL("GameObject.enabled"),
    VECTOR("MeshRenderer.color", 0, 1),
    NUMBER("MeshRenderer.specular", 0, 1),
    BOOL("MeshRenderer.checkerboard"),
    VECTOR("Collider.size", .001f, 10000),
    VECTOR("Collider.center", -1000000, 1000000),
    NUMBER("Collider.radius", .001f, 10000),
    NUMBER("Collider.height", .001f, 10000),
    NUMBER("Collider.friction", 0, 1),
    NUMBER("Collider.bounciness", 0, 1),
    NUMBER("RigidBody.mass", 0, 1000000),
    NUMBER("RigidBody.drag", 0, 100),
    NUMBER("RigidBody.angularDrag", 0, 100),
    VECTOR("RigidBody.linearVelocity", -1000000, 1000000),
    VECTOR("RigidBody.angularVelocity", -1000000, 1000000),
    BOOL("RigidBody.useGravity"),
    NUMBER("Camera.fieldOfView", 1, 179),
    NUMBER("Camera.orthographicSize", .01f, 10000),
    BOOL("Camera.active"),
    NUMBER("AudioSource.volume", 0, 1),
    NUMBER("AudioSource.pitch", .01f, 4),
    NUMBER("AudioSource.pan", -1, 1),
    BOOL("AudioSource.muted"),
    BOOL("AudioListener.enabled"),
#define LIGHT(type)                                                                                \
    VECTOR(#type ".color", 0, 100), NUMBER(#type ".intensity", 0, 100),                            \
        NUMBER(#type ".range", .01f, 10000), BOOL(#type ".enabled")
    LIGHT(DirectionalLight),
    LIGHT(PointLight),
    LIGHT(SpotLight),
    VECTOR("Skybox.color", 0, 100),
    BOOL("Skybox.enabled"),
    NUMBER("CharacterController.speed", 0, 1000),
    NUMBER("CharacterController.jumpSpeed", 0, 1000),
    BOOL("CharacterController.canMove"),
    BOOL("CharacterController.canJump"),
    BOOL("CharacterController.canSprint"),
    NUMBER("FPSCamera.sensitivity", 0, 1000),
    NUMBER("FPSCamera.speed", 0, 1000),
    BOOL("FPSCamera.mouseLook"),
    NUMBER("Raycast.distance", .001f, 1000000),
    NUMBER("Raycast.radius", .001f, 1000000),
    NUMBER("Raycast.angle", 0, 360),
    BOOL("Raycast.includeTriggers")
#undef LIGHT
#undef BOOL
#undef VECTOR
#undef NUMBER
};
inline const AnimationProperty* animationProperty(const std::string& name) {
    for (const auto& property : animationProperties) {
        if (name == property.name) {
            return &property;
        }
    }
    return nullptr;
}
inline void* animationPropertyPointer(World& world, Entity entity, const std::string& property) {
    if (!world.alive(entity)) {
        return nullptr;
    }
#define FIELD(type, name)                                                                          \
    if (property == #type "." #name) {                                                             \
        auto* c = world.tryGet<type>(entity);                                                      \
        return c ? static_cast<void*>(&c->name) : nullptr;                                         \
    }
    FIELD(Transform, position)
    FIELD(Transform, scale) FIELD(Transform, rotation) if (property == "GameObject.enabled") {
        if (!world.has<ObjectActivity>(entity)) {
            world.add(entity, ObjectActivity{});
        }
        return &world.get<ObjectActivity>(entity).enabled;
    }
    FIELD(MeshRenderer, color)
    FIELD(MeshRenderer, specular)
    FIELD(MeshRenderer, checkerboard)
    FIELD(Collider, size)
    FIELD(Collider, center)
    FIELD(Collider, radius)
    FIELD(Collider, height)
    FIELD(Collider, friction)
    FIELD(Collider, bounciness)
    FIELD(RigidBody, mass) FIELD(RigidBody, drag) FIELD(RigidBody, angularDrag)
        FIELD(RigidBody, linearVelocity) FIELD(RigidBody, angularVelocity)
            FIELD(RigidBody, useGravity) FIELD(Camera, fieldOfView) FIELD(Camera, orthographicSize)
                FIELD(Camera, active) FIELD(AudioSource, volume) FIELD(AudioSource, pitch)
                    FIELD(AudioSource, pan) FIELD(AudioSource, muted) FIELD(AudioListener, enabled)
#define LIGHT(type)                                                                                \
    FIELD(type, color) FIELD(type, intensity) FIELD(type, range) FIELD(type, enabled)
                        LIGHT(DirectionalLight) LIGHT(PointLight) LIGHT(SpotLight)
#undef LIGHT
                            FIELD(Skybox, color) FIELD(Skybox, enabled) FIELD(
                                CharacterController, speed) FIELD(CharacterController, jumpSpeed)
                                FIELD(CharacterController, canMove)
                                    FIELD(CharacterController, canJump)
                                        FIELD(CharacterController, canSprint)
                                            FIELD(FPSCamera, sensitivity) FIELD(FPSCamera, speed)
                                                FIELD(FPSCamera, mouseLook) FIELD(Raycast, distance)
                                                    FIELD(Raycast, radius) FIELD(Raycast, angle)
                                                        FIELD(Raycast, includeTriggers)
#undef FIELD
                                                            return nullptr;
}
inline bool readAnimationValue(World& world,
                               Entity entity,
                               const AnimationProperty& property,
                               AnimationValue& value) {
    auto* pointer = animationPropertyPointer(world, entity, property.name);
    if (!pointer) {
        return false;
    }
    value = {};
    if (property.name == std::string("Transform.position") ||
        property.name == std::string("Transform.scale") ||
        property.name == std::string("Transform.rotation")) {
        updateHierarchy(world);
        const auto pose = localTransform(world, entity);
        if (property.name == std::string("Transform.position")) {
            value = {pose.position.x, pose.position.y, pose.position.z, 0};
        } else if (property.name == std::string("Transform.scale")) {
            value = {pose.scale.x, pose.scale.y, pose.scale.z, 0};
        } else {
            value = {pose.rotation.x, pose.rotation.y, pose.rotation.z, pose.rotation.w};
        }
        return true;
    }
    if (property.kind == AnimationValueKind::Boolean) {
        value[0] = *static_cast<bool*>(pointer) ? 1.f : 0.f;
    } else if (property.kind == AnimationValueKind::Vector) {
        const auto v = *static_cast<Vec3*>(pointer);
        value = {v.x, v.y, v.z, 0};
    } else if (property.kind == AnimationValueKind::Quaternion) {
        const auto q = *static_cast<Quat*>(pointer);
        value = {q.x, q.y, q.z, q.w};
    } else {
        value[0] = *static_cast<float*>(pointer);
    }
    return true;
}
inline bool writeAnimationValue(World& world,
                                Entity entity,
                                const AnimationProperty& property,
                                AnimationValue value) {
    for (auto& v : value) {
        if (!std::isfinite(v)) {
            return false;
        }
        v = std::clamp(v, property.minimum, property.maximum);
    }
    auto* pointer = animationPropertyPointer(world, entity, property.name);
    if (!pointer) {
        return false;
    }
    if (property.name == std::string("Transform.position") ||
        property.name == std::string("Transform.scale") ||
        property.name == std::string("Transform.rotation")) {
        updateHierarchy(world);
        auto pose = localTransform(world, entity);
        if (property.name == std::string("Transform.position")) {
            pose.position = {value[0], value[1], value[2]};
        } else if (property.name == std::string("Transform.scale")) {
            pose.scale = {value[0], value[1], value[2]};
        } else {
            pose.rotation = normalize(Quat{value[0], value[1], value[2], value[3]});
        }
        setLocalTransform(world, entity, pose);
        updateHierarchy(world);
        return true;
    }
    if (property.kind == AnimationValueKind::Boolean) {
        *static_cast<bool*>(pointer) = value[0] >= .5f;
    } else if (property.kind == AnimationValueKind::Vector) {
        *static_cast<Vec3*>(pointer) = {value[0], value[1], value[2]};
    } else if (property.kind == AnimationValueKind::Quaternion) {
        *static_cast<Quat*>(pointer) = normalize(Quat{value[0], value[1], value[2], value[3]});
    } else {
        *static_cast<float*>(pointer) = value[0];
    }
    return true;
}
