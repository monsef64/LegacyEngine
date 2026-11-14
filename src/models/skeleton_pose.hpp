#pragma once
#include "models/model_pose.hpp"

// Decode JSON once, not once per node/mesh/view/frame. Preserve the affine
// residual of imported bind transforms (FBX pivots, shear, reflected axes).
inline Mat4 inverseModelAffine(const Mat4& m) {
    Vec3 a{m.m[0], m.m[1], m.m[2]}, b{m.m[4], m.m[5], m.m[6]}, c{m.m[8], m.m[9], m.m[10]};
    float det = dot(a, cross(b, c));
    if (!std::isfinite(det) || std::abs(det) < 1e-15f) {
        throw std::runtime_error("Singular skeleton bind transform");
    }
    auto x = cross(b, c) * (1 / det), y = cross(c, a) * (1 / det), z = cross(a, b) * (1 / det);
    Mat4 r = transformMatrix({}, Quat{}, Vec3{1, 1, 1});
    r.m[0] = x.x;
    r.m[4] = x.y;
    r.m[8] = x.z;
    r.m[1] = y.x;
    r.m[5] = y.y;
    r.m[9] = y.z;
    r.m[2] = z.x;
    r.m[6] = z.y;
    r.m[10] = z.z;
    auto t = modelPoint(r, {m.m[12], m.m[13], m.m[14]}, true) * -1;
    r.m[12] = t.x;
    r.m[13] = t.y;
    r.m[14] = t.z;
    return r;
}
class SkeletonPose {
    struct Key {
        float time;
        std::array<float, 4> value;
    };
    struct Channel {
        int node;
        std::vector<Key> p, r, s;
    };
    std::vector<int> parents_;
    std::vector<ModelTRS> bind_;
    std::vector<Mat4> matrices_, residual_;
    std::vector<std::vector<Channel>> clips_;
    static std::vector<Key> decode(const Json& keys, int lanes) {
        if (!keys.is_array() || keys.size() > 1000000) {
            throw std::runtime_error("Invalid skeleton keys");
        }
        std::vector<Key> result;
        result.reserve(keys.size());
        float last = -INFINITY;
        for (const auto& k : keys) {
            if (!k.is_array() || k.size() != std::size_t(lanes + 1)) {
                throw std::runtime_error("Invalid skeleton key");
            }
            Key key{k[0], {}};
            if (!std::isfinite(key.time) || key.time < last) {
                throw std::runtime_error("Unordered skeleton keys");
            }
            last = key.time;
            for (int i = 0; i < lanes; ++i) {
                key.value[i] = k[i + 1];
                if (!std::isfinite(key.value[i])) {
                    throw std::runtime_error("Invalid skeleton value");
                }
            }
            result.push_back(key);
        }
        return result;
    }
    static std::array<float, 4> sample(const std::vector<Key>& keys,
                                       float time,
                                       std::array<float, 4> fallback,
                                       bool rotation = false) {
        if (keys.empty()) {
            return fallback;
        }
        auto b = std::upper_bound(
            keys.begin(), keys.end(), time, [](float t, const Key& key) { return t < key.time; });
        if (b == keys.begin()) {
            return b->value;
        }
        if (b == keys.end()) {
            return keys.back().value;
        }
        auto a = b - 1;
        float t = (time - a->time) / (b->time - a->time);
        if (rotation) {
            auto q = nlerp(Quat{a->value[0], a->value[1], a->value[2], a->value[3]},
                           Quat{b->value[0], b->value[1], b->value[2], b->value[3]},
                           t);
            return {q.x, q.y, q.z, q.w};
        }
        auto value = a->value;
        for (int i = 0; i < 3; ++i) {
            value[i] += (b->value[i] - value[i]) * t;
        }
        return value;
    }

  public:
    explicit SkeletonPose(const Json& root) {
        bind_ = modelLocalTRS(root, -1, 0);
        const auto& nodes = root.at("nodes");
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            int p = nodes[i].at("parent");
            if (p < -1 || p >= int(i)) {
                throw std::runtime_error("Invalid skeleton parent");
            }
            parents_.push_back(p);
            auto matrix = modelMatrix(nodes[i].at("matrix"));
            matrices_.push_back(matrix);
            const auto& b = bind_[i];
            if (std::abs(b.scale.x) < 1e-10f || std::abs(b.scale.y) < 1e-10f ||
                std::abs(b.scale.z) < 1e-10f) {
                residual_.push_back(transformMatrix({}, Quat{}, {1, 1, 1}));
            } else {
                residual_.push_back(multiply(
                    inverseModelAffine(transformMatrix(b.position, b.rotation, b.scale)), matrix));
            }
        }
        const auto& clips = root.at("animations");
        if (!clips.is_array() || clips.size() > 256) {
            throw std::runtime_error("Invalid skeleton clips");
        }
        for (const auto& clip : clips) {
            std::vector<Channel> channels;
            const auto& data = clip.at("channels");
            if (!data.is_array() || data.size() > 4096) {
                throw std::runtime_error("Invalid animation channels");
            }
            for (const auto& c : data) {
                int i = c.at("node");
                if (i < 0 || std::size_t(i) >= bind_.size()) {
                    throw std::runtime_error("Invalid animation node");
                }
                channels.push_back({i,
                                    decode(c.at("positions"), 3),
                                    decode(c.at("rotations"), 4),
                                    decode(c.at("scales"), 3)});
            }
            clips_.push_back(std::move(channels));
        }
    }
    std::vector<ModelTRS> local(int clip, float time) const {
        auto result = bind_;
        if (clip < 0) {
            return result;
        }
        if (std::size_t(clip) >= clips_.size()) {
            throw std::runtime_error("Invalid animation index");
        }
        for (const auto& c : clips_[clip]) {
            auto& v = result[c.node];
            auto p = sample(c.p, time, {v.position.x, v.position.y, v.position.z, 0});
            auto r =
                sample(c.r, time, {v.rotation.x, v.rotation.y, v.rotation.z, v.rotation.w}, true);
            auto s = sample(c.s, time, {v.scale.x, v.scale.y, v.scale.z, 0});
            v = {{p[0], p[1], p[2]}, {s[0], s[1], s[2]}, normalize(Quat{r[0], r[1], r[2], r[3]})};
        }
        return result;
    }
    std::vector<Mat4> evaluate(const Animation& a) const {
        auto target = local(a.modelAnimation, a.time);
        if (a.blendWeight < 1) {
            auto from = a.blendFrozenPose.empty() ? local(a.blendSourceAnimation, a.blendSourceTime)
                                                  : a.blendFrozenPose;
            if (from.size() != target.size()) {
                throw std::runtime_error("Invalid blend pose");
            }
            for (std::size_t i = 0; i < target.size(); ++i) {
                auto& v = target[i];
                float t = std::clamp(a.blendWeight, 0.f, 1.f);
                v = {from[i].position + (v.position - from[i].position) * t,
                     from[i].scale + (v.scale - from[i].scale) * t,
                     nlerp(from[i].rotation, v.rotation, t)};
            }
        }
        std::vector<bool> affected(target.size(), false);
        auto mark = [&](int clip) {
            if (clip >= 0 && std::size_t(clip) < clips_.size()) {
                for (const auto& c : clips_[clip]) {
                    affected[c.node] = true;
                }
            }
        };
        mark(a.modelAnimation);
        if (a.blendWeight < 1) {
            if (a.blendFrozenPose.empty()) {
                mark(a.blendSourceAnimation);
            } else {
                std::fill(affected.begin(), affected.end(), true);
            }
        }
        std::vector<Mat4> global;
        global.reserve(target.size());
        for (std::size_t i = 0; i < target.size(); ++i) {
            const auto& v = target[i];
            auto m = multiply(transformMatrix(v.position, v.rotation, v.scale), residual_[i]);
            if (!affected[i]) {
                m = matrices_[i];
            }
            global.push_back(parents_[i] < 0 ? m : multiply(global[parents_[i]], m));
        }
        return global;
    }
};
