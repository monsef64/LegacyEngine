// Jolt must precede every other Jolt header.
#include <Jolt/Jolt.h>

#include "effects/environment.hpp"
#include "effects/water_mesh.hpp"
#include "models/collider_source.hpp"
#include "models/mesh_simplify.hpp"
#include "models/model_asset.hpp"
#include "physics/physics.hpp"
#include "project/layers.hpp"
#include "terrain/terrain_mesh.hpp"
#include <set>
#include <tuple>

#include "core/components.hpp"
#include "core/hierarchy.hpp"
#include "core/worker_pool.hpp"

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/EmptyShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <unordered_map>

namespace {

// The simulation only ever advances in steps of this length. Steps of equal
// length keep it stable and make it behave the same on a fast and on a slow
// computer.
constexpr float kStepSeconds = 1.0f / 60.0f;

// A frame that took longer than this (the window was being dragged, say) counts
// as this long, so that the simulation does not try to catch up all at once.
constexpr float kLongestFrameSeconds = 0.25f;

// Jolt needs boxes with some thickness on every axis.
constexpr float kSmallestBoxSize = 0.001f;

// A mass too close to zero cannot be calculated with. Zero itself is fine: it
// makes a body that gravity and collisions do not move.
constexpr float kSmallestMass = 1.0e-6f;

// The simulation calculates with floats. Beyond this size (in metres, metres
// per second, kilograms and so on) they are too coarse for it, and much further
// out its calculations overflow.
constexpr float kLargestNumber = 1.0e6f;

struct Pose {
    Vec3 position;
    Quat rotation;
};

JPH::Vec3 jvec(Vec3 v) {
    return {v.x, v.y, v.z};
}
Vec3 vec(JPH::Vec3Arg v) {
    return {v.GetX(), v.GetY(), v.GetZ()};
}
JPH::Quat jquat(Quat q) {
    q = normalize(q);
    return {q.x, q.y, q.z, q.w};
}
Quat quat(JPH::QuatArg q) {
    return {q.GetX(), q.GetY(), q.GetZ(), q.GetW()};
}

// Jolt only accepts rotations of length 1.
Pose toPose(const Transform& transform) {
    return {transform.position, normalize(transform.rotation)};
}

// A BoxCollider as the simulation sees it: with the entity's scale applied and
// with values it can work with.
struct Box {
    std::string model;
    Vec3 hullScale{1, 1, 1};
    std::uint64_t revision = 0;
    int meshDetail = 8;
    ColliderShape shape = ColliderShape::Box;
    float radius = 0.5f, height = 1.0f;
    Vec3 halfSize;
    Vec3 center;
    float friction = 0.0f;
    float bounciness = 0.0f;
};

Box toBox(const BoxCollider& collider, Vec3 scale) {
    const Vec3 size = collider.size * scale;
    const float smallest = kSmallestBoxSize * 0.5f;

    Box box;
    box.halfSize = {std::max(std::abs(size.x) * 0.5f, smallest),
                    std::max(std::abs(size.y) * 0.5f, smallest),
                    std::max(std::abs(size.z) * 0.5f, smallest)};
    box.shape = collider.shape;
    if (box.shape == ColliderShape::Mesh) {
        box.model = collider.model;
        box.meshDetail = std::clamp(collider.meshDetail, 2, 15);
        box.hullScale = scale;
        box.revision = modelAssetRevision;
    }
    const float radialScale =
        collider.shape == ColliderShape::Sphere
            ? std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)})
            : std::max(std::abs(scale.x), std::abs(scale.z));
    box.radius = std::max(std::abs(collider.radius) * radialScale, smallest);
    if (collider.shape == ColliderShape::Sphere) {
        box.halfSize = {std::max(std::abs(collider.radius * scale.x), smallest),
                        std::max(std::abs(collider.radius * scale.y), smallest),
                        std::max(std::abs(collider.radius * scale.z), smallest)};
    }
    box.height = std::max(std::abs(collider.height * scale.y), kSmallestBoxSize);
    box.center = collider.center * scale;
    box.friction = std::max(collider.friction, 0.0f);
    box.bounciness = std::clamp(collider.bounciness, 0.0f, 1.0f);
    return box;
}

// Whether the simulation can work with a number: false for a NaN, for an
// infinity and for a number beyond kLargestNumber.
bool isUsable(float value) {
    return std::abs(value) <= kLargestNumber;
}
bool isUsable(Vec3 v) {
    return isUsable(v.x) && isUsable(v.y) && isUsable(v.z);
}
bool isUsable(Quat q) {
    return isUsable(Vec3{q.x, q.y, q.z}) && isUsable(q.w);
}
bool isUsable(const Pose& pose) {
    return isUsable(pose.position) && isUsable(pose.rotation);
}

// Whether the simulation can work with the components of an entity. One number
// that is not usable (the result of dividing by zero, say) would spread to
// every body the entity touches.
bool isUsable(const Transform& transform, const BoxCollider* collider, const RigidBody* rigidBody) {
    if (!isUsable(transform.position) || !isUsable(transform.rotation) ||
        !isUsable(transform.scale)) {
        return false;
    }
    if (collider &&
        (!isUsable(collider->size) || !isUsable(collider->center) ||
         !isUsable(collider->size * transform.scale) ||
         !isUsable(collider->center * transform.scale) || !isUsable(collider->friction) ||
         !isUsable(collider->bounciness) || !isUsable(collider->radius) ||
         !isUsable(collider->height) || !isUsable(transform.scale * collider->radius) ||
         !isUsable(collider->height * transform.scale.y))) {
        return false;
    }
    if (rigidBody &&
        (!isUsable(rigidBody->mass) || !isUsable(rigidBody->linearVelocity) ||
         !isUsable(rigidBody->angularVelocity) || !isUsable(rigidBody->drag) ||
         !isUsable(rigidBody->angularDrag) || rigidBody->drag < 0 || rigidBody->angularDrag < 0)) {
        return false;
    }
    return true;
}

std::uint64_t keyOf(Entity entity) {
    return (static_cast<std::uint64_t>(entity.generation) << 32) | entity.index;
}

} // namespace

struct Physics::Impl {
    struct Runtime {
        Runtime() {
            JPH::RegisterDefaultAllocator();
            JPH::Factory::sInstance = new JPH::Factory;
            JPH::RegisterTypes();
        }
        ~Runtime() {
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }
    };
    static Runtime& runtime() {
        static Runtime value;
        return value;
    }
    Runtime& runtime_ = runtime();
    struct BroadLayers : JPH::BroadPhaseLayerInterface {
        JPH::uint GetNumBroadPhaseLayers() const override {
            return 2;
        }
        JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
            return JPH::BroadPhaseLayer(layer & 1);
        }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
        const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
            return layer.GetValue() ? "Moving" : "Static";
        }
#endif
    } broad;
    struct BroadFilter : JPH::ObjectVsBroadPhaseLayerFilter {
        bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer other) const override {
            return (layer & 1) != 0 || other.GetValue() != 0;
        }
    } broadFilter;
    struct PairFilter : JPH::ObjectLayerPairFilter {
        bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
            return ((a & 1) != 0 || (b & 1) != 0) && LayerSettings::collide(a >> 1, b >> 1);
        }
    } pairFilter;
    struct Pair {
        Entity a, b;
        bool sensor;
    };
    using ContactKey = std::tuple<unsigned, unsigned, unsigned, unsigned>;
    struct Contacts : JPH::ContactListener {
        std::mutex mutex;
        std::map<ContactKey, Pair> live;
        std::set<ContactKey> removed;
        static ContactKey key(const JPH::SubShapeIDPair& pair) {
            return {pair.GetBody1ID().GetIndexAndSequenceNumber(),
                    pair.GetBody2ID().GetIndexAndSequenceNumber(),
                    pair.GetSubShapeID1().GetValue(),
                    pair.GetSubShapeID2().GetValue()};
        }
        static Entity entity(JPH::uint64 value) {
            return {unsigned(value), unsigned(value >> 32)};
        }
        void OnContactAdded(const JPH::Body& a,
                            const JPH::Body& b,
                            const JPH::ContactManifold& manifold,
                            JPH::ContactSettings&) override {
            std::lock_guard<std::mutex> lock(mutex);
            removed.erase({a.GetID().GetIndexAndSequenceNumber(),
                           b.GetID().GetIndexAndSequenceNumber(),
                           manifold.mSubShapeID1.GetValue(),
                           manifold.mSubShapeID2.GetValue()});
            live[{a.GetID().GetIndexAndSequenceNumber(),
                  b.GetID().GetIndexAndSequenceNumber(),
                  manifold.mSubShapeID1.GetValue(),
                  manifold.mSubShapeID2.GetValue()}] = {
                entity(a.GetUserData()), entity(b.GetUserData()), a.IsSensor() || b.IsSensor()};
        }
        void OnContactPersisted(const JPH::Body& a,
                                const JPH::Body& b,
                                const JPH::ContactManifold& m,
                                JPH::ContactSettings& settings) override {
            OnContactAdded(a, b, m, settings);
        }
        void OnContactRemoved(const JPH::SubShapeIDPair& pair) override {
            std::lock_guard<std::mutex> lock(mutex);
            removed.insert(key(pair));
        }
        void erase(JPH::BodyID id) {
            for (auto it = live.begin(); it != live.end();) {
                if (std::get<0>(it->first) == id.GetIndexAndSequenceNumber() ||
                    std::get<1>(it->first) == id.GetIndexAndSequenceNumber()) {
                    it = live.erase(it);
                } else {
                    ++it;
                }
            }
        }
    } contacts;
    JPH::TempAllocatorImpl allocator{32 * 1024 * 1024};
    // Jolt blocks until these jobs complete before ECS synchronization/event
    // publication. Contacts serialize callbacks; workers never access the ECS.
    JPH::JobSystemThreadPool jobs{2048, 8, int(backgroundWorkerCount())};
    JPH::PhysicsSystem system;
    struct Body {
        Entity entity;
        JPH::BodyID id;
        Box box;
        RigidBody rigid;
        Vec3 centerOfMass;
        bool hasCollider = false, hasRigid = false, sensor = false, controller = false,
             visited = false, failed = false;
        Pose previous{}, current{};
        Transform shown{};
        JPH::EMotionType motion = JPH::EMotionType::Static;
    };
    std::map<std::uint64_t, Body> bodies;
    std::map<std::pair<std::uint64_t, std::uint64_t>, Pair> pairs;
    AssetManager assets;
    float accumulated = 0;
    double waterTime = 0;
    bool needsWake_ = false;
    Impl() {
        system.Init(16384, 0, 65536, 16384, broad, broadFilter, pairFilter);
        system.SetContactListener(&contacts);
        system.SetGravity({0, -9.81f, 0});
    }
    struct WaterPatch {
        Vec3 a, along, side, normal;
        float aa, ss, as, inverseDet, flow, wave;
        float waveLength = 4, wavePhaseSpeed = 0;
        bool triangle = false;
    };
    std::vector<WaterPatch> waterPatches(World& world) {
        std::vector<WaterPatch> patches;
        world.eachReadOnly<Transform, River>([&](Entity e, const Transform& t, const River& r) {
            if (!r.enabled || !isActive(world, e) || !Environment::valid(r) ||
                !Environment::finite(t.position) || !Environment::finite(t.scale) ||
                !isUsable(t.rotation)) {
                return;
            }
            const int count = (int(r.points.size()) - 1) * r.subdivisions;
            auto point = [&](Vec3 p) { return t.position + rotateVector(t.rotation, p * t.scale); };
            for (int i = 0; i < count; ++i) {
                const auto a = Environment::curve(r, float(i) / count);
                const auto b = Environment::curve(r, float(i + 1) / count);
                auto side = normalize(Vec3{-(b - a).z, 0, (b - a).x});
                if (dot(side, side) < .1f) {
                    continue;
                }
                WaterPatch patch;
                patch.a = point(a);
                patch.along = point(b) - patch.a;
                patch.side = rotateVector(t.rotation, side * t.scale) * (r.width * .5f);
                patch.normal = normalize(cross(patch.along, patch.side));
                if (patch.normal.y < 0) {
                    patch.normal = patch.normal * -1.f;
                }
                // Rivers must be water-like surfaces, not near-vertical walls.
                if (patch.normal.y < .25f) {
                    continue;
                }
                patch.aa = dot(patch.along, patch.along);
                patch.ss = dot(patch.side, patch.side);
                patch.as = dot(patch.along, patch.side);
                const float det = patch.aa * patch.ss - patch.as * patch.as;
                if (!std::isfinite(det) || det < 1e-10f) {
                    continue;
                }
                patch.inverseDet = 1.f / det;
                patch.flow = r.flowing ? std::clamp(r.speed * r.flowAmount * .08f, -2.f, 2.f) : 0;
                patch.wave = r.waveHeight;
                patch.waveLength = r.waveLength;
                patch.wavePhaseSpeed = r.flowing ? r.speed * r.flowAmount * r.waveSpeed : 0;
                patches.push_back(patch);
            }
        });
        world.eachReadOnly<Transform, Water>([&](Entity e, const Transform& t, const Water& w) {
            if (!w.enabled || !isActive(world, e) || !Environment::valid(w)) {
                return;
            }
            try {
                const auto& mesh = Waters::geometry(world, e, assets, false);
                auto point = [&](Vec3 p) {
                    return t.position + rotateVector(t.rotation, p * t.scale);
                };
                for (size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
                    WaterPatch p;
                    p.a = point(mesh.vertices[i].position);
                    p.along = point(mesh.vertices[i + 1].position) - p.a;
                    p.side = point(mesh.vertices[i + 2].position) - p.a;
                    p.normal = normalize(cross(p.along, p.side));
                    if (p.normal.y < .25f) {
                        continue;
                    }
                    p.aa = dot(p.along, p.along);
                    p.ss = dot(p.side, p.side);
                    p.as = dot(p.along, p.side);
                    float det = p.aa * p.ss - p.as * p.as;
                    if (!std::isfinite(det) || det < 1e-10f) {
                        continue;
                    }
                    p.inverseDet = 1 / det;
                    p.triangle = true;
                    p.flow = w.flowing ? std::clamp(w.speed * w.flowAmount * .08f, -2.f, 2.f) : 0;
                    p.wave = w.waveHeight;
                    p.waveLength = w.waveLength;
                    p.wavePhaseSpeed = w.flowing ? w.speed * w.flowAmount * w.waveSpeed : 0;
                    patches.push_back(p);
                }
            } catch (const std::exception& ex) {
                std::fprintf(stderr, "Water physics: %s\n", ex.what());
            }
        });
        return patches;
    }
    void floatBody(const Body& body, const std::vector<WaterPatch>& patches) {
        auto& api = system.GetBodyInterface();
        const auto center = vec(api.GetCenterOfMassPosition(body.id));
        if (!isUsable(center)) {
            return;
        }
        const auto& box = body.box;
        const float radius = box.shape == ColliderShape::Box
                                 ? std::sqrt(dot(box.halfSize, box.halfSize))
                                 : box.radius + box.height * .5f;
        const WaterPatch* best = nullptr;
        Vec3 surface{};
        float highest = -1e30f;
        for (const auto& patch : patches) {
            // Intersect the body's vertical line with the finite ribbon patch.
            const float dy = dot(patch.a - center, patch.normal) / patch.normal.y;
            if (dy > 4.f + radius || dy < -radius - .15f) {
                continue;
            }
            const auto p = center + Vec3{0, dy, 0};
            const auto delta = p - patch.a;
            const float da = dot(delta, patch.along), ds = dot(delta, patch.side);
            const float u = (da * patch.ss - ds * patch.as) * patch.inverseDet;
            const float v = (ds * patch.aa - da * patch.as) * patch.inverseDet;
            if (u < 0 || u > 1 || (patch.triangle ? (v < 0 || u + v > 1) : std::abs(v) > 1)) {
                continue;
            }
            if (p.y > highest) {
                highest = p.y;
                surface = p;
                best = &patch;
            }
        }
        if (!best) {
            return; // Never stack buoyancy from overlapping rivers.
        }
        // A tiny coherent wave proxy drives gentle heave; Jolt integrates the
        // actual submerged shape volume, center-of-buoyancy torque and drag.
        const float k = 2 * kPi / best->waveLength, phase = float(waterTime) * best->wavePhaseSpeed;
        surface.y +=
            best->wave *
            (.6f * std::sin(k * (surface.x * .8f + surface.z * .6f) - phase * 1.4f) +
             .28f * std::sin(k * 1.73f * (-surface.x * .6f + surface.z * .8f) - phase * 1.9f) +
             .12f * std::sin(k * 2.61f * (surface.x * .3f + surface.z * .95f) - phase * 2.6f));
        const auto current = normalize(best->along) * best->flow;
        api.ApplyBuoyancyImpulse(body.id,
                                 jvec(surface),
                                 jvec(best->normal),
                                 1.8f,
                                 .8f,
                                 .6f,
                                 jvec(current),
                                 system.GetGravity(),
                                 kStepSeconds);
    }
    ~Impl() {
        for (auto& entry : bodies) {
            release(entry.second);
        }
    }
    void release(Body& body) {
        if (!body.id.IsInvalid()) {
            needsWake_ = true;
            contacts.erase(body.id);
            system.GetBodyInterface().RemoveBody(body.id);
            system.GetBodyInterface().DestroyBody(body.id);
            body.id = JPH::BodyID();
        }
    }
    static JPH::ShapeRefC checked(const JPH::ShapeSettings::ShapeResult& result) {
        if (result.HasError()) {
            throw std::runtime_error(result.GetError().c_str());
        }
        return result.Get();
    }
    JPH::ShapeRefC shape(const Box& box, bool collider, const Terrain* terrain = nullptr) {
        JPH::ShapeRefC result;
        if (!collider) {
            return new JPH::EmptyShape;
        }
        switch (box.shape) {
        case ColliderShape::Box:
            result = checked(
                JPH::BoxShapeSettings(
                    jvec(box.halfSize),
                    std::min(.05f,
                             std::min({box.halfSize.x, box.halfSize.y, box.halfSize.z}) * .5f))
                    .Create());
            break;
        case ColliderShape::Sphere: {
            if (std::abs(box.halfSize.x - box.halfSize.y) < 1e-5f &&
                std::abs(box.halfSize.x - box.halfSize.z) < 1e-5f) {
                result = checked(JPH::SphereShapeSettings(box.radius).Create());
            } else {
                JPH::Array<JPH::Vec3> points;
                for (int ring = 0; ring <= 12; ++ring) {
                    for (int slice = 0; slice < 24; ++slice) {
                        float lat = -kPi * .5f + kPi * ring / 12, lon = 2 * kPi * slice / 24;
                        points.push_back({box.halfSize.x * std::cos(lat) * std::cos(lon),
                                          box.halfSize.y * std::sin(lat),
                                          box.halfSize.z * std::cos(lat) * std::sin(lon)});
                    }
                }
                result = checked(JPH::ConvexHullShapeSettings(points, 0).Create());
            }
            break;
        }
        case ColliderShape::Capsule:
            result = checked(JPH::CapsuleShapeSettings(box.height * .5f, box.radius).Create());
            break;
        case ColliderShape::Cone: {
            JPH::Array<JPH::Vec3> points;
            points.push_back({0, box.height * .5f, 0});
            for (int i = 0; i < 32; ++i) {
                float angle = 2 * kPi * i / 32;
                points.push_back({box.radius * std::cos(angle),
                                  -box.height * .5f,
                                  box.radius * std::sin(angle)});
            }
            result = checked(JPH::ConvexHullShapeSettings(points, 0).Create());
            break;
        }
        case ColliderShape::Mesh: {
            if (box.model.empty()) {
                throw std::runtime_error(
                    "Mesh collider requires a model asset; no convex/box fallback");
            }
            auto vertices =
                terrain && terrain->data
                    ? Terrains::collision(*terrain->data)
                    : simplifyMeshTriangles(loadModelAsset(assets, box.model), box.meshDetail);
            JPH::TriangleList triangles;
            for (std::size_t i = 0; i + 2 < vertices.size(); i += 3) {
                auto a = vertices[i] * box.hullScale, b = vertices[i + 1] * box.hullScale,
                     c = vertices[i + 2] * box.hullScale;
                if (dot(cross(b - a, c - a), cross(b - a, c - a)) < 1e-16f) {
                    continue;
                }
                if (box.hullScale.x * box.hullScale.y * box.hullScale.z < 0) {
                    std::swap(b, c);
                }
                triangles.emplace_back(jvec(a), jvec(b), jvec(c));
            }
            if (triangles.empty()) {
                throw std::runtime_error("Mesh collider contains no usable triangles");
            }
            result = checked(JPH::MeshShapeSettings(triangles).Create());
            break;
        }
        }
        if (box.center != Vec3{}) {
            result = checked(JPH::RotatedTranslatedShapeSettings(
                                 jvec(box.center), JPH::Quat::sIdentity(), result)
                                 .Create());
        }
        return result;
    }
    void sync(World& world) {
        for (auto& entry : bodies) {
            entry.second.visited = false;
        }
        auto& api = system.GetBodyInterface();
        world.each<Transform>([&](Entity entity, Transform& transform) {
            if (!isActive(world, entity)) {
                return;
            }
            const auto* terrain = world.tryGet<Terrain>(entity);
            const auto* source = world.tryGet<Collider>(entity);
            Collider terrainCollider;
            if (terrain && terrain->data) {
                terrainCollider.shape = ColliderShape::Mesh;
                terrainCollider.model = "@terrain";
                terrainCollider.meshDetail = 15;
                terrainCollider.friction = .7f;
                source = &terrainCollider;
            }
            const auto resolved = source == &terrainCollider ? terrainCollider
                                  : source ? effectiveCollider(world, entity, *source)
                                           : Collider{};
            if (world.has<Water>(entity)) {
                return; // Virtual water surfaces never become solid Jolt obstacles.
            }
            const auto* rigid = world.tryGet<RigidBody>(entity);
            if ((!source && !rigid) || !isUsable(transform, source ? &resolved : nullptr, rigid)) {
                return;
            }
            auto& body = bodies[keyOf(entity)];
            const bool fresh = body.id.IsInvalid();
            body.entity = entity;
            body.visited = true;
            Box box = source ? toBox(resolved, transform.scale) : Box{};
            if (terrain && terrain->data) {
                box.revision =
                    (std::uint64_t(terrain->data->identity) << 32) | terrain->data->shapeRevision;
                if (terrain->sculpting && !fresh) {
                    box.revision = body.box.revision;
                }
            }
            const bool sensor = source && resolved.isTrigger;
            auto motion = rigid && rigid->isKinematic ? JPH::EMotionType::Kinematic
                          : rigid && rigid->mass > 0  ? JPH::EMotionType::Dynamic
                                                      : JPH::EMotionType::Static;
            if (source && box.shape == ColliderShape::Mesh) {
                motion = JPH::EMotionType::Static;
            }
            if (sensor && box.shape != ColliderShape::Mesh && motion == JPH::EMotionType::Static) {
                motion = JPH::EMotionType::Kinematic;
            }
            const bool controller = world.has<CharacterController>(entity);
            Vec3 centerOfMass;
            if (const auto* vehicle = world.tryGet<Vehicle>(entity);
                vehicle && Vehicles::valid(*vehicle) && motion == JPH::EMotionType::Dynamic) {
                centerOfMass = {
                    vehicle->centerOfMassX, vehicle->centerOfMassY, vehicle->centerOfMassZ};
                centerOfMass = {centerOfMass.x * transform.scale.x,
                                centerOfMass.y * transform.scale.y,
                                centerOfMass.z * transform.scale.z};
            }
            const bool changed =
                centerOfMass != body.centerOfMass || body.hasCollider != bool(source) ||
                body.hasRigid != bool(rigid) || body.sensor != sensor ||
                body.controller != controller || body.motion != motion ||
                box.shape != body.box.shape || box.halfSize != body.box.halfSize ||
                box.center != body.box.center || box.radius != body.box.radius ||
                box.height != body.box.height || box.model != body.box.model ||
                box.meshDetail != body.box.meshDetail || box.revision != body.box.revision ||
                box.hullScale != body.box.hullScale || (rigid && rigid->mass != body.rigid.mass);
            if (fresh && body.failed && !changed) {
                return;
            }
            const bool rebuild = fresh || changed;
            Pose pose = fresh ? toPose(transform) : body.current;
            bool moved = fresh;
            if (fresh || motion == JPH::EMotionType::Static) {
                pose = toPose(transform);
                moved = fresh || transform.position != body.shown.position ||
                        transform.rotation != body.shown.rotation;
            } else {
                for (int axis = 0; axis < 3; ++axis) {
                    float* p = axis == 0   ? &pose.position.x
                               : axis == 1 ? &pose.position.y
                                           : &pose.position.z;
                    float t = axis == 0   ? transform.position.x
                              : axis == 1 ? transform.position.y
                                          : transform.position.z;
                    float old = axis == 0   ? body.shown.position.x
                                : axis == 1 ? body.shown.position.y
                                            : body.shown.position.z;
                    if (t != old) {
                        *p = t;
                        moved = true;
                    }
                }
                if (transform.rotation != body.shown.rotation) {
                    pose.rotation = normalize(transform.rotation);
                    moved = true;
                }
            }
            try {
                if (rebuild) {
                    release(body);
                    auto geometry = shape(box, source != nullptr, terrain);
                    if (centerOfMass != Vec3{}) {
                        geometry =
                            new JPH::OffsetCenterOfMassShape(geometry.GetPtr(), jvec(centerOfMass));
                    }
                    JPH::BodyCreationSettings settings(
                        geometry,
                        jvec(pose.position),
                        jquat(pose.rotation),
                        motion,
                        JPH::ObjectLayer(LayerSettings::effective(world, entity) * 2 +
                                         (motion == JPH::EMotionType::Static ? 0 : 1)));
                    settings.mUserData = keyOf(entity);
                    settings.mIsSensor = sensor;
                    settings.mCollideKinematicVsNonDynamic = sensor;
                    settings.mAllowSleeping = !sensor;
                    settings.mFriction = box.friction;
                    settings.mRestitution = box.bounciness;
                    if (controller) {
                        settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX |
                                                JPH::EAllowedDOFs::TranslationY |
                                                JPH::EAllowedDOFs::TranslationZ;
                    }
                    if (motion == JPH::EMotionType::Dynamic) {
                        settings.mOverrideMassProperties =
                            JPH::EOverrideMassProperties::MassAndInertiaProvided;
                        auto mass = geometry->GetMassProperties();
                        if (mass.mMass > 0) {
                            mass.ScaleToMass(std::max(rigid->mass, kSmallestMass));
                        } else {
                            mass.SetMassAndInertiaOfSolidBox(JPH::Vec3::sReplicate(1),
                                                             std::max(rigid->mass, kSmallestMass));
                        }
                        settings.mMassPropertiesOverride = mass;
                    }
                    body.id = api.CreateAndAddBody(settings,
                                                   motion == JPH::EMotionType::Static
                                                       ? JPH::EActivation::DontActivate
                                                       : JPH::EActivation::Activate);
                    if (body.id.IsInvalid()) {
                        throw std::runtime_error("Jolt body capacity exhausted");
                    }
                    body.previous = body.current = pose;
                } else if (moved) {
                    api.SetPositionAndRotation(body.id,
                                               jvec(pose.position),
                                               jquat(pose.rotation),
                                               JPH::EActivation::Activate);
                    body.previous = body.current = pose;
                }
                if ((moved && motion == JPH::EMotionType::Static) ||
                    box.friction != body.box.friction || box.bounciness != body.box.bounciness) {
                    needsWake_ = true;
                }
                api.SetFriction(body.id, box.friction);
                api.SetRestitution(body.id, box.bounciness);
                if (rigid && motion != JPH::EMotionType::Static) {
                    api.SetGravityFactor(body.id, rigid->useGravity ? 1 : 0);
                    if (rebuild || rigid->linearVelocity != body.rigid.linearVelocity ||
                        rigid->angularVelocity != body.rigid.angularVelocity) {
                        api.SetLinearAndAngularVelocity(body.id,
                                                        jvec(rigid->linearVelocity),
                                                        controller ? JPH::Vec3::sZero()
                                                                   : jvec(rigid->angularVelocity));
                    }
                    JPH::BodyLockWrite lock(system.GetBodyLockInterface(), body.id);
                    if (lock.Succeeded()) {
                        auto* properties = lock.GetBody().GetMotionProperties();
                        properties->SetLinearDamping(rigid->drag);
                        properties->SetAngularDamping(rigid->angularDrag);
                    }
                }
                body.box = box;
                body.centerOfMass = centerOfMass;
                body.hasCollider = source != nullptr;
                body.hasRigid = rigid != nullptr;
                body.sensor = sensor;
                body.controller = controller;
                body.motion = motion;
                body.rigid = rigid ? *rigid : RigidBody{};
                const auto objectLayer =
                    JPH::ObjectLayer(LayerSettings::effective(world, entity) * 2 +
                                     (motion == JPH::EMotionType::Static ? 0 : 1));
                if (api.GetObjectLayer(body.id) != objectLayer) {
                    api.SetObjectLayer(body.id, objectLayer);
                }
                body.shown = transform;
                body.failed = false;
            } catch (const std::exception& error) {
                release(body);
                std::fprintf(stderr, "Jolt entity %u: %s\n", entity.index, error.what());
                body.box = box;
                body.hasCollider = source != nullptr;
                body.hasRigid = rigid != nullptr;
                body.sensor = sensor;
                body.controller = controller;
                body.motion = motion;
                body.rigid = rigid ? *rigid : RigidBody{};
                body.failed = true;
            }
        });
        for (auto it = bodies.begin(); it != bodies.end();) {
            if (!it->second.visited) {
                release(it->second);
                it = bodies.erase(it);
            } else {
                ++it;
            }
        }
        if (needsWake_) {
            for (auto& entry : bodies) {
                if (!entry.second.id.IsInvalid() &&
                    entry.second.motion != JPH::EMotionType::Static) {
                    api.ActivateBody(entry.second.id);
                }
            }
            needsWake_ = false;
        }
    }
    void events(std::vector<Physics::TriggerEvent>& triggers,
                std::vector<Physics::TriggerEvent>& collisions) {
        // Jolt removes cached contacts when bodies go to sleep. That is not
        // a gameplay exit: retain sleeping pairs until woken or explicitly removed.
        auto& bodyAPI = system.GetBodyInterface();
        for (auto it = contacts.removed.begin(); it != contacts.removed.end();) {
            const auto found = contacts.live.find(*it);
            if (found == contacts.live.end()) {
                it = contacts.removed.erase(it);
                continue;
            }
            if (bodyAPI.IsActive(JPH::BodyID(std::get<0>(*it))) ||
                bodyAPI.IsActive(JPH::BodyID(std::get<1>(*it)))) {
                contacts.live.erase(found);
                it = contacts.removed.erase(it);
            } else {
                ++it;
            }
        }
        decltype(pairs) now;
        for (const auto& contact : contacts.live) {
            auto pair = contact.second;
            auto a = keyOf(pair.a), b = keyOf(pair.b);
            if (a > b) {
                std::swap(a, b);
                std::swap(pair.a, pair.b);
            }
            now[{a, b}] = pair;
        }
        // Explicit trigger shape queries also cover stationary/static pairs,
        // which rigid-body solvers normally omit from their active-contact pass.
        for (const auto& entry : bodies) {
            const auto& sensor = entry.second;
            if (!sensor.sensor || sensor.id.IsInvalid()) {
                continue;
            }
            struct ExcludeSelf : JPH::BodyFilter {
                JPH::BodyID self;
                bool mesh;
                int layer;
                ExcludeSelf(JPH::BodyID id, bool isMesh, int objectLayer)
                    : self(id), mesh(isMesh), layer(objectLayer >> 1) {}
                bool ShouldCollide(const JPH::BodyID& id) const override {
                    return id != self;
                }
                bool ShouldCollideLocked(const JPH::Body& body) const override {
                    // Jolt has no mesh-vs-mesh narrow phase. Concave geometry is
                    // retained, not silently replaced by a convex hull.
                    return LayerSettings::collide(layer, body.GetObjectLayer() >> 1) &&
                           (!mesh || !body.GetShape()->MustBeStatic());
                }
            } filter(sensor.id,
                     sensor.box.shape == ColliderShape::Mesh,
                     system.GetBodyInterface().GetObjectLayer(sensor.id));
            const auto& api = system.GetBodyInterface();
            const auto geometry = api.GetShape(sensor.id);
            JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
            JPH::CollideShapeSettings settings;
            system.GetNarrowPhaseQuery().CollideShape(geometry,
                                                      JPH::Vec3::sReplicate(1),
                                                      api.GetCenterOfMassTransform(sensor.id),
                                                      settings,
                                                      JPH::RVec3::sZero(),
                                                      collector,
                                                      {},
                                                      {},
                                                      filter);
            for (const auto& hit : collector.mHits) {
                const auto entity = Contacts::entity(api.GetUserData(hit.mBodyID2));
                auto a = keyOf(sensor.entity), b = keyOf(entity);
                Pair pair{sensor.entity, entity, true};
                if (a > b) {
                    std::swap(a, b);
                    std::swap(pair.a, pair.b);
                }
                now[{a, b}] = pair;
            }
        }
        auto emit = [&](const Pair& pair, int phase) {
            auto& target = pair.sensor ? triggers : collisions;
            target.push_back({pair.a, pair.b, phase});
            target.push_back({pair.b, pair.a, phase});
        };
        for (const auto& old : pairs) {
            if (!now.count(old.first) || now.at(old.first).sensor != old.second.sensor) {
                emit(old.second, 2);
            }
        }
        for (const auto& pair : now) {
            emit(pair.second,
                 pairs.count(pair.first) && pairs.at(pair.first).sensor == pair.second.sensor ? 1
                                                                                              : 0);
        }
        pairs = std::move(now);
    }
};

Physics::Physics() : impl_(std::make_unique<Impl>()) {}
Physics::~Physics() = default;
void Physics::reset() {
    const auto root = impl_->assets.root;
    impl_ = std::make_unique<Impl>();
    Environment::waterTime = 0;
    impl_->assets.root = root;
    triggerEvents.clear();
    collisionEvents.clear();
}
void Physics::setAssetRoot(const std::filesystem::path& root) {
    if (impl_->assets.root != root) {
        impl_->assets.root = root;
        for (auto& entry : impl_->bodies) {
            if (entry.second.box.shape == ColliderShape::Mesh) {
                impl_->release(entry.second);
            }
        }
    }
}
void Physics::update(World& world, float seconds, const std::function<void(float)>& beforeStep) {
    auto& impl = *impl_;
    auto& api = impl.system.GetBodyInterface();
    collisionEvents.clear();
    triggerEvents.clear();
    impl.sync(world);
    if (std::isfinite(seconds) && seconds > 0) {
        impl.accumulated += std::min(seconds, kLongestFrameSeconds);
    }
    while (impl.accumulated >= kStepSeconds) {
        if (beforeStep) {
            beforeStep(kStepSeconds);
            impl.sync(world);
        }
        const bool hasFloaters =
            std::any_of(impl.bodies.begin(), impl.bodies.end(), [](const auto& entry) {
                const auto& b = entry.second;
                return b.motion == JPH::EMotionType::Dynamic && b.rigid.floatOnRivers &&
                       b.rigid.useGravity;
            });
        const auto water = hasFloaters ? impl.waterPatches(world) : std::vector<Impl::WaterPatch>{};
        impl.waterTime += kStepSeconds;
        Environment::waterTime = impl.waterTime;
        for (auto& entry : impl.bodies) {
            auto& body = entry.second;
            if (body.id.IsInvalid()) {
                continue;
            }
            body.previous = body.current;
            if (body.motion == JPH::EMotionType::Dynamic) {
                auto& rigid = world.get<RigidBody>(body.entity);
                if (isUsable(rigid.force) && rigid.force != Vec3{}) {
                    api.AddForce(body.id, jvec(rigid.force));
                }
                if (isUsable(rigid.impulse) && rigid.impulse != Vec3{}) {
                    api.AddImpulse(body.id, jvec(rigid.impulse));
                }
                if (rigid.floatOnRivers && rigid.useGravity && body.hasCollider && !body.sensor &&
                    body.box.shape != ColliderShape::Mesh) {
                    impl.floatBody(body, water);
                }
                rigid.force = {};
                rigid.impulse = {};
            }
        }
        // Raycast wheels exert point impulses on the actual solver chassis, never
        // teleporting its Transform. Commands written by scripts apply this step.
        world.each<Vehicle>([&](Entity entity, Vehicle& v) {
            v.speed = 0;
            v.groundedWheels = 0;
            v.wheelSteerAngle = std::clamp(v.steering, -1.f, 1.f) * radians(v.maxSteerAngle);
            v.compression.fill(0);
            auto found = impl.bodies.find(keyOf(entity));
            if (!v.enabled || !Vehicles::valid(v) || !isActive(world, entity) ||
                found == impl.bodies.end()) {
                return;
            }
            const auto& body = found->second;
            if (body.id.IsInvalid() || body.motion != JPH::EMotionType::Dynamic ||
                !body.hasCollider || body.sensor || body.box.shape == ColliderShape::Mesh ||
                body.controller) {
                return;
            }
            const auto position = vec(api.GetPosition(body.id));
            const auto rotation = quat(api.GetRotation(body.id));
            const auto up = rotateVector(rotation, {0, 1, 0}),
                       forward = rotateVector(rotation, {0, 0, -1});
            v.speed = dot(vec(api.GetLinearVelocity(body.id)), forward);
            if (dot(up, Vec3{0, 1, 0}) < .1f) {
                return; // No upside-down traction.
            }
            auto command = [](float n, float lo, float hi) {
                return std::isfinite(n) ? std::clamp(n, lo, hi) : 0.f;
            };
            const float throttle = command(v.throttle, -1, 1), steer = command(v.steering, -1, 1),
                        brake = command(v.brake, 0, 1);
            const float mass = body.rigid.mass;
            const auto* transform = world.tryGet<Transform>(entity);
            // Wheel dimensions are chassis-local metres, transformed by scale.
            if (!transform) {
                return;
            }
            const float verticalScale = std::abs(transform->scale.y);
            const float rest = v.suspensionLength * verticalScale;
            const float radius =
                v.wheelRadius * std::max(std::abs(transform->scale.x), verticalScale);
            if (rest < .001f || radius < .001f) {
                return;
            }
            struct TireContact {
                Vec3 point, direction, lateral, gravity;
                JPH::BodyID ground;
                bool dynamicGround = false;
                float load = 0, inverseLong = 0, inverseSide = 0;
                float longitudinal = 0, sideways = 0, drive = 0, brakeLimit = 0;
                bool hold = false, rear = false;
                int wheel = 0;
            };
            std::vector<TireContact> tires;
            tires.reserve(4);
            auto inverseMassAt = [&](JPH::BodyID id, float inverseMass, Vec3 point, Vec3 axis) {
                const auto arm = jvec(point) - JPH::Vec3(api.GetCenterOfMassPosition(id));
                const auto torque = arm.Cross(jvec(axis));
                return inverseMass + torque.Dot(api.GetInverseInertia(id).Multiply3x3(torque));
            };
            for (int wheel = 0; wheel < 4; ++wheel) {
                const bool front = wheel < 2;
                const Vec3 local{(wheel % 2 ? 1.f : -1.f) * v.trackWidth * .5f,
                                 v.mountHeight,
                                 (front ? -1.f : 1.f) * v.wheelBase * .5f};
                const auto mount = position + rotateVector(rotation, local * transform->scale);
                RayHit hit;
                const bool contact =
                    raycast({mount, up * -1.f}, rest + radius, hit, [&](Entity other) {
                        auto ancestor = other;
                        for (unsigned depth = 0; world.alive(ancestor) && depth < 128; ++depth) {
                            if (ancestor == entity) {
                                return false;
                            }
                            ancestor = getParent(world, ancestor);
                        }
                        const auto* c = world.tryGet<Collider>(other);
                        return (!c || !c->isTrigger) &&
                               LayerSettings::collide(LayerSettings::effective(world, entity),
                                                      LayerSettings::effective(world, other));
                    });
                if (!contact || dot(hit.normal, up) < .2f) {
                    continue;
                }
                ++v.groundedWheels;
                const float compression = std::clamp(rest - (hit.distance - radius), 0.f, rest);
                v.compression[wheel] = compression / rest;
                auto ground = impl.bodies.find(keyOf(hit.entity));
                const bool movingGround =
                    ground != impl.bodies.end() && !ground->second.id.IsInvalid();
                const auto groundVelocity =
                    movingGround ? vec(api.GetPointVelocity(ground->second.id, jvec(hit.point)))
                                 : Vec3{};
                const auto relative =
                    vec(api.GetPointVelocity(body.id, jvec(hit.point))) - groundVelocity;
                const float load = std::clamp(v.springStrength * compression -
                                                  v.suspensionDamping * dot(relative, up),
                                              0.f,
                                              mass * 9.81f * 2.5f);
                v.wheelSteerAngle = steer * radians(v.maxSteerAngle);
                const float angle = front ? v.wheelSteerAngle : 0;
                auto direction = rotateVector(rotation, {std::sin(angle), 0, -std::cos(angle)});
                direction = normalize(direction - hit.normal * dot(direction, hit.normal));
                if (dot(direction, direction) < .1f) {
                    continue;
                }
                const auto lateral = normalize(cross(direction, hit.normal));
                const float longitudinalSpeed = dot(relative, direction),
                            lateralSpeed = dot(relative, lateral);
                // Account for gravity integrated by Jolt immediately after these
                // impulses. Without this, a resting tire always lags by one step
                // on slopes. Work relative to dynamic ground, not world velocity.
                Vec3 relativeGravity =
                    body.rigid.useGravity ? vec(impl.system.GetGravity()) : Vec3{};
                if (movingGround && ground->second.motion == JPH::EMotionType::Dynamic &&
                    ground->second.rigid.useGravity) {
                    relativeGravity = relativeGravity - vec(impl.system.GetGravity());
                }
                TireContact tire;
                tire.point = hit.point;
                tire.wheel = wheel;
                tire.direction = direction;
                tire.lateral = lateral;
                tire.gravity = relativeGravity;
                tire.load = load;
                tire.rear = !front;
                tire.dynamicGround =
                    movingGround && ground->second.motion == JPH::EMotionType::Dynamic;
                if (movingGround) {
                    tire.ground = ground->second.id;
                }
                tire.inverseLong = inverseMassAt(body.id, 1.f / mass, hit.point, direction);
                tire.inverseSide = inverseMassAt(body.id, 1.f / mass, hit.point, lateral);
                if (tire.dynamicGround) {
                    tire.inverseLong += inverseMassAt(
                        tire.ground, 1.f / ground->second.rigid.mass, hit.point, direction);
                    tire.inverseSide += inverseMassAt(
                        tire.ground, 1.f / ground->second.rigid.mass, hit.point, lateral);
                }
                tire.hold = std::abs(throttle) < .01f && std::abs(longitudinalSpeed) < .5f &&
                            std::abs(lateralSpeed) < .5f;
                const float braking = (tire.hold || (v.handbrake && !front)) ? 1.f : brake;
                tire.brakeLimit = braking * v.brakeForce * .25f * kStepSeconds;
                tire.drive = (std::abs(v.speed) >= v.maxSpeed && throttle * v.speed > 0)
                                 ? 0.f
                                 : throttle * v.engineForce * .25f * kStepSeconds;
                // Coasting rolling resistance, without making it an unbounded brake.
                if (!tire.hold) {
                    tire.brakeLimit += load * .015f * (1 - braking) * kStepSeconds;
                }
                tires.push_back(tire);
            }
            auto applyTireImpulse = [&](const TireContact& tire, Vec3 impulse) {
                api.AddImpulse(body.id, jvec(impulse), jvec(tire.point));
                if (tire.dynamicGround) {
                    api.AddImpulse(tire.ground, jvec(impulse * -1.f), jvec(tire.point));
                }
            };
            // Apply all suspension impulses before solving traction, then iterate
            // accumulated, effective-mass tire constraints. A single mass/4 force
            // pass leaves residual yaw/slip and lets later wheels undo earlier ones.
            for (const auto& tire : tires) {
                applyTireImpulse(tire, up * (tire.load * kStepSeconds));
            }
            for (int iteration = 0; iteration < 8; ++iteration) {
                for (auto& tire : tires) {
                    const Vec3 groundVelocity =
                        tire.ground.IsInvalid()
                            ? Vec3{}
                            : vec(api.GetPointVelocity(tire.ground, jvec(tire.point)));
                    const Vec3 velocity = vec(api.GetPointVelocity(body.id, jvec(tire.point))) -
                                          groundVelocity + tire.gravity * kStepSeconds;
                    const float limit = v.tireGrip * tire.load * kStepSeconds;
                    const float sideLimit = limit * (v.handbrake && tire.rear ? .5f : 1.f);
                    float side = std::clamp(tire.sideways - dot(velocity, tire.lateral) /
                                                                std::max(1e-8f, tire.inverseSide),
                                            -sideLimit,
                                            sideLimit);
                    const float remaining = std::sqrt(std::max(0.f, limit * limit - side * side));
                    const float desired = tire.longitudinal - dot(velocity, tire.direction) /
                                                                  std::max(1e-8f, tire.inverseLong);
                    float longitudinal = std::clamp(
                        desired, tire.drive - tire.brakeLimit, tire.drive + tire.brakeLimit);
                    longitudinal = std::clamp(longitudinal, -remaining, remaining);
                    applyTireImpulse(tire,
                                     tire.lateral * (side - tire.sideways) +
                                         tire.direction * (longitudinal - tire.longitudinal));
                    tire.sideways = side;
                    tire.longitudinal = longitudinal;
                }
            }
        });
        const auto error = impl.system.Update(kStepSeconds, 1, &impl.allocator, &impl.jobs);
        if (error != JPH::EPhysicsUpdateError::None) {
            std::fprintf(stderr, "Jolt update capacity error: %u\n", unsigned(error));
        }
        for (auto& entry : impl.bodies) {
            auto& body = entry.second;
            if (body.id.IsInvalid()) {
                continue;
            }
            body.current = {vec(api.GetPosition(body.id)), quat(api.GetRotation(body.id))};
            if (body.hasRigid) {
                body.rigid.linearVelocity = vec(api.GetLinearVelocity(body.id));
                body.rigid.angularVelocity = vec(api.GetAngularVelocity(body.id));
                if (!isUsable(body.current) || !isUsable(body.rigid.linearVelocity) ||
                    !isUsable(body.rigid.angularVelocity)) {
                    std::fprintf(
                        stderr,
                        "Jolt: entity %u produced an invalid state and was removed from simulation\n",
                        body.entity.index);
                    impl.release(body);
                    continue;
                }
                auto& output = world.get<RigidBody>(body.entity);
                output.linearVelocity = body.rigid.linearVelocity;
                output.angularVelocity = body.rigid.angularVelocity;
            }
        }
        impl.events(triggerEvents, collisionEvents);
        impl.accumulated -= kStepSeconds;
    }
    const float blend = impl.accumulated / kStepSeconds;
    for (auto& entry : impl.bodies) {
        auto& body = entry.second;
        if (body.id.IsInvalid() || !body.hasRigid) {
            continue;
        }
        auto& transform = world.get<Transform>(body.entity);
        transform.position =
            body.previous.position + (body.current.position - body.previous.position) * blend;
        transform.rotation = nlerp(body.previous.rotation, body.current.rotation, blend);
        body.shown = transform;
        auto& rigid = world.get<RigidBody>(body.entity);
        rigid.linearVelocity = body.rigid.linearVelocity;
        rigid.angularVelocity = body.rigid.angularVelocity;
    }
}
std::vector<RayHit> Physics::raycastAll(const Ray& ray,
                                        float distance,
                                        const std::function<bool(Entity)>& filter) const {
    std::vector<RayHit> hits;
    const auto direction = normalize(ray.direction);
    if (!isUsable(ray.origin) || !isUsable(direction) || direction == Vec3{} ||
        !std::isfinite(distance) || distance <= 0) {
        return hits;
    }
    distance = std::min(distance, kLargestNumber);
    JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
    JPH::RayCastSettings settings;
    settings.mTreatConvexAsSolid = false;
    impl_->system.GetNarrowPhaseQuery().CastRay(
        JPH::RRayCast(jvec(ray.origin), jvec(direction * distance)), settings, collector);
    for (const auto& result : collector.mHits) {
        JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), result.mBodyID);
        if (!lock.Succeeded()) {
            continue;
        }
        const auto entity = Impl::Contacts::entity(lock.GetBody().GetUserData());
        if (filter && !filter(entity)) {
            continue;
        }
        const auto point = ray.origin + direction * (distance * result.mFraction);
        hits.push_back(
            {entity,
             point,
             vec(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, jvec(point))),
             distance * result.mFraction});
    }
    std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) {
        return a.distance < b.distance;
    });
    std::set<std::uint64_t> seen;
    hits.erase(
        std::remove_if(hits.begin(),
                       hits.end(),
                       [&](const auto& hit) { return !seen.insert(keyOf(hit.entity)).second; }),
        hits.end());
    return hits;
}
bool Physics::raycast(const Ray& ray,
                      float distance,
                      RayHit& hit,
                      const std::function<bool(Entity)>& filter) const {
    const auto hits = raycastAll(ray, distance, filter);
    if (hits.empty()) {
        return false;
    }
    hit = hits.front();
    return true;
}
std::vector<RayHit>
Physics::overlapSphere(Vec3 origin, float radius, const std::function<bool(Entity)>& filter) const {
    std::vector<RayHit> hits;
    if (!isUsable(origin) || !std::isfinite(radius) || radius <= 0 || radius > kLargestNumber) {
        return hits;
    }
    const auto shape = Impl::checked(JPH::SphereShapeSettings(radius).Create());
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    JPH::CollideShapeSettings settings;
    impl_->system.GetNarrowPhaseQuery().CollideShape(shape,
                                                     JPH::Vec3::sReplicate(1),
                                                     JPH::RMat44::sTranslation(jvec(origin)),
                                                     settings,
                                                     JPH::RVec3::sZero(),
                                                     collector);
    std::set<std::uint64_t> seen;
    for (const auto& contact : collector.mHits) {
        JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), contact.mBodyID2);
        if (!lock.Succeeded()) {
            continue;
        }
        const auto entity = Impl::Contacts::entity(lock.GetBody().GetUserData());
        if ((filter && !filter(entity)) || !seen.insert(keyOf(entity)).second) {
            continue;
        }
        const auto point = vec(contact.mContactPointOn2);
        const auto delta = point - origin;
        hits.push_back({entity,
                        point,
                        vec(-contact.mPenetrationAxis.NormalizedOr(JPH::Vec3::sAxisY())),
                        std::sqrt(dot(delta, delta))});
    }
    std::sort(hits.begin(), hits.end(), [](const auto& a, const auto& b) {
        return a.distance < b.distance;
    });
    return hits;
}
bool Physics::checkBox(Vec3 center, Vec3 size) const {
    if (!isUsable(center) || !isUsable(size)) {
        return false;
    }
    auto half = Vec3{std::max(std::abs(size.x) * .5f, .0005f),
                     std::max(std::abs(size.y) * .5f, .0005f),
                     std::max(std::abs(size.z) * .5f, .0005f)};
    const auto shape = Impl::checked(JPH::BoxShapeSettings(jvec(half), 0).Create());
    JPH::AnyHitCollisionCollector<JPH::CollideShapeCollector> collector;
    JPH::CollideShapeSettings settings;
    impl_->system.GetNarrowPhaseQuery().CollideShape(shape,
                                                     JPH::Vec3::sReplicate(1),
                                                     JPH::RMat44::sTranslation(jvec(center)),
                                                     settings,
                                                     JPH::RVec3::sZero(),
                                                     collector);
    return collector.HadHit();
}

bool Physics::sphereCast(const Ray& ray,
                         float distance,
                         float radius,
                         RayHit& hit,
                         const std::function<bool(Entity)>& filter) const {
    if (!isUsable(ray.origin) || !isUsable(ray.direction) || !std::isfinite(radius) ||
        radius <= 0 || !std::isfinite(distance) || distance <= 0) {
        return false;
    }
    const auto direction = normalize(ray.direction);
    if (direction == Vec3{}) {
        return false;
    }
    JPH::SphereShape shape(radius);
    JPH::RShapeCast cast(&shape,
                         JPH::Vec3::sReplicate(1),
                         JPH::RMat44::sTranslation(jvec(ray.origin)),
                         jvec(direction * distance));
    JPH::ShapeCastSettings settings;
    settings.mReturnDeepestPoint = true;
    settings.SetBackFaceMode(JPH::EBackFaceMode::CollideWithBackFaces);
    JPH::AllHitCollisionCollector<JPH::CastShapeCollector> collector;
    impl_->system.GetNarrowPhaseQuery().CastShape(cast, settings, JPH::RVec3::sZero(), collector);
    bool found = false;
    float closest = distance;
    for (const auto& result : collector.mHits) {
        JPH::BodyLockRead lock(impl_->system.GetBodyLockInterface(), result.mBodyID2);
        if (!lock.Succeeded()) {
            continue;
        }
        const auto entity = Impl::Contacts::entity(lock.GetBody().GetUserData());
        if (filter && !filter(entity)) {
            continue;
        }
        float d = std::max(0.f, result.mFraction) * distance;
        if (d <= closest) {
            closest = d;
            hit.entity = entity;
            hit.distance = d;
            hit.point = ray.origin + direction * d;
            hit.normal = normalize(vec(-result.mPenetrationAxis));
            found = true;
        }
    }
    return found;
}
