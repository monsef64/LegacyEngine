#include "editor/collider_menu.hpp"
#include <cassert>

int main() {
    World world;
    const ColliderShape shapes[]{ColliderShape::Box,
                                 ColliderShape::Sphere,
                                 ColliderShape::Capsule,
                                 ColliderShape::Cone,
                                 ColliderShape::Mesh};
    for (const auto shape : shapes) {
        const auto entity = world.create();
        assert(addColliderShape(world, entity, shape));
        auto& collider = world.get<Collider>(entity);
        assert(collider.shape == shape);
        collider.friction = .25f;
        assert(!addColliderShape(world, entity, ColliderShape::Box));
        assert(world.get<Collider>(entity).shape == shape);
        assert(world.get<Collider>(entity).friction == .25f);
    }
    const auto entity = world.create();
    assert(!addColliderShape(world, entity, static_cast<ColliderShape>(-1)));
    assert(!world.has<Collider>(entity));
    world.destroy(entity);
    assert(!addColliderShape(world, entity, ColliderShape::Box));
}
