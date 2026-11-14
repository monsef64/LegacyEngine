#pragma once
#include "core/components.hpp"

// An explicit collision asset overrides the visual mesh. Otherwise a Mesh
// collider follows the model rendered by this same object (not an arbitrary child).
inline Collider effectiveCollider(World& world, Entity entity, const Collider& source) {
    auto result = source;
    if (result.shape == ColliderShape::Mesh && result.model.empty()) {
        if (const auto* renderer = world.tryGet<MeshRenderer>(entity)) {
            result.model = renderer->model;
        }
    }
    return result;
}
