#include "components.hpp"
#include "physics.hpp"
#include "collider_debug.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

static void require(bool ok, const char* message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

static Entity object(World& world, Vec3 position, ColliderShape shape)
{
    Entity e = world.create();
    world.add(e, Transform{position});
    Collider c;
    c.shape = shape;
    world.add(e, c);
    return e;
}

int main()
{
    // Selection outlines use collider dimensions, scaled offsets and curved geometry.
    for (const auto shape : {ColliderShape::Box, ColliderShape::Sphere, ColliderShape::Capsule}) {
        Collider c;
        c.shape = shape;
        c.center = {1, 2, -1};
        const Vec3 scale{2, 3, 4};
        const auto wire = colliderWireframe(c, scale);
        require(wire.topology == MeshData::Topology::Lines, "debug outline uses lines");
        require(!wire.vertices.empty() && wire.vertices.size() % 2 == 0, "complete line segments");
        if (shape == ColliderShape::Box) require(wire.vertices.size() == 24, "box has twelve edges");
        Vec3 lo{1e6f, 1e6f, 1e6f}, hi{-1e6f, -1e6f, -1e6f};
        const Vec3 center = c.center * scale;
        for (const auto& v : wire.vertices) {
            const Vec3 p = v.position-center;
            lo = {std::min(lo.x,p.x), std::min(lo.y,p.y), std::min(lo.z,p.z)};
            hi = {std::max(hi.x,p.x), std::max(hi.y,p.y), std::max(hi.z,p.z)};
            if (shape != ColliderShape::Box) {
                Vec3 radial = p;
                if (shape == ColliderShape::Capsule) radial.y -= std::clamp(radial.y, -1.5f, 1.5f);
                require(std::abs(dot(radial, radial)-4.0f) < 1e-4f, "wire lies on actual round collider");
            }
        }
        const Vec3 half = shape == ColliderShape::Box ? Vec3{1, 1.5f, 2} :
            (shape == ColliderShape::Sphere ? Vec3{2,2,2} : Vec3{2,3.5f,2});
        require(dot(hi-half, hi-half) < 1e-6f && dot(lo+half, lo+half) < 1e-6f,
                "wire bounds match scaled native collider including offset");
    }
    // Mesh winding, radius, and capsule section dimensions agree with native colliders.
    for (bool capsule : {false, true}) {
        const auto mesh = capsule ? makeCapsule(0.5f, 1.0f) : makeSphere(0.5f);
        require(!mesh.vertices.empty(), "mesh is not empty");
        for (const auto& v : mesh.vertices) {
            Vec3 radial = v.position;
            if (capsule) radial.y -= v.position.y < 0 ? -0.5f : 0.5f;
            require(std::abs(dot(radial, radial) - 0.25f) < 1e-5f, "vertices lie on round surface");
            require(dot(radial, v.normal) > 0.49f, "smooth normals point outwards");
        }
        for (std::size_t i = 0; i < mesh.vertices.size(); i += 3) {
            const auto a = mesh.vertices[i], b = mesh.vertices[i+1], c = mesh.vertices[i+2];
            const Vec3 n = cross(b.position-a.position, c.position-a.position);
            require(dot(n, a.normal+b.normal+c.normal) >= -1e-7f, "triangle winding faces out");
        }
    }
    for (const auto shape : {ColliderShape::Box, ColliderShape::Sphere, ColliderShape::Capsule}) {
        World world;
        Physics physics;
        const Entity e = object(world, {0, 2, 0}, shape);
        physics.update(world, 0);
        RayHit hit;
        require(physics.raycast({{0, 5, 0}, {0, -1, 0}}, 10, hit), "vertical ray hits");
        const float top = shape == ColliderShape::Capsule ? 3.0f : 2.5f;
        require(hit.entity == e && std::abs(hit.point.y-top) < 1e-4f, "top matches graphic");
        require(hit.normal.y > 0.99f, "hit normal points up");
        require(physics.checkBox({0, 2, 0}, {0.1f, 0.1f, 0.1f}), "inside overlaps");
        require(!physics.checkBox({2, 2, 0}, {0.1f, 0.1f, 0.1f}), "outside does not overlap");
        if (shape != ColliderShape::Box) {
            require(!physics.raycast({{0.49f, 5, 0.49f}, {0, -1, 0}}, 10, hit), "round shape is not a box raycast");
            require(!physics.checkBox({0.47f, 2, 0.47f}, {0.02f, 0.02f, 0.02f}), "round shape is not a box overlap");
        }
        if (shape == ColliderShape::Capsule) {
            world.get<Transform>(e).rotation = axisAngle({0, 0, 1}, kPi * 0.5f);
            physics.update(world, 0);
            require(physics.raycast({{3, 2, 0}, {-1, 0, 0}}, 10, hit), "rotated capsule hit");
            require(std::abs(hit.point.x-1) < 1e-4f, "rotated capsule end matches mesh");
            world.get<Transform>(e).rotation = {};
        }
        Entity floor = object(world, {0, -0.5f, 0}, ColliderShape::Box);
        world.get<Collider>(floor).size = {10, 1, 10};
        world.add(e, RigidBody{});
        for (int i=0; i<600; ++i) physics.update(world, 1.0f/60);
        require(std::abs(world.get<Transform>(e).position.y - (top-2)) < 0.04f, "native collider rests on floor at correct height");
        // Changing type and size rebuilds safely, even with a sleeping body.
        world.get<Collider>(e).shape = ColliderShape::Sphere;
        world.get<Collider>(e).radius = 0.75f;
        world.get<Transform>(e).position = {0, 4, 0};
        physics.update(world, 0);
        require(physics.raycast({{0, 6, 0}, {0, -1, 0}}, 10, hit), "changed shape hit");
        require(std::abs(hit.point.y-4.75f) < 1e-4f, "changed radius rebuilds");
        world.destroy(e);
        physics.update(world, 0);
    }
    std::puts("Shape geometry, picking, overlap, rotation, settling and lifecycle tests passed");
}
