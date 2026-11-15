#include "core/components.hpp"
#include "physics/physics.hpp"
#include "project/json_io.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
void ck(bool v, const char* m) {
    if (!v) {
        std::fprintf(stderr, "FAIL: %s\n", m);
        std::exit(1);
    }
}
Entity box(World& w, Vec3 p) {
    auto e = w.create();
    w.add(e, Transform{p});
    w.add(e, Collider{});
    return e;
}
bool ev(const std::vector<Physics::TriggerEvent>& evs, Entity a, Entity b, int phase) {
    for (const auto& e : evs) {
        if (e.receiver == a && e.other == b && e.phase == phase) {
            return true;
        }
    }
    return false;
}
int main() {
    {
        World w;
        Physics p;
        auto a = box(w, {0, 0, 0});
        auto b = box(w, {.25f, 0, 0});
        w.get<Collider>(a).isTrigger = true;
        p.update(w, 1.f / 60);
        ck(ev(p.triggerEvents, a, b, 0) && ev(p.triggerEvents, b, a, 0), "static trigger enter");
        p.update(w, 1.f / 60);
        ck(ev(p.triggerEvents, a, b, 1), "static trigger stay");
        w.get<Transform>(b).position = {5, 0, 0};
        p.update(w, 1.f / 60);
        ck(ev(p.triggerEvents, a, b, 2), "static trigger exit");
    }
    {
        World w;
        Physics p;
        auto a = box(w, {0, 0, 0});
        auto b = box(w, {0, .9f, 0});
        RigidBody rb;
        rb.useGravity = false;
        w.add(b, rb);
        p.update(w, 1.f / 60);
        ck(ev(p.collisionEvents, a, b, 0), "collision enter");
        p.update(w, 1.f / 60);
        ck(ev(p.collisionEvents, a, b, 1), "collision stay");
        w.get<Transform>(b).position = {0, 5, 0};
        p.update(w, 1.f / 60);
        ck(ev(p.collisionEvents, a, b, 2), "collision exit");
    }
    {
        World w;
        Physics p;
        auto a = box(w, {0, 0, 0});
        auto b = box(w, {0, .9f, 0});
        RigidBody rb;
        rb.useGravity = false;
        w.add(b, rb);
        p.update(w, 1.f / 60);
        ck(ev(p.collisionEvents, a, b, 0), "contact before collider rebuild");
        w.get<Collider>(a).size = {2, 1, 2};
        p.update(w, 1.f / 60);
        ck(!ev(p.collisionEvents, a, b, 2), "rebuilding an overlapping collider does not exit");
        w.get<Collider>(a).isTrigger = true;
        p.update(w, 1.f / 60);
        ck(ev(p.collisionEvents, a, b, 2) && ev(p.triggerEvents, a, b, 0),
           "switching a solid to a trigger exits the contact and enters the trigger");
    }
    {
        World w;
        Physics p;
        auto a = box(w, {0, 0, 0});
        auto b = box(w, {0, 0, -2});
        p.update(w, 0);
        auto hits = p.raycastAll({{0, 0, 2}, {0, 0, -1}}, 10);
        ck(hits.size() == 2 && hits[0].entity == a && hits[1].entity == b, "raycast all ordering");
        RayHit hit;
        ck(p.raycast({{0, 0, 2}, {0, 0, -1}}, 10, hit, [&](Entity e) { return e == b; }) &&
               hit.entity == b,
           "raycast filter");
        ck(!p.raycast({{0, 0, 0}, {0, 1, 0}}, 10, hit), "ray origin inside rejected");
        auto overlaps = p.overlapSphere({0, 0, 0}, .1f);
        ck(overlaps.size() == 1 && overlaps[0].entity == a && overlaps[0].distance == 0,
           "overlap sphere inside reports origin");
    }
    {
        World w;
        Physics p;
        auto e = w.create();
        w.add(e, Transform{});
        RigidBody rb;
        rb.useGravity = false;
        rb.mass = 2;
        rb.impulse = {4, 0, 0};
        w.add(e, rb);
        p.update(w, 1.f / 60);
        ck(std::abs(w.get<RigidBody>(e).linearVelocity.x - 2) < .01f, "impulse with no collider");
        ck(w.get<RigidBody>(e).impulse == Vec3{}, "impulse consumed");
        w.get<RigidBody>(e).force = {120, 0, 0};
        p.update(w, 1.f / 60);
        ck(std::abs(w.get<RigidBody>(e).linearVelocity.x - 3) < .02f, "force and mass");
        w.get<RigidBody>(e).mass = 4;
        w.get<RigidBody>(e).impulse = {4, 0, 0};
        p.update(w, 1.f / 60);
        ck(std::abs(w.get<RigidBody>(e).linearVelocity.x - 4) < .03f, "changed mass");
        w.get<RigidBody>(e).isKinematic = true;
        w.get<RigidBody>(e).linearVelocity = {3, 0, 0};
        p.update(w, 1.f / 60);
        p.update(w, 1.f / 60);
        ck(w.get<Transform>(e).position.x > .05f, "kinematic moves");
    }
    {
        World w;
        Physics p;
        auto e = box(w, {0, 0, 0});
        auto& t = w.get<Transform>(e);
        t.scale = {2, 1, 1};
        w.get<Collider>(e).center = {1, 0, 0};
        p.update(w, 0);
        RayHit hit;
        ck(p.raycast({{2, 3, 0}, {0, -1, 0}}, 10, hit) && hit.entity == e,
           "scaled collider offset");
        t.position = {std::numeric_limits<float>::quiet_NaN(), 0, 0};
        p.update(w, 0);
        ck(!p.raycast({{2, 3, 0}, {0, -1, 0}}, 10, hit), "invalid removed");
        t.position = {0, 0, 0};
        p.update(w, 0);
        ck(p.raycast({{2, 3, 0}, {0, -1, 0}}, 10, hit), "invalid recovered");
    }

    {
        const auto root =
            std::filesystem::temp_directory_path() /
            ("legacy-physics-fixture-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root);
        writeJsonFile(
            root / "floor.lemesh",
            Json::parse(
                R"({"format":"LegacyEngineMesh","version":1,"vertices":[[-5,0,-5,0,1,0,0,0],[-5,0,5,0,1,0,0,0],[5,0,-5,0,1,0,0,0],[5,0,-5,0,1,0,0,0],[-5,0,5,0,1,0,0,0],[5,0,5,0,1,0,0,0]]})"));
        World w;
        Physics p;
        p.setAssetRoot(root);
        auto ground = box(w, {0, 0, 0});
        auto& c = w.get<Collider>(ground);
        c.shape = ColliderShape::Mesh;
        c.model = "floor.lemesh";
        c.meshDetail = 15;
        p.update(w, 0);
        RayHit hit;
        ck(p.raycast({{0, 3, 0}, {0, -1, 0}}, 10, hit) && hit.entity == ground &&
               std::abs(hit.point.y) < .001f,
           "mesh raycast");
        ck(p.checkBox({0, 0, 0}, {.2f, .2f, .2f}), "mesh overlap");
        auto ball = box(w, {0, 2, 0});
        w.get<Collider>(ball).shape = ColliderShape::Sphere;
        w.add(ball, RigidBody{});
        bool collided = false;
        int exits = 0;
        for (int i = 0; i < 240; ++i) {
            p.update(w, 1.f / 60);
            collided |= ev(p.collisionEvents, ground, ball, 0);
            exits += ev(p.collisionEvents, ground, ball, 2);
        }
        ck(collided, "mesh collision event");
        ck(exits == 0, "resting across mesh triangles does not spuriously exit");
        ck(std::abs(w.get<Transform>(ball).position.y - .5f) < .06f, "mesh supports sphere");
        w.get<Collider>(ground).isTrigger = true;
        w.get<Transform>(ball).position = {0, .1f, 0};
        w.remove<RigidBody>(ball);
        p.update(w, 1.f / 60);
        ck(ev(p.triggerEvents, ground, ball, 0), "mesh trigger overlap");
        const auto meshCollider = w.get<Collider>(ground);
        auto secondMesh = box(w, {0, 0, 0});
        w.get<Collider>(secondMesh) = meshCollider;
        // Jolt has no mesh-vs-mesh collision dispatch. It must not assert,
        // and supported convex overlaps must still be reported.
        p.update(w, 1.f / 60);
        ck(ev(p.triggerEvents, ground, ball, 1), "mesh trigger remains active near another mesh");
        p.reset();
        p.setAssetRoot(root);
        p.update(w, 1.f / 60);
        ck(ev(p.triggerEvents, ground, ball, 0), "reset allows new trigger enter");
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }
    {
        World w;
        Physics p;
        auto floor = box(w, {0, -.5f, 0});
        w.get<Collider>(floor).size = {10, 1, 10};
        auto body = box(w, {0, 2, 0});
        w.add(body, RigidBody{});
        bool entered = false;
        int exits = 0;
        for (int i = 0; i < 300; ++i) {
            p.update(w, 1.f / 60);
            entered |= ev(p.collisionEvents, body, floor, 0);
            exits += ev(p.collisionEvents, body, floor, 2);
        }
        ck(entered && exits == 0, "resting contact must not exit when solver goes to sleep");
        // Waking and separating a sleeping contact must emit an exit.
        w.get<Transform>(body).position = {0, 4, 0};
        p.update(w, 1.f / 60);
        ck(ev(p.collisionEvents, body, floor, 2), "moving a sleeping body emits contact exit");
        // Let it settle again, then remove the support.
        for (int i = 0; i < 300; ++i) {
            p.update(w, 1.f / 60);
        }
        w.destroy(floor);
        bool exited = false;
        for (int i = 0; i < 5; ++i) {
            p.update(w, 1.f / 60);
            exited |= ev(p.collisionEvents, body, floor, 2);
        }
        ck(exited, "removing the sleeping body's support emits contact exit");
    }
    {
        World w1, w2;
        Physics p1, p2;
        auto a = box(w1, {0, 0, 0});
        auto b = box(w2, {10, 0, 0});
        p1.update(w1, 0);
        p2.update(w2, 0);
        RayHit hit;
        ck(p1.raycast({{0, 2, 0}, {0, -1, 0}}, 5, hit) && hit.entity == a,
           "multiple physics systems first");
        ck(p2.raycast({{10, 2, 0}, {0, -1, 0}}, 5, hit) && hit.entity == b,
           "multiple physics systems second");
    }
    std::puts("physics regression passed");
}
