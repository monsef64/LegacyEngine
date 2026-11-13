#include "physics/physics.hpp"

#include "core/components.hpp"

#include <reactphysics3d/reactphysics3d.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <unordered_map>

namespace {

// The simulation only ever advances in steps of this length. Steps of equal
// length keep it stable and make it behave the same on a fast and on a slow
// computer.
constexpr float kStepSeconds = 1.0f / 60.0f;

// A frame that took longer than this (the window was being dragged, say) counts
// as this long, so that the simulation does not try to catch up all at once.
constexpr float kLongestFrameSeconds = 0.25f;

// ReactPhysics3D needs boxes with some thickness on every axis.
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

rp3d::Vector3 toRp3d(Vec3 v) {
    return rp3d::Vector3(rp3d::decimal(v.x), rp3d::decimal(v.y), rp3d::decimal(v.z));
}

rp3d::Transform toRp3d(const Pose& pose) {
    const Quat& q = pose.rotation;
    return rp3d::Transform(
        toRp3d(pose.position),
        rp3d::Quaternion(
            rp3d::decimal(q.x), rp3d::decimal(q.y), rp3d::decimal(q.z), rp3d::decimal(q.w)));
}

Vec3 toVec3(const rp3d::Vector3& v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

Pose toPose(const rp3d::Transform& transform) {
    const rp3d::Quaternion& q = transform.getOrientation();
    return {toVec3(transform.getPosition()),
            {static_cast<float>(q.x),
             static_cast<float>(q.y),
             static_cast<float>(q.z),
             static_cast<float>(q.w)}};
}

// ReactPhysics3D only accepts rotations of length 1.
Pose toPose(const Transform& transform) {
    return {transform.position, normalize(transform.rotation)};
}

// A BoxCollider as the simulation sees it: with the entity's scale applied and
// with values it can work with.
struct Box {
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
    if (rigidBody && (!isUsable(rigidBody->mass) || !isUsable(rigidBody->linearVelocity) ||
                      !isUsable(rigidBody->angularVelocity))) {
        return false;
    }
    return true;
}

std::uint64_t keyOf(Entity entity) {
    return (static_cast<std::uint64_t>(entity.generation) << 32) | entity.index;
}

// A box anywhere in the world, turned any way.
struct OrientedBox {
    Vec3 center;
    Vec3 axes[3];      // unit vectors along its edges
    float halfSize[3]; // half its length along each of them
};

OrientedBox toOrientedBox(const Pose& pose, const Box& box) {
    const Mat3 rotation = rotationMatrix(pose.rotation);
    OrientedBox result;
    for (int i = 0; i < 3; ++i) {
        result.axes[i] = {rotation.m[i * 3], rotation.m[i * 3 + 1], rotation.m[i * 3 + 2]};
    }
    result.center = pose.position + result.axes[0] * box.center.x + result.axes[1] * box.center.y +
                    result.axes[2] * box.center.z;
    result.halfSize[0] = box.halfSize.x;
    result.halfSize[1] = box.halfSize.y;
    result.halfSize[2] = box.halfSize.z;
    return result;
}

// Where a ray enters a box: how far along the ray, and through a side that
// faces which way. `direction` is a unit vector. Returns false if the ray
// misses the box or starts inside it.
bool rayEnters(Vec3 origin, Vec3 direction, const OrientedBox& box, float& distance, Vec3& normal) {
    // A box is what three slabs have in common, each one the space between
    // two opposite sides. The ray is inside the box where it is inside all
    // three slabs: from the last slab it enters to the first one it leaves.
    const Vec3 offset = origin - box.center;
    float enters = -INFINITY;
    float leaves = INFINITY;
    for (int i = 0; i < 3; ++i) {
        const float position = dot(offset, box.axes[i]);
        const float speed = dot(direction, box.axes[i]);
        if (speed == 0.0f) {
            if (std::abs(position) > box.halfSize[i]) {
                return false; // it runs alongside this slab, outside it
            }
            continue;
        }
        const float one = (-box.halfSize[i] - position) / speed;
        const float two = (box.halfSize[i] - position) / speed;
        if (std::min(one, two) > enters) {
            enters = std::min(one, two);
            normal = box.axes[i] * (speed > 0.0f ? -1.0f : 1.0f);
        }
        leaves = std::min(leaves, std::max(one, two));
    }
    if (!(enters > 0.0f) || enters > leaves) {
        return false;
    }
    distance = enters;
    return true;
}

// Whether two boxes reach into each other or touch. The edges of the first one
// lie along the world's axes.
bool boxesMeet(Vec3 center, Vec3 halfSize, const OrientedBox& box) {
    // Two boxes are apart exactly when they are apart as seen along one of 15
    // lines: the three edge directions of each box, and the nine directions
    // at right angles to an edge of each (the separating axis theorem).
    static const Vec3 kWorldAxes[3] = {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}};

    const Vec3 offset = box.center - center;
    const auto apartAlong = [&](Vec3 line) {
        // How far the two boxes reach along the line, each from its centre,
        // against how far apart their centres are along it.
        const float reach = halfSize.x * std::abs(line.x) + halfSize.y * std::abs(line.y) +
                            halfSize.z * std::abs(line.z) +
                            box.halfSize[0] * std::abs(dot(box.axes[0], line)) +
                            box.halfSize[1] * std::abs(dot(box.axes[1], line)) +
                            box.halfSize[2] * std::abs(dot(box.axes[2], line));
        return std::abs(dot(offset, line)) > reach;
    };

    for (int i = 0; i < 3; ++i) {
        if (apartAlong(kWorldAxes[i]) || apartAlong(box.axes[i])) {
            return false;
        }
    }
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) {
            // Two edges that are as good as parallel have no direction at
            // right angles to both that the lines above do not cover.
            const Vec3 line = cross(kWorldAxes[i], box.axes[j]);
            if (dot(line, line) > 1.0e-6f && apartAlong(line)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

struct Physics::Impl {
    // What the simulation holds for one entity.
    struct Body {
        Entity entity;
        rp3d::RigidBody* body = nullptr;       // null while the entity is left out (see update())
        rp3d::Collider* collider = nullptr;    // null while the entity has no BoxCollider
        rp3d::CollisionShape* shape = nullptr; // the shape of `collider`

        // The values of the entity's components that the simulation is in step
        // with. A component that differs from them was changed by game code
        // since the last update.
        Vec3 position;
        Quat rotation;
        RigidBody rigidBody;
        Box box;
        bool hasBox = false;
        bool dynamic = false; // the entity has a RigidBody

        // Where the simulation had the body one step ago, and where it has it
        // now. What is shown lies in between (see update()).
        Pose previous;
        Pose current;

        bool visited = false;  // the entity still exists and still takes part
        bool reported = false; // its unusable values have been reported
        bool failed = false;   // the last step gave it unusable values (see update())
    };

    rp3d::PhysicsCommon common;
    rp3d::PhysicsWorld* world = nullptr;
    rp3d::ConvexMesh* coneMesh = nullptr;

    std::unordered_map<std::uint64_t, Body> bodies;
    float pendingSeconds = 0.0f; // time that has passed but is not simulated yet

    // ReactPhysics3D puts bodies that lie still to sleep: it leaves them out of
    // its calculations until something bumps into them. A sleeping body does
    // not notice that the thing it rests on was moved or taken away, so after
    // a change of that kind every body is woken.
    bool wakeAll = false;

    Impl() {
        rp3d::PhysicsWorld::WorldSettings settings;
        settings.gravity = rp3d::Vector3(0, rp3d::decimal(-9.81), 0); // metres per second squared
        world = common.createPhysicsWorld(settings);
        const auto vertices = coneHullVertices();
        const rp3d::VertexArray array(vertices.data(),
                                      sizeof(Vec3),
                                      static_cast<rp3d::uint32>(vertices.size()),
                                      rp3d::VertexArray::DataType::VERTEX_FLOAT_TYPE);
        std::vector<rp3d::Message> messages;
        coneMesh = common.createConvexMesh(array, messages);
    }

    // `common` frees the world and everything in it when it is destroyed.

    Body& bodyOf(Entity entity, const Transform& transform) {
        const auto found = bodies.find(keyOf(entity));
        if (found != bodies.end()) {
            return found->second;
        }

        Body body;
        body.entity = entity;
        body.position = transform.position;
        body.rotation = transform.rotation;
        body.current = toPose(transform);
        body.previous = body.current;
        return bodies.emplace(keyOf(entity), body).first->second;
    }

    // Takes an entity's body out of the simulation.
    void release(Body& body) {
        if (!body.body) {
            return;
        }
        world->destroyRigidBody(body.body); // takes the collider with it
        if (body.shape) {
            switch (body.box.shape) {
            case ColliderShape::Box:
                common.destroyBoxShape(static_cast<rp3d::BoxShape*>(body.shape));
                break;
            case ColliderShape::Sphere:
                common.destroySphereShape(static_cast<rp3d::SphereShape*>(body.shape));
                break;
            case ColliderShape::Capsule:
                common.destroyCapsuleShape(static_cast<rp3d::CapsuleShape*>(body.shape));
                break;
            case ColliderShape::Cone:
                common.destroyConvexMeshShape(static_cast<rp3d::ConvexMeshShape*>(body.shape));
                break;
            }
        }
        body.body = nullptr;
        body.collider = nullptr;
        body.shape = nullptr;
        wakeAll = true;
    }

    // Puts an entity's body into the simulation, made from scratch.
    void build(Body& body, const Box* box, const RigidBody* rigidBody) {
        release(body);
        body.body = world->createRigidBody(toRp3d(body.current));

        body.hasBox = box != nullptr;
        body.box = box ? *box : Box{};
        if (box) {
            const rp3d::Transform place(toRp3d(box->center), rp3d::Quaternion::identity());
            switch (box->shape) {
            case ColliderShape::Box:
                body.shape = common.createBoxShape(toRp3d(box->halfSize));
                break;
            case ColliderShape::Sphere:
                body.shape = common.createSphereShape(box->radius);
                break;
            case ColliderShape::Capsule:
                body.shape = common.createCapsuleShape(box->radius, box->height);
                break;
            case ColliderShape::Cone:
                body.shape = common.createConvexMeshShape(
                    coneMesh, {box->radius * 2, box->height, box->radius * 2});
                break;
            }
            body.collider = body.body->addCollider(body.shape, place);
            body.collider->getMaterial().setFrictionCoefficient(box->friction);
            body.collider->getMaterial().setBounciness(box->bounciness);
        }

        // An entity without a RigidBody is an obstacle that never moves.
        body.dynamic = rigidBody != nullptr;
        body.rigidBody = rigidBody ? *rigidBody : RigidBody{};
        if (rigidBody) {
            applyMass(body);
            body.body->enableGravity(rigidBody->useGravity);
            body.body->setLinearVelocity(toRp3d(rigidBody->linearVelocity));
            body.body->setAngularVelocity(toRp3d(rigidBody->angularVelocity));
        } else {
            body.body->setType(rp3d::BodyType::STATIC);
        }
        wakeAll = true;
    }

    void applyMass(Body& body) {
        float mass = std::max(body.rigidBody.mass, 0.0f);
        if (mass > 0.0f) {
            mass = std::max(mass, kSmallestMass);
        }
        if (body.collider) {
            // ReactPhysics3D works out the mass, the centre of mass and the
            // resistance to spinning from the shape of the collider and its
            // density.
            body.collider->getMaterial().setMassDensity(
                mass / static_cast<rp3d::ConvexShape*>(body.shape)->getVolume());
            body.body->updateMassPropertiesFromColliders();
        } else {
            body.body->setMass(mass);
        }
    }

    // Takes over one number of a Transform if game code changed it: `written`
    // is what the Transform holds and `shown` what the last update put there.
    // Returns whether it did.
    static bool adopt(float written, float shown, float& current, float& previous) {
        if (written == shown && isUsable(current)) {
            return false;
        }
        current = written;
        previous = written; // nothing to blend: it did not travel there
        return true;
    }

    // Brings the simulation in step with the entity's components.
    void follow(Body& body,
                const Transform& transform,
                const BoxCollider* boxCollider,
                const RigidBody* rigidBody) {
        const bool dynamic = rigidBody != nullptr;
        const bool hasBox = boxCollider != nullptr;
        const Box box = hasBox ? toBox(*boxCollider, transform.scale) : Box{};

        bool moved = false;
        if (!dynamic) {
            // An obstacle is always exactly where its Transform says.
            moved = transform.position != body.position || transform.rotation != body.rotation;
            body.current = toPose(transform);
            body.previous = body.current;
        } else {
            // Game code moved the entity: the body jumps there. Only what was
            // changed is taken from the Transform, one number at a time. The
            // rest of it is not where the simulation has the body: it shows
            // the body on its way from one step to the next (see update()).
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
                body.current.rotation = normalize(transform.rotation);
                body.previous.rotation = body.current.rotation;
                moved = true;
            }
            if (!body.body) {
                body.previous = body.current; // it joins here: no earlier step to blend from
            }
        }
        body.position = transform.position;
        body.rotation = transform.rotation;

        // ReactPhysics3D 0.10.2 can crash when an obstacle is moved while a
        // sleeping body rests on it, or when a sleeping body is turned into an
        // obstacle (see issue 429 in its repository). A body made from scratch
        // has no such history, so for the changes that would need those calls,
        // and for a change of shape, the body is made again. They are rare.
        if (!body.body || dynamic != body.dynamic || hasBox != body.hasBox ||
            box.halfSize != body.box.halfSize || box.center != body.box.center ||
            box.shape != body.box.shape || box.radius != body.box.radius ||
            box.height != body.box.height || (moved && !dynamic)) {
            build(body, hasBox ? &box : nullptr, rigidBody);
            return;
        }

        // Everything else can be changed on the body as it is.
        if (hasBox &&
            (box.friction != body.box.friction || box.bounciness != body.box.bounciness)) {
            body.collider->getMaterial().setFrictionCoefficient(box.friction);
            body.collider->getMaterial().setBounciness(box.bounciness);
            body.box = box;
            wakeAll = true; // something asleep on a slope may now slide
        }
        if (!dynamic) {
            return;
        }

        const bool massChanged = rigidBody->mass != body.rigidBody.mass;
        if (moved || massChanged || rigidBody->useGravity != body.rigidBody.useGravity ||
            rigidBody->linearVelocity != body.rigidBody.linearVelocity ||
            rigidBody->angularVelocity != body.rigidBody.angularVelocity) {
            body.body->setIsSleeping(false); // first: only a body that is awake takes changes well
            body.rigidBody = *rigidBody;
            if (massChanged) {
                applyMass(body);
            }
            body.body->enableGravity(rigidBody->useGravity);
            if (moved) {
                body.body->setTransform(toRp3d(body.current));
                wakeAll = true;
            }
            // Last: moving a body can alter its velocity inside ReactPhysics3D.
            body.body->setLinearVelocity(toRp3d(rigidBody->linearVelocity));
            body.body->setAngularVelocity(toRp3d(rigidBody->angularVelocity));
        }
    }
};

Physics::Physics() : impl_(std::make_unique<Impl>()) {}

Physics::~Physics() = default;

void Physics::update(World& world, float seconds, const std::function<void(float)>& beforeStep) {
    Impl& impl = *impl_;

    const auto synchronize = [&]() {
        // 1. Pick up what game code did since the last update: new, changed and
        //    removed components, and destroyed entities.
        for (auto& entry : impl.bodies) {
            entry.second.visited = false;
        }
        world.each<Transform>([&](Entity entity, Transform& transform) {
            if (!isActive(world, entity)) {
                return;
            }
            const BoxCollider* boxCollider = world.tryGet<BoxCollider>(entity);
            const RigidBody* rigidBody = world.tryGet<RigidBody>(entity);
            if (!boxCollider && !rigidBody) {
                return; // takes no part in the physics
            }
            Impl::Body& body = impl.bodyOf(entity, transform);
            body.visited = true;
            if (isUsable(transform, boxCollider, rigidBody)) {
                impl.follow(body, transform, boxCollider, rigidBody);
                body.reported = false;
            } else {
                // Left out, and left alone, until game code has repaired it.
                impl.release(body);
                if (!body.reported) {
                    std::fprintf(
                        stderr,
                        "Physics: entity %u is left out of the simulation: a number in its "
                        "Transform, BoxCollider or RigidBody is NaN, infinite or beyond a "
                        "million\n",
                        static_cast<unsigned>(entity.index));
                    body.reported = true;
                }
            }
        });
        for (auto entry = impl.bodies.begin(); entry != impl.bodies.end();) {
            if (entry->second.visited) {
                ++entry;
            } else {
                impl.release(entry->second);
                entry = impl.bodies.erase(entry);
            }
        }
        if (impl.wakeAll) {
            for (auto& entry : impl.bodies) {
                if (entry.second.body) {
                    entry.second.body->setIsSleeping(false);
                }
            }
            impl.wakeAll = false;
        }
    };
    synchronize();

    // 2. Simulate the time that has passed, one fixed step at a time. What is
    //    left over, less than a step, waits for the next update.
    if (seconds > 0.0f) { // also false for NaN
        impl.pendingSeconds += std::min(seconds, kLongestFrameSeconds);
    }
    while (impl.pendingSeconds >= kStepSeconds) {
        impl.pendingSeconds -= kStepSeconds;
        if (beforeStep) {
            beforeStep(kStepSeconds);
            synchronize();
        }
        impl.world->update(kStepSeconds);
        for (auto& entry : impl.bodies) {
            Impl::Body& body = entry.second;
            if (!body.body || !body.dynamic) {
                continue;
            }
            body.previous = body.current;
            body.current = toPose(body.body->getTransform());
            body.rigidBody.linearVelocity = toVec3(body.body->getLinearVelocity());
            body.rigidBody.angularVelocity = toVec3(body.body->getAngularVelocity());

            // The step took the body out of the range the simulation can
            // work with: take it out at once, before it affects the bodies
            // it touches.
            if (!isUsable(body.current) || !isUsable(body.rigidBody.linearVelocity) ||
                !isUsable(body.rigidBody.angularVelocity)) {
                impl.release(body);
                body.failed = true;
            }
        }
    }

    // 3. Hand the results back to the components. The time that has passed is
    //    rarely a whole number of steps, so showing the latest step as it is
    //    would make the motion stutter. Each entity is shown on its way from
    //    the previous step to the latest one instead, as far along as the
    //    leftover time reaches into a step.
    const float blend = impl.pendingSeconds / kStepSeconds;
    for (auto& entry : impl.bodies) {
        Impl::Body& body = entry.second;
        if (!body.dynamic || !(body.body || body.failed)) {
            continue;
        }
        Transform& transform = world.get<Transform>(body.entity);
        if (body.failed) {
            // Shown as they are: the next update finds the entity unusable,
            // says so, and leaves it out until game code has repaired it.
            transform.position = body.current.position;
            transform.rotation = body.current.rotation;
            body.failed = false;
        } else {
            transform.position = lerp(body.previous.position, body.current.position, blend);
            transform.rotation = nlerp(body.previous.rotation, body.current.rotation, blend);
        }
        body.position = transform.position;
        body.rotation = transform.rotation;

        RigidBody& rigidBody = world.get<RigidBody>(body.entity);
        rigidBody.linearVelocity = body.rigidBody.linearVelocity;
        rigidBody.angularVelocity = body.rigidBody.angularVelocity;
    }
}

bool Physics::raycast(const Ray& ray, float maxDistance, RayHit& hit) const {
    const Vec3 direction = normalize(ray.direction);
    if (!isUsable(ray.origin) || !isUsable(direction) || direction == Vec3{} ||
        !(maxDistance > 0.0f)) { // also false for NaN
        return false;
    }

    bool found = false;
    for (const auto& entry : impl_->bodies) {
        const Impl::Body& body = entry.second;
        if (!body.body || !body.hasBox) {
            continue; // left out of the simulation, or nothing solid about it
        }
        if (body.box.shape != ColliderShape::Box) {
            const float reach = std::min(maxDistance, kLargestNumber);
            rp3d::RaycastInfo info;
            if (body.collider->raycast(
                    rp3d::Ray(toRp3d(ray.origin), toRp3d(ray.origin + direction * reach)), info)) {
                const float distance = static_cast<float>(info.hitFraction) * reach;
                if (!found || distance <= hit.distance) {
                    hit = {
                        body.entity, toVec3(info.worldPoint), toVec3(info.worldNormal), distance};
                    found = true;
                }
            }
            continue;
        }
        float distance = 0.0f;
        Vec3 normal;
        if (rayEnters(
                ray.origin, direction, toOrientedBox(body.current, body.box), distance, normal) &&
            distance <= (found ? hit.distance : maxDistance)) {
            hit = {body.entity, ray.origin + direction * distance, normal, distance};
            found = true;
        }
    }
    return found;
}

bool Physics::checkBox(Vec3 center, Vec3 size) const {
    if (!isUsable(center) || !isUsable(size)) {
        return false;
    }
    const Vec3 halfSize = {
        std::abs(size.x) * 0.5f, std::abs(size.y) * 0.5f, std::abs(size.z) * 0.5f};

    for (const auto& entry : impl_->bodies) {
        const Impl::Body& body = entry.second;
        if (body.body && body.hasBox && body.box.shape != ColliderShape::Box) {
            // Use the actual curved collider, not its bounding box.
            auto* shape = impl_->common.createBoxShape(toRp3d(Vec3{std::max(halfSize.x, 0.0005f),
                                                                   std::max(halfSize.y, 0.0005f),
                                                                   std::max(halfSize.z, 0.0005f)}));
            auto* probe = impl_->world->createRigidBody(toRp3d(Pose{center, {}}));
            probe->setType(rp3d::BodyType::STATIC);
            probe->addCollider(shape, rp3d::Transform::identity());
            const bool overlaps = impl_->world->testOverlap(probe, body.body);
            impl_->world->destroyRigidBody(probe);
            impl_->common.destroyBoxShape(shape);
            if (overlaps) {
                return true;
            }
            continue;
        }
        if (body.body && body.hasBox &&
            boxesMeet(center, halfSize, toOrientedBox(body.current, body.box))) {
            return true;
        }
    }
    return false;
}

void Physics::reset() {
    impl_ = std::make_unique<Impl>();
}
