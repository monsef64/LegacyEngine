#pragma once
#include "core/hierarchy.hpp"
#include "models/model_asset.hpp"

namespace BoneAttachments {
inline const Json& skeleton(const AssetManager& assets, const std::string& path) {
    static std::map<std::string, Json> docs;
    static std::filesystem::path root;
    static unsigned revision = ~0u;
    if (root != assets.root || revision != modelAssetRevision) {
        docs.clear();
        root = assets.root;
        revision = modelAssetRevision;
    }
    if (docs.size() > 128) {
        docs.clear();
    }
    const auto& part = cachedModelDocument(assets, path, &docs);
    auto owner = part.value("settings", std::string{});
    return owner.empty() ? part : cachedModelDocument(assets, owner, &docs);
}
inline std::string sourceModel(World& world, Entity source) {
    if (const auto* a = world.tryGet<Animation>(source); a && !a->model.empty()) {
        return a->model;
    }
    if (const auto* m = world.tryGet<MeshRenderer>(source); m && !m->model.empty()) {
        return m->model;
    }
    for (auto e : world.entities()) {
        if (const auto* m = world.tryGet<MeshRenderer>(e); m && !m->model.empty()) {
            auto p = getParent(world, e);
            for (unsigned depth = 0; world.alive(p) && depth < 128; ++depth) {
                if (p == source) {
                    return m->model;
                }
                p = getParent(world, p);
            }
        }
    }
    return {};
}
inline void update(World& world, const AssetManager& assets) {
    world.each<BoneAttachment>([&](Entity e, BoneAttachment& b) {
        b.error.clear();
        if (!b.enabled) {
            return;
        }
        try {
            if (!world.has<Transform>(e) || !world.has<Transform>(b.source) || b.source == e) {
                throw std::runtime_error("Select another animated model as source");
            }
            if (const auto* rb = world.tryGet<RigidBody>(e);
                rb && rb->mass > 0 && !rb->isKinematic) {
                throw std::runtime_error("Attached objects must not have a dynamic RigidBody");
            }
            // Reject dependency chains/cycles, including source descendants.
            auto ancestor = b.source;
            for (unsigned depth = 0; world.alive(ancestor) && depth < 128; ++depth) {
                if (ancestor == e || world.has<BoneAttachment>(ancestor)) {
                    throw std::runtime_error("Attachment dependency cycle/chain is not supported");
                }
                ancestor = getParent(world, ancestor);
            }
            auto path = sourceModel(world, b.source);
            if (path.empty()) {
                throw std::runtime_error("Source needs an imported skeletal model");
            }
            const auto& doc = skeleton(assets, path);
            const Animation* animation = nullptr;
            auto animationOwner = b.source;
            for (unsigned depth = 0; world.alive(animationOwner) && depth < 128; ++depth) {
                if (const auto* candidate = world.tryGet<Animation>(animationOwner);
                    candidate && candidate->poseActive) {
                    animation = candidate;
                    break;
                }
                animationOwner = getParent(world, animationOwner);
            }
            const bool active = animation != nullptr;
            auto pose = modelGlobalPose(doc,
                                        active ? animation->modelAnimation : -1,
                                        active ? animation->time : 0,
                                        active ? animation->blendSourceAnimation : -1,
                                        active ? animation->blendSourceTime : 0,
                                        active ? animation->blendWeight : 1,
                                        active ? &animation->blendFrozenPose : nullptr);
            if (b.node < 0 || size_t(b.node) >= pose.size()) {
                throw std::runtime_error("Select a valid skeleton node/bone");
            }
            const auto source = world.get<Transform>(b.source);
            Mat4 matrix{};
            // Skeleton translation uses model units, adjusted by import scale.
            const float importScale = modelImportScale(doc);
            matrix = multiply(
                transformMatrix(source.position, source.rotation, source.scale * importScale),
                pose[b.node]);
            Vec3 x = normalize(Vec3{matrix.m[0], matrix.m[1], matrix.m[2]});
            Vec3 y = Vec3{matrix.m[4], matrix.m[5], matrix.m[6]};
            y = normalize(y - x * dot(x, y));
            Vec3 z = normalize(cross(x, y));
            if (dot(x, x) < .5f || dot(y, y) < .5f) {
                throw std::runtime_error("Source bone has zero scale");
            }
            // Orthonormal basis to quaternion, robust near 180 degrees.
            Quat q;
            float trace = x.x + y.y + z.z;
            if (trace > 0) {
                float s = 2 * std::sqrt(trace + 1);
                q = {(y.z - z.y) / s, (z.x - x.z) / s, (x.y - y.x) / s, .25f * s};
            } else if (x.x > y.y && x.x > z.z) {
                float s = 2 * std::sqrt(1 + x.x - y.y - z.z);
                q = {.25f * s, (y.x + x.y) / s, (z.x + x.z) / s, (y.z - z.y) / s};
            } else if (y.y > z.z) {
                float s = 2 * std::sqrt(1 + y.y - x.x - z.z);
                q = {(y.x + x.y) / s, .25f * s, (z.y + y.z) / s, (z.x - x.z) / s};
            } else {
                float s = 2 * std::sqrt(1 + z.z - x.x - y.y);
                q = {(z.x + x.z) / s, (z.y + y.z) / s, .25f * s, (x.y - y.x) / s};
            }
            auto offsetRotation =
                composeRotation(composeRotation(axisAngle({0, 1, 0}, radians(b.rotation.y)),
                                                axisAngle({1, 0, 0}, radians(b.rotation.x))),
                                axisAngle({0, 0, 1}, radians(b.rotation.z)));
            auto& target = world.get<Transform>(e);
            target.position = modelPoint(matrix, b.offset);
            target.rotation = composeRotation(normalize(q), offsetRotation);
            if (auto* parent = world.tryGet<Parent>(e)) {
                parent->previous = world.get<Transform>(parent->entity);
            }
        } catch (const std::exception& ex) {
            b.error = ex.what();
        }
    });
}
} // namespace BoneAttachments
