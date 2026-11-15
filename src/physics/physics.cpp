#include "physics/physics.hpp"
#include "core/components.hpp"
#include "models/collider_source.hpp"
#include "models/mesh_simplify.hpp"
#include "models/model_asset.hpp"
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/EmptyShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <tuple>
#include <unordered_map>

namespace {
constexpr float kStepSeconds = 1.f / 60.f;
constexpr float kLongestFrameSeconds = .25f;
constexpr float kSmallestBoxSize = .001f;
constexpr float kSmallestMass = 1.e-6f;
constexpr float kLargestNumber = 1.e6f;
struct Pose {
    Vec3 position;
    Quat rotation;
};
JPH::Vec3 toJolt(Vec3 v) {
    return {v.x, v.y, v.z};
}
JPH::Quat toJolt(Quat q) {
    return {q.x, q.y, q.z, q.w};
}
Vec3 fromJolt(JPH::Vec3Arg v) {
    return {v.GetX(), v.GetY(), v.GetZ()};
}
Quat fromJolt(JPH::QuatArg q) {
    return {q.GetX(), q.GetY(), q.GetZ(), q.GetW()};
}
Pose toPose(const Transform& t) {
    return {t.position, normalize(t.rotation)};
}
struct Box {
    std::string model;
    Vec3 hullScale{1, 1, 1};
    unsigned revision = 0;
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
         !isUsable(rigidBody->angularVelocity) || !isUsable(rigidBody->force) ||
         !isUsable(rigidBody->impulse) || !isUsable(rigidBody->drag) ||
         !isUsable(rigidBody->angularDrag) || rigidBody->drag < 0 || rigidBody->angularDrag < 0)) {
        return false;
    }
    return true;
}

std::uint64_t keyOf(Entity entity) {
    return (static_cast<std::uint64_t>(entity.generation) << 32) | entity.index;
}

void initializeJolt() {
    struct Global {
        Global() {
            JPH::RegisterDefaultAllocator();
            JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();
        }
        ~Global() {
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }
    };
    static Global global;
    (void)global;
}
class BroadLayers final : public JPH::BroadPhaseLayerInterface {
  public:
    JPH::uint GetNumBroadPhaseLayers() const override {
        return 1;
    }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer) const override {
        return JPH::BroadPhaseLayer(0);
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer) const override {
        return "All";
    }
#endif
};
class ObjectBroadFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
  public:
    bool ShouldCollide(JPH::ObjectLayer, JPH::BroadPhaseLayer) const override {
        return true;
    }
};
class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
  public:
    bool ShouldCollide(JPH::ObjectLayer, JPH::ObjectLayer) const override {
        return true;
    }
};
Entity entityOf(std::uint64_t key) {
    return {std::uint32_t(key), std::uint32_t(key >> 32)};
}
} // namespace

struct Physics::Impl {
    struct Body {
        Entity entity;
        JPH::BodyID id;
        bool hullInvalid = false, hasBox = false, dynamic = false, trigger = false, locked = false;
        bool visited = false, reported = false, failed = false;
        Vec3 position;
        Quat rotation;
        RigidBody rigidBody;
        Box box;
        Pose previous, current;
    };
    struct Runtime {
        Runtime() {
            initializeJolt();
        }
    } runtime;
    using PairKey = std::pair<std::uint64_t, std::uint64_t>;
    struct ContactPair {
        Entity a, b;
        JPH::BodyID idA, idB;
    };
    struct Listener final : JPH::ContactListener {
        std::mutex mutex;
        std::map<JPH::SubShapeIDPair, ContactPair> active;
        std::map<PairKey, ContactPair> seen;
        void record(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold& manifold) {
            if (a.IsSensor() || b.IsSensor()) {
                return;
            }
            std::lock_guard<std::mutex> lock(mutex);
            JPH::SubShapeIDPair subshape(
                a.GetID(), manifold.mSubShapeID1, b.GetID(), manifold.mSubShapeID2);
            const auto x = entityOf(a.GetUserData()), y = entityOf(b.GetUserData());
            const bool ordered = keyOf(x) < keyOf(y);
            const PairKey key = ordered ? PairKey{keyOf(x), keyOf(y)} : PairKey{keyOf(y), keyOf(x)};
            const ContactPair pair = ordered ? ContactPair{x, y, a.GetID(), b.GetID()}
                                             : ContactPair{y, x, b.GetID(), a.GetID()};
            active[subshape] = pair;
            seen[key] = pair;
        }
        void OnContactAdded(const JPH::Body& a,
                            const JPH::Body& b,
                            const JPH::ContactManifold& m,
                            JPH::ContactSettings&) override {
            record(a, b, m);
        }
        void OnContactPersisted(const JPH::Body& a,
                                const JPH::Body& b,
                                const JPH::ContactManifold& m,
                                JPH::ContactSettings&) override {
            record(a, b, m);
        }
        void OnContactRemoved(const JPH::SubShapeIDPair& key) override {
            std::lock_guard<std::mutex> lock(mutex);
            active.erase(key);
        }
    } contacts;
    std::map<PairKey, ContactPair> solidPairs;
    BroadLayers layers;
    ObjectBroadFilter objectBroad;
    ObjectPairFilter objectPair;
    JPH::TempAllocatorImpl allocator{32 * 1024 * 1024};
    JPH::JobSystemThreadPool jobs{
        JPH::cMaxPhysicsJobs,
        JPH::cMaxPhysicsBarriers,
        int(std::min(3u, std::max(1u, std::thread::hardware_concurrency()) - 1u))};
    JPH::PhysicsSystem physics;
    AssetManager assets;
    unsigned hullRevision = 0;
    std::map<std::pair<std::string, int>, std::vector<Vec3>> hullPoints;
    std::unordered_map<std::uint64_t, Body> bodies;
    std::map<std::pair<std::uint64_t, std::uint64_t>, std::pair<Entity, Entity>> triggerPairs;
    float pendingSeconds = 0;
    bool wakeAll = false;

    Impl() {
        physics.Init(65536, 0, 65536, 10240, layers, objectBroad, objectPair);
        physics.SetGravity(JPH::Vec3(0, -9.81f, 0));
        physics.SetContactListener(&contacts);
    }
    ~Impl() {
        for (auto& item : bodies) {
            release(item.second);
        }
        physics.SetContactListener(nullptr);
    }
    JPH::BodyInterface& api() {
        return physics.GetBodyInterface();
    }
    const JPH::BodyInterface& api() const {
        return physics.GetBodyInterface();
    }
    Body& bodyOf(Entity entity, const Transform& transform) {
        auto [it, inserted] = bodies.try_emplace(keyOf(entity));
        if (inserted) {
            auto& body = it->second;
            body.entity = entity;
            body.position = transform.position;
            body.rotation = transform.rotation;
            body.previous = body.current = toPose(transform);
        }
        return it->second;
    }
    void release(Body& body) {
        if (body.id.IsInvalid()) {
            return;
        }
        api().RemoveBody(body.id);
        api().DestroyBody(body.id);
        {
            std::lock_guard<std::mutex> lock(contacts.mutex);
            for (auto it = contacts.active.begin(); it != contacts.active.end();) {
                if (it->second.idA == body.id || it->second.idB == body.id) {
                    it = contacts.active.erase(it);
                } else {
                    ++it;
                }
            }
        }
        body.id = JPH::BodyID();
        wakeAll = true;
    }
    JPH::RefConst<JPH::Shape> shape(const Box* box) {
        if (!box) {
            return new JPH::EmptyShape();
        }
        JPH::RefConst<JPH::Shape> result;
        switch (box->shape) {
        case ColliderShape::Box:
            result = new JPH::BoxShape(toJolt(box->halfSize), 0.f);
            break;
        case ColliderShape::Sphere:
            result = new JPH::SphereShape(box->radius);
            break;
        case ColliderShape::Capsule:
            result = new JPH::CapsuleShape(box->height * .5f, box->radius);
            break;
        case ColliderShape::Cone: {
            JPH::Array<JPH::Vec3> points;
            for (auto p : coneHullVertices()) {
                points.push_back(
                    toJolt(Vec3{p.x * box->radius * 2, p.y * box->height, p.z * box->radius * 2}));
            }
            JPH::ConvexHullShapeSettings settings(points, 0.f);
            auto created = settings.Create();
            if (created.HasError()) {
                throw std::runtime_error(created.GetError().c_str());
            }
            result = created.Get();
            break;
        }
        case ColliderShape::Mesh: {
            if (box->model.empty()) {
                throw std::runtime_error(
                    "Mesh collider needs a .lemesh source or a MeshRenderer with a model asset; no box fallback is used");
            }
            if (hullRevision != modelAssetRevision) {
                hullPoints.clear();
                hullRevision = modelAssetRevision;
            }
            const auto key = std::make_pair(box->model, box->meshDetail);
            auto found = hullPoints.find(key);
            if (found == hullPoints.end()) {
                found = hullPoints
                            .emplace(key,
                                     simplifyMeshTriangles(loadModelAsset(assets, box->model),
                                                           box->meshDetail))
                            .first;
            }
            const auto& vertices = found->second;
            if (vertices.empty()) {
                throw std::runtime_error("Mesh collider has no non-degenerate triangles");
            }
            JPH::TriangleList triangles;
            triangles.reserve(vertices.size() / 3);
            bool mirrored = box->hullScale.x * box->hullScale.y * box->hullScale.z < 0;
            for (std::size_t i = 0; i + 2 < vertices.size(); i += 3) {
                auto a = toJolt(vertices[i] * box->hullScale);
                auto b = toJolt(vertices[i + (mirrored ? 2 : 1)] * box->hullScale);
                auto c = toJolt(vertices[i + (mirrored ? 1 : 2)] * box->hullScale);
                triangles.emplace_back(a, b, c);
            }
            JPH::MeshShapeSettings settings(triangles);
            auto created = settings.Create();
            if (created.HasError()) {
                throw std::runtime_error(created.GetError().c_str());
            }
            result = created.Get();
            break;
        }
        }
        if (box->center != Vec3{}) {
            result = new JPH::RotatedTranslatedShape(
                toJolt(box->center), JPH::Quat::sIdentity(), result);
        }
        return result;
    }
    void build(Body& body, const Box* box, const RigidBody* rigidBody, bool trigger, bool locked) {
        release(body);
        body.hullInvalid = false;
        body.hasBox = box != nullptr;
        body.box = box ? *box : Box{};
        body.dynamic = rigidBody != nullptr;
        body.rigidBody = rigidBody ? *rigidBody : RigidBody{};
        body.trigger = trigger;
        body.locked = locked;
        try {
            auto nativeShape = shape(box);
            const auto mode = !rigidBody || (box && box->shape == ColliderShape::Mesh)
                                  ? JPH::EMotionType::Static
                              : rigidBody->isKinematic ? JPH::EMotionType::Kinematic
                              : rigidBody->mass > 0    ? JPH::EMotionType::Dynamic
                                                       : JPH::EMotionType::Static;
            JPH::BodyCreationSettings settings(
                nativeShape, toJolt(body.current.position), toJolt(body.current.rotation), mode, 0);
            settings.mUserData = keyOf(body.entity);
            settings.mIsSensor = trigger;
            settings.mCollideKinematicVsNonDynamic = true;
            // Preserve the engine's validated velocity range rather than Jolt's
            // lower default caps, which silently truncate scripted velocities.
            settings.mMaxLinearVelocity = kLargestNumber;
            settings.mMaxAngularVelocity = kLargestNumber;
            settings.mFriction = box ? box->friction : .5f;
            settings.mRestitution = box ? box->bounciness : 0.f;
            settings.mAllowDynamicOrKinematic = mode != JPH::EMotionType::Static;
            settings.mAllowedDOFs =
                locked ? (JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY |
                          JPH::EAllowedDOFs::TranslationZ)
                       : JPH::EAllowedDOFs::All;
            if (rigidBody && mode != JPH::EMotionType::Static) {
                settings.mLinearVelocity = toJolt(rigidBody->linearVelocity);
                settings.mAngularVelocity = toJolt(rigidBody->angularVelocity);
                settings.mGravityFactor = rigidBody->useGravity ? 1.f : 0.f;
                settings.mLinearDamping = rigidBody->drag;
                settings.mAngularDamping = rigidBody->angularDrag;
                settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                settings.mMassPropertiesOverride.mMass = std::max(rigidBody->mass, kSmallestMass);
            }
            auto* created = api().CreateBody(settings);
            if (!created) {
                throw std::runtime_error("Jolt body limit exceeded");
            }
            body.id = created->GetID();
            api().AddBody(body.id,
                          mode == JPH::EMotionType::Static ? JPH::EActivation::DontActivate
                                                           : JPH::EActivation::Activate);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Physics collider: %s\n", e.what());
            release(body);
            body.hullInvalid = true;
        }
        wakeAll = true;
    }
    static bool adopt(float written, float shown, float& current, float& previous) {
        if (written == shown && isUsable(current)) {
            return false;
        }
        current = previous = written;
        return true;
    }
    void follow(Body& body,
                const Transform& transform,
                const Collider* collider,
                const RigidBody* rb,
                bool locked) {
        const bool dynamic = rb != nullptr, hasBox = collider != nullptr;
        const Box box = hasBox ? toBox(*collider, transform.scale) : Box{};
        const bool trigger = collider && collider->isTrigger;
        if (body.id.IsInvalid() && body.hullInvalid && box.shape == body.box.shape &&
            box.meshDetail == body.box.meshDetail && box.model == body.box.model &&
            box.hullScale == body.box.hullScale && box.halfSize == body.box.halfSize &&
            box.revision == body.box.revision) {
            return;
        }
        bool moved = false;
        if (!dynamic) {
            moved = transform.position != body.position || transform.rotation != body.rotation;
            body.previous = body.current = toPose(transform);
        } else {
            moved |= adopt(transform.position.x,
                           body.position.x,
                           body.current.position.x,
                           body.previous.position.x);
            moved |= adopt(transform.position.y,
                           body.position.y,
                           body.current.position.y,
                           body.previous.position.y);
            moved |= adopt(transform.position.z,
                           body.position.z,
                           body.current.position.z,
                           body.previous.position.z);
            if (transform.rotation != body.rotation || !isUsable(body.current.rotation)) {
                body.previous.rotation = body.current.rotation = normalize(transform.rotation);
                moved = true;
            }
            if (body.id.IsInvalid()) {
                body.previous = body.current;
            }
        }
        body.position = transform.position;
        body.rotation = transform.rotation;
        const bool rbChanged =
            rb &&
            (rb->mass != body.rigidBody.mass || rb->isKinematic != body.rigidBody.isKinematic ||
             rb->useGravity != body.rigidBody.useGravity || rb->drag != body.rigidBody.drag ||
             rb->angularDrag != body.rigidBody.angularDrag);
        if (body.id.IsInvalid() || dynamic != body.dynamic || hasBox != body.hasBox ||
            box.halfSize != body.box.halfSize || box.center != body.box.center ||
            box.meshDetail != body.box.meshDetail || box.model != body.box.model ||
            box.hullScale != body.box.hullScale || box.revision != body.box.revision ||
            box.shape != body.box.shape || box.radius != body.box.radius ||
            box.height != body.box.height || trigger != body.trigger || locked != body.locked ||
            rbChanged) {
            build(body, hasBox ? &box : nullptr, rb, trigger, locked);
            return;
        }
        if (hasBox &&
            (box.friction != body.box.friction || box.bounciness != body.box.bounciness)) {
            api().SetFriction(body.id, box.friction);
            api().SetRestitution(body.id, box.bounciness);
            body.box = box;
            wakeAll = true;
        }
        if (moved) {
            api().SetPositionAndRotation(body.id,
                                         toJolt(body.current.position),
                                         toJolt(body.current.rotation),
                                         JPH::EActivation::Activate);
            wakeAll = true;
        }
        if (rb && (rb->linearVelocity != body.rigidBody.linearVelocity ||
                   rb->angularVelocity != body.rigidBody.angularVelocity)) {
            if (api().GetMotionType(body.id) != JPH::EMotionType::Static) {
                api().SetLinearVelocity(body.id, toJolt(rb->linearVelocity));
                api().SetAngularVelocity(body.id, toJolt(rb->angularVelocity));
            }
            body.rigidBody = *rb;
        }
    }
};

Physics::Physics() : impl_(std::make_unique<Impl>()) {}
Physics::~Physics() = default;
void Physics::update(World& world, float seconds, const std::function<void(float)>& beforeStep) {
    auto& impl = *impl_;
    collisionEvents.clear();
    auto synchronize = [&] {
        for (auto& item : impl.bodies) {
            item.second.visited = false;
        }
        world.each<Transform>([&](Entity entity, Transform& transform) {
            if (!isActive(world, entity)) {
                return;
            }
            const auto* component = world.tryGet<Collider>(entity);
            const auto resolved =
                component ? effectiveCollider(world, entity, *component) : Collider{};
            const auto* collider = component ? &resolved : nullptr;
            const auto* rb = world.tryGet<RigidBody>(entity);
            if (!collider && !rb) {
                return;
            }
            auto& body = impl.bodyOf(entity, transform);
            body.visited = true;
            if (isUsable(transform, collider, rb)) {
                impl.follow(body, transform, collider, rb, world.has<CharacterController>(entity));
                body.reported = false;
            } else {
                impl.release(body);
                if (!body.reported) {
                    std::fprintf(stderr,
                                 "Physics: entity %u has invalid component values\n",
                                 unsigned(entity.index));
                    body.reported = true;
                }
            }
        });
        for (auto it = impl.bodies.begin(); it != impl.bodies.end();) {
            if (it->second.visited) {
                ++it;
            } else {
                impl.release(it->second);
                it = impl.bodies.erase(it);
            }
        }
        if (impl.wakeAll) {
            for (auto& item : impl.bodies) {
                if (!item.second.id.IsInvalid() &&
                    impl.api().GetMotionType(item.second.id) != JPH::EMotionType::Static) {
                    impl.api().ActivateBody(item.second.id);
                }
            }
            impl.wakeAll = false;
        }
    };
    synchronize();
    if (seconds > 0) {
        impl.pendingSeconds += std::min(seconds, kLongestFrameSeconds);
    }
    while (impl.pendingSeconds >= kStepSeconds) {
        impl.pendingSeconds -= kStepSeconds;
        if (beforeStep) {
            beforeStep(kStepSeconds);
            synchronize();
        }
        world.each<RigidBody>([&](Entity entity, RigidBody& rb) {
            auto found = impl.bodies.find(keyOf(entity));
            if (found == impl.bodies.end() || found->second.id.IsInvalid() || rb.mass <= 0 ||
                rb.isKinematic ||
                impl.api().GetMotionType(found->second.id) != JPH::EMotionType::Dynamic) {
                return;
            }
            const auto id = found->second.id;
            if (rb.impulse != Vec3{}) {
                impl.api().AddImpulse(id, toJolt(rb.impulse));
                rb.impulse = {};
            }
            if (rb.force != Vec3{}) {
                impl.api().AddForce(id, toJolt(rb.force));
                rb.force = {};
            }
        });
        const auto error = impl.physics.Update(kStepSeconds, 1, &impl.allocator, &impl.jobs);
        if (error != JPH::EPhysicsUpdateError::None) {
            std::fprintf(stderr, "Physics: Jolt update exceeded capacity (%u)\n", unsigned(error));
        }
        // Jolt removes contact constraints when bodies sleep. That is not a
        // separation event. Keep the pair until a body wakes, is rebuilt or is
        // removed, and aggregate multiple subshape manifolds into one pair.
        std::map<Impl::PairKey, Impl::ContactPair> current;
        for (const auto& entry : impl.contacts.active) {
            const auto& pair = entry.second;
            const auto a = impl.bodies.find(keyOf(pair.a));
            const auto b = impl.bodies.find(keyOf(pair.b));
            if (a != impl.bodies.end() && b != impl.bodies.end() && a->second.id == pair.idA &&
                b->second.id == pair.idB && !a->second.trigger && !b->second.trigger) {
                current[{keyOf(pair.a), keyOf(pair.b)}] = pair;
            }
        }
        for (const auto& entry : impl.solidPairs) {
            if (current.count(entry.first)) {
                continue;
            }
            const auto& pair = entry.second;
            auto a = impl.bodies.find(keyOf(pair.a)), b = impl.bodies.find(keyOf(pair.b));
            if (a != impl.bodies.end() && b != impl.bodies.end() && a->second.id == pair.idA &&
                b->second.id == pair.idB && !a->second.trigger && !b->second.trigger &&
                !impl.api().IsActive(pair.idA) && !impl.api().IsActive(pair.idB)) {
                current[entry.first] = pair;
            }
        }
        for (const auto& entry : impl.contacts.seen) {
            const auto& pair = entry.second;
            const int phase = impl.solidPairs.count(entry.first) ? 1 : 0;
            collisionEvents.push_back({pair.a, pair.b, phase});
            collisionEvents.push_back({pair.b, pair.a, phase});
        }
        for (const auto& entry : impl.solidPairs) {
            if (!current.count(entry.first)) {
                const auto& pair = entry.second;
                collisionEvents.push_back({pair.a, pair.b, 2});
                collisionEvents.push_back({pair.b, pair.a, 2});
            }
        }
        for (const auto& entry : impl.contacts.seen) {
            if (!current.count(entry.first) && !impl.solidPairs.count(entry.first)) {
                const auto& pair = entry.second;
                collisionEvents.push_back({pair.a, pair.b, 2});
                collisionEvents.push_back({pair.b, pair.a, 2});
            }
        }
        impl.solidPairs = std::move(current);
        impl.contacts.seen.clear();
        for (auto& item : impl.bodies) {
            auto& body = item.second;
            if (body.id.IsInvalid() || !body.dynamic) {
                continue;
            }
            body.previous = body.current;
            JPH::RVec3 position;
            JPH::Quat rotation;
            impl.api().GetPositionAndRotation(body.id, position, rotation);
            body.current = {fromJolt(position), fromJolt(rotation)};
            body.rigidBody.linearVelocity = fromJolt(impl.api().GetLinearVelocity(body.id));
            body.rigidBody.angularVelocity = fromJolt(impl.api().GetAngularVelocity(body.id));
            if (!isUsable(body.current) || !isUsable(body.rigidBody.linearVelocity) ||
                !isUsable(body.rigidBody.angularVelocity)) {
                impl.release(body);
                body.failed = true;
            }
        }
    }
    const float blend = impl.pendingSeconds / kStepSeconds;
    for (auto& item : impl.bodies) {
        auto& body = item.second;
        if (!body.dynamic || (body.id.IsInvalid() && !body.failed)) {
            continue;
        }
        auto& transform = world.get<Transform>(body.entity);
        if (body.failed) {
            transform.position = body.current.position;
            transform.rotation = body.current.rotation;
            body.failed = false;
        } else {
            transform.position = lerp(body.previous.position, body.current.position, blend);
            transform.rotation = nlerp(body.previous.rotation, body.current.rotation, blend);
        }
        body.position = transform.position;
        body.rotation = transform.rotation;
        auto& rb = world.get<RigidBody>(body.entity);
        rb.linearVelocity = body.rigidBody.linearVelocity;
        rb.angularVelocity = body.rigidBody.angularVelocity;
    }
    triggerEvents.clear();
    if (seconds > 0) {
        decltype(impl.triggerPairs) pairs;
        JPH::CollideShapeSettings settings;
        settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
        for (const auto& item : impl.bodies) {
            const auto& body = item.second;
            if (body.id.IsInvalid() || !body.hasBox || !body.trigger) {
                continue;
            }
            // Jolt does not dispatch mesh-vs-mesh queries. Ignore these
            // unsupported pairs rather than hitting an assertion in Debug.
            struct TriggerFilter final : JPH::BodyFilter {
                const Impl& impl;
                const Impl::Body& source;
                TriggerFilter(const Impl& i, const Impl::Body& b) : impl(i), source(b) {}
                bool ShouldCollide(const JPH::BodyID& id) const override {
                    return id != source.id;
                }
                bool ShouldCollideLocked(const JPH::Body& other) const override {
                    const auto it = impl.bodies.find(other.GetUserData());
                    return it != impl.bodies.end() && it->second.hasBox &&
                           !(source.box.shape == ColliderShape::Mesh &&
                             it->second.box.shape == ColliderShape::Mesh);
                }
            } bodyFilter(impl, body);
            JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
            auto shape = impl.api().GetShape(body.id);
            impl.physics.GetNarrowPhaseQuery().CollideShape(
                shape,
                JPH::Vec3::sReplicate(1),
                impl.api().GetCenterOfMassTransform(body.id),
                settings,
                JPH::RVec3::sZero(),
                collector,
                {},
                {},
                bodyFilter);
            for (const auto& hit : collector.mHits) {
                if (hit.mBodyID2 == body.id) {
                    continue;
                }
                const auto otherKey = impl.api().GetUserData(hit.mBodyID2);
                const auto other = impl.bodies.find(otherKey);
                if (other == impl.bodies.end() || !other->second.hasBox) {
                    continue;
                }
                const auto key = std::minmax(item.first, otherKey);
                if (pairs.count(key)) {
                    continue;
                }
                auto x = body.entity, y = other->second.entity;
                pairs[key] = {x, y};
                const int phase = impl.triggerPairs.count(key) ? 1 : 0;
                triggerEvents.push_back({x, y, phase});
                triggerEvents.push_back({y, x, phase});
            }
        }
        for (const auto& old : impl.triggerPairs) {
            if (!pairs.count(old.first)) {
                auto [x, y] = old.second;
                triggerEvents.push_back({x, y, 2});
                triggerEvents.push_back({y, x, 2});
            }
        }
        impl.triggerPairs = std::move(pairs);
    }
}

std::vector<RayHit> Physics::raycastAll(const Ray& ray,
                                        float distance,
                                        const std::function<bool(Entity)>& filter) const {
    std::vector<RayHit> result;
    const auto dir = normalize(ray.direction);
    if (!isUsable(ray.origin) || !isUsable(dir) || dir == Vec3{} || !std::isfinite(distance) ||
        distance <= 0) {
        return result;
    }
    const float range = std::min(distance, kLargestNumber);
    JPH::RayCastSettings settings;
    settings.mTreatConvexAsSolid = false;
    settings.mBackFaceModeTriangles = JPH::EBackFaceMode::CollideWithBackFaces;
    JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
    impl_->physics.GetNarrowPhaseQuery().CastRay(
        JPH::RRayCast(toJolt(ray.origin), toJolt(dir * range)), settings, collector);
    std::map<std::uint64_t, RayHit> nearest;
    for (const auto& hit : collector.mHits) {
        if (hit.mFraction <= 0 || hit.mFraction > 1) {
            continue;
        }
        JPH::BodyLockRead lock(impl_->physics.GetBodyLockInterface(), hit.mBodyID);
        if (!lock.Succeeded()) {
            continue;
        }
        const auto& body = lock.GetBody();
        auto entity = entityOf(body.GetUserData());
        if (filter && !filter(entity)) {
            continue;
        }
        const Vec3 point = ray.origin + dir * (range * hit.mFraction);
        const auto normal =
            fromJolt(body.GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, toJolt(point)));
        auto key = keyOf(entity);
        auto found = nearest.find(key);
        if (found == nearest.end() || hit.mFraction * range < found->second.distance) {
            nearest[key] = {entity, point, normal, hit.mFraction * range};
        }
    }
    for (const auto& item : nearest) {
        result.push_back(item.second);
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.distance < b.distance;
    });
    return result;
}
bool Physics::raycast(const Ray& ray,
                      float distance,
                      RayHit& hit,
                      const std::function<bool(Entity)>& filter) const {
    const auto direction = normalize(ray.direction);
    if (!isUsable(ray.origin) || !isUsable(direction) || direction == Vec3{} ||
        !std::isfinite(distance) || distance <= 0) {
        return false;
    }
    struct EntityFilter final : JPH::BodyFilter {
        explicit EntityFilter(const std::function<bool(Entity)>& f) : filter(f) {}
        const std::function<bool(Entity)>& filter;
        bool ShouldCollideLocked(const JPH::Body& body) const override {
            return !filter || filter(entityOf(body.GetUserData()));
        }
    } entityFilter(filter);
    struct Closest final : JPH::CastRayCollector {
        JPH::RayCastResult result;
        bool found = false;
        void AddHit(const JPH::RayCastResult& value) override {
            if (value.mFraction > 0 && value.mFraction <= 1 &&
                (!found || value.mFraction < result.mFraction)) {
                result = value;
                found = true;
                UpdateEarlyOutFraction(value.mFraction);
            }
        }
    } collector;
    const float range = std::min(distance, kLargestNumber);
    JPH::RayCastSettings settings;
    settings.mTreatConvexAsSolid = false;
    settings.mBackFaceModeTriangles = JPH::EBackFaceMode::CollideWithBackFaces;
    impl_->physics.GetNarrowPhaseQuery().CastRay(
        JPH::RRayCast(toJolt(ray.origin), toJolt(direction * range)),
        settings,
        collector,
        {},
        {},
        entityFilter);
    if (!collector.found) {
        return false;
    }
    JPH::BodyLockRead lock(impl_->physics.GetBodyLockInterface(), collector.result.mBodyID);
    if (!lock.Succeeded()) {
        return false;
    }
    const auto& body = lock.GetBody();
    const float hitDistance = collector.result.mFraction * range;
    const Vec3 point = ray.origin + direction * hitDistance;
    hit = {entityOf(body.GetUserData()),
           point,
           fromJolt(body.GetWorldSpaceSurfaceNormal(collector.result.mSubShapeID2, toJolt(point))),
           hitDistance};
    return true;
}
std::vector<RayHit>
Physics::overlapSphere(Vec3 origin, float radius, const std::function<bool(Entity)>& filter) const {
    std::vector<RayHit> result;
    if (!isUsable(origin) || !std::isfinite(radius) || radius <= 0 || radius > kLargestNumber) {
        return result;
    }
    JPH::SphereShape sphere(radius);
    JPH::CollideShapeSettings settings;
    settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
    JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
    impl_->physics.GetNarrowPhaseQuery().CollideShape(&sphere,
                                                      JPH::Vec3::sReplicate(1),
                                                      JPH::RMat44::sTranslation(toJolt(origin)),
                                                      settings,
                                                      JPH::RVec3::sZero(),
                                                      collector);
    std::map<std::uint64_t, std::pair<Entity, Vec3>> unique;
    for (const auto& hit : collector.mHits) {
        JPH::BodyLockRead lock(impl_->physics.GetBodyLockInterface(), hit.mBodyID2);
        if (!lock.Succeeded()) {
            continue;
        }
        const auto& native = lock.GetBody();
        auto entity = entityOf(native.GetUserData());
        if (filter && !filter(entity)) {
            continue;
        }
        auto found = impl_->bodies.find(keyOf(entity));
        if (found == impl_->bodies.end() || !found->second.hasBox) {
            continue;
        }
        const auto center =
            native.GetPosition() + native.GetRotation() * toJolt(found->second.box.center);
        unique[keyOf(entity)] = {entity, fromJolt(center)};
    }
    for (const auto& item : unique) {
        const auto entity = item.second.first;
        const Vec3 delta = item.second.second - origin;
        RayHit hit{entity, origin, {}, 0};
        const float length2 = dot(delta, delta);
        if (length2 > 1e-8f) {
            RayHit surface;
            if (raycast({origin, normalize(delta)},
                        radius + std::sqrt(length2),
                        surface,
                        [&](Entity candidate) { return candidate == entity; })) {
                hit = surface;
            }
        }
        result.push_back(hit);
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.distance < b.distance;
    });
    return result;
}
bool Physics::checkBox(Vec3 center, Vec3 size) const {
    if (!isUsable(center) || !isUsable(size)) {
        return false;
    }
    JPH::BoxShape box(toJolt(Vec3{std::max(std::abs(size.x) * .5f, .0005f),
                                  std::max(std::abs(size.y) * .5f, .0005f),
                                  std::max(std::abs(size.z) * .5f, .0005f)}),
                      0.f);
    JPH::CollideShapeSettings settings;
    settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;
    JPH::AnyHitCollisionCollector<JPH::CollideShapeCollector> collector;
    impl_->physics.GetNarrowPhaseQuery().CollideShape(&box,
                                                      JPH::Vec3::sReplicate(1),
                                                      JPH::RMat44::sTranslation(toJolt(center)),
                                                      settings,
                                                      JPH::RVec3::sZero(),
                                                      collector);
    return collector.HadHit();
}
void Physics::setAssetRoot(const std::filesystem::path& root) {
    if (impl_->assets.root != root) {
        impl_->assets.root = root;
        impl_->hullPoints.clear();
        for (auto& item : impl_->bodies) {
            if (item.second.box.shape == ColliderShape::Mesh) {
                impl_->release(item.second);
                item.second.hullInvalid = false;
            }
        }
    }
}
void Physics::reset() {
    impl_ = std::make_unique<Impl>();
    triggerEvents.clear();
    collisionEvents.clear();
}
