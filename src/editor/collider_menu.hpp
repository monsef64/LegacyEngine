#pragma once
#include "core/components.hpp"
#include "core/ecs.hpp"

// Collider types share one component and are selected via its shape field.
// Refuse replacement so adding a component cannot destroy existing settings.
inline bool addColliderShape(World& world, Entity entity, ColliderShape shape) {
    if (!world.alive(entity) || world.has<Collider>(entity)) {
        return false;
    }
    switch (shape) {
    case ColliderShape::Box:
    case ColliderShape::Sphere:
    case ColliderShape::Capsule:
    case ColliderShape::Cone:
    case ColliderShape::Mesh:
        break;
    default:
        return false;
    }
    Collider collider;
    collider.shape = shape;
    world.add(entity, collider);
    return true;
}
