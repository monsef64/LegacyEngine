#pragma once
#include "animation/animation_component.hpp"
#include "project/json_io.hpp"
#include "rendering/mesh.hpp"
#include <algorithm>
#include <array>
#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#include <xmmintrin.h>
#endif

inline Mat4 modelMatrix(const Json& value) {
    if (!value.is_array() || value.size() != 16) {
        throw std::runtime_error("Invalid skeleton matrix");
    }
    Mat4 m{};
    for (int i = 0; i < 16; ++i) {
        m.m[i] = value[i].get<float>();
        if (!std::isfinite(m.m[i])) {
            throw std::runtime_error("Invalid skeleton matrix");
        }
    }
    return m;
}
inline Vec3 modelPointScalar(const Mat4& m, Vec3 p, bool direction = false) {
    const float w = direction ? 0.f : 1.f;
    return {m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12] * w,
            m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13] * w,
            m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14] * w};
}
inline Vec3 modelPoint(const Mat4& m, Vec3 p, bool direction = false) {
#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
    // Mat4 is column-major. Evaluate the three output coordinates together,
    // keeping the scalar operation order and allowing unaligned matrix storage.
    const __m128 x = _mm_mul_ps(_mm_loadu_ps(m.m), _mm_set1_ps(p.x));
    const __m128 y = _mm_mul_ps(_mm_loadu_ps(m.m + 4), _mm_set1_ps(p.y));
    const __m128 z = _mm_mul_ps(_mm_loadu_ps(m.m + 8), _mm_set1_ps(p.z));
    const __m128 w = _mm_mul_ps(_mm_loadu_ps(m.m + 12), _mm_set1_ps(direction ? 0.f : 1.f));
    float values[4];
    _mm_storeu_ps(values, _mm_add_ps(_mm_add_ps(_mm_add_ps(x, y), z), w));
    return {values[0], values[1], values[2]};
#else
    return modelPointScalar(m, p, direction);
#endif
}
// Inverse-transpose normal transform, including nonuniform node/bone scale.
inline Vec3 modelNormal(const Mat4& m, Vec3 n) {
    const Vec3 a{m.m[0], m.m[1], m.m[2]}, b{m.m[4], m.m[5], m.m[6]}, c{m.m[8], m.m[9], m.m[10]};
    const float determinant = dot(a, cross(b, c));
    if (std::abs(determinant) < 1e-12f) {
        return modelPoint(m, n, true);
    }
    return (cross(b, c) * n.x + cross(c, a) * n.y + cross(a, b) * n.z) * (1 / determinant);
}
inline std::array<float, 4>
modelSample(const Json& keys, float time, std::array<float, 4> fallback, bool rotation = false) {
    if (!keys.is_array() || keys.size() > 1000000) {
        throw std::runtime_error("Invalid skeleton keys");
    }
    if (keys.empty()) {
        return fallback;
    }
    auto read = [&](const Json& k) {
        if (!k.is_array() || k.size() != std::size_t(rotation ? 5 : 4)) {
            throw std::runtime_error("Invalid skeleton key");
        }
        std::array<float, 4> v{};
        for (int i = 0; i < (rotation ? 4 : 3); ++i) {
            v[i] = k[i + 1].get<float>();
            if (!std::isfinite(v[i])) {
                throw std::runtime_error("Invalid skeleton value");
            }
        }
        return v;
    };
    auto it = std::upper_bound(keys.begin(), keys.end(), time, [](float t, const Json& k) {
        return t < k.at(0).get<float>();
    });
    if (it == keys.begin()) {
        return read(*it);
    }
    if (it == keys.end()) {
        return read(keys.back());
    }
    const auto& a = *(it - 1);
    const auto& b = *it;
    auto av = read(a), bv = read(b);
    float span = b[0].get<float>() - a[0].get<float>();
    float t = span > 0 ? std::clamp((time - a[0].get<float>()) / span, 0.f, 1.f) : 0;
    if (rotation) {
        auto q = nlerp(Quat{av[0], av[1], av[2], av[3]}, Quat{bv[0], bv[1], bv[2], bv[3]}, t);
        return {q.x, q.y, q.z, q.w};
    }
    for (int i = 0; i < 3; ++i) {
        av[i] += (bv[i] - av[i]) * t;
    }
    return av;
}
using ModelTRS = AnimationPose;
inline std::vector<ModelTRS> modelLocalTRS(const Json& root, int animation, float time) {
    const auto& nodes = root.at("nodes");
    std::vector<ModelTRS> poses;
    if (!nodes.is_array() || nodes.empty() || nodes.size() > 4096) {
        throw std::runtime_error("Invalid skeleton");
    }
    for (const auto& n : nodes) {
        poses.push_back(
            {{n.at("position")[0], n.at("position")[1], n.at("position")[2]},
             {n.at("scale")[0], n.at("scale")[1], n.at("scale")[2]},
             {n.at("rotation")[0], n.at("rotation")[1], n.at("rotation")[2], n.at("rotation")[3]}});
    }
    if (animation < 0) {
        return poses;
    }
    const auto& clips = root.at("animations");
    if (!clips.is_array() || clips.size() > 256 || std::size_t(animation) >= clips.size()) {
        throw std::runtime_error("Invalid animation");
    }
    const auto& channels = clips[animation].at("channels");
    if (!channels.is_array() || channels.size() > 4096) {
        throw std::runtime_error("Invalid channels");
    }
    for (const auto& c : channels) {
        int i = c.at("node");
        if (i < 0 || std::size_t(i) >= poses.size()) {
            throw std::runtime_error("Invalid animation node");
        }
        auto& pose = poses[i];
        auto p = modelSample(
            c.at("positions"), time, {pose.position.x, pose.position.y, pose.position.z, 0});
        auto s = modelSample(c.at("scales"), time, {pose.scale.x, pose.scale.y, pose.scale.z, 0});
        auto r = modelSample(c.at("rotations"),
                             time,
                             {pose.rotation.x, pose.rotation.y, pose.rotation.z, pose.rotation.w},
                             true);
        pose = {{p[0], p[1], p[2]}, {s[0], s[1], s[2]}, normalize(Quat{r[0], r[1], r[2], r[3]})};
    }
    return poses;
}
inline std::vector<Mat4> modelGlobalPose(const Json& root,
                                         int animation,
                                         float time,
                                         int sourceAnimation = -1,
                                         float sourceTime = 0,
                                         float blend = 1,
                                         const std::vector<ModelTRS>* frozen = nullptr) {
    const auto& nodes = root.at("nodes");
    if (!nodes.is_array() || nodes.empty() || nodes.size() > 4096) {
        throw std::runtime_error("Invalid model skeleton");
    }
    std::vector<Mat4> local, global;
    for (const auto& n : nodes) {
        local.push_back(modelMatrix(n.at("matrix")));
    }
    if (animation >= 0) {
        const auto& clips = root.at("animations");
        if (!clips.is_array() || clips.size() > 256 || std::size_t(animation) >= clips.size()) {
            throw std::runtime_error("Invalid model animation");
        }
        const auto& channels = clips[animation].at("channels");
        if (!channels.is_array() || channels.size() > 4096) {
            throw std::runtime_error("Invalid animation channels");
        }
        for (const auto& c : channels) {
            int index = c.at("node");
            if (index < 0 || std::size_t(index) >= nodes.size()) {
                throw std::runtime_error("Invalid animation node");
            }
            const auto& n = nodes[index];
            auto p =
                modelSample(c.at("positions"),
                            time,
                            {n.at("position")[0], n.at("position")[1], n.at("position")[2], 0});
            auto s = modelSample(
                c.at("scales"), time, {n.at("scale")[0], n.at("scale")[1], n.at("scale")[2], 0});
            auto r = modelSample(c.at("rotations"),
                                 time,
                                 {n.at("rotation")[0],
                                  n.at("rotation")[1],
                                  n.at("rotation")[2],
                                  n.at("rotation")[3]},
                                 true);
            local[index] = transformMatrix(
                {p[0], p[1], p[2]}, normalize(Quat{r[0], r[1], r[2], r[3]}), {s[0], s[1], s[2]});
        }
    }
    if (blend < 1) {
        // Blend local TRS, never matrices, before accumulating hierarchy transforms.
        const auto source =
            frozen && !frozen->empty() ? *frozen : modelLocalTRS(root, sourceAnimation, sourceTime);
        if (source.size() != nodes.size()) {
            throw std::runtime_error("Invalid blend pose");
        }
        const auto target = modelLocalTRS(root, animation, time);
        for (std::size_t i = 0; i < local.size(); ++i) {
            const auto& a = source[i];
            const auto& b = target[i];
            local[i] = transformMatrix(a.position + (b.position - a.position) * blend,
                                       nlerp(a.rotation, b.rotation, blend),
                                       a.scale + (b.scale - a.scale) * blend);
        }
    }
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        int parent = nodes[i].at("parent");
        if (parent < -1 || parent >= int(i)) {
            throw std::runtime_error("Invalid skeleton parent");
        }
        global.push_back(parent < 0 ? local[i] : multiply(global[parent], local[i]));
    }
    return global;
}
// Applies an already evaluated global pose. Using the same evaluated pose as
// GPU skinning preserves crossfades and skeleton residual transforms.
inline void poseModelMeshGlobal(MeshData& mesh,
                                const Json& part,
                                const std::vector<Mat4>& global,
                                bool skinWeights) {
    if (!part.contains("node")) {
        return;
    }
    int node = part.at("node");
    if (node < 0 || std::size_t(node) >= global.size()) {
        throw std::runtime_error("Invalid mesh node");
    }
    // Imported vertices are already in mesh-local bind space. A scene's
    // default joint transforms are not necessarily its skin bind transforms
    // (notably FBX files with a separate bind/rest pose). Skinning here while
    // animation == -1 incorrectly poses the mesh during import, thumbnail
    // generation, bounds/collider loading and Model Preview's Reset state.
    // Place the original geometry using its mesh node, without bone weights.
    if (!skinWeights) {
        for (auto& vertex : mesh.vertices) {
            vertex.position = modelPoint(global[node], vertex.position);
            vertex.normal = normalize(modelNormal(global[node], vertex.normal));
        }
        return;
    }
    const auto& bones = part.at("bones");
    const auto& weights = part.at("weights");
    if (!bones.is_array() || bones.size() > 4096 || !weights.is_array() ||
        weights.size() != mesh.vertices.size()) {
        throw std::runtime_error("Invalid skin data");
    }
    std::vector<Mat4> skin;
    for (const auto& b : bones) {
        int index = b.at("node");
        if (index < 0 || std::size_t(index) >= global.size()) {
            throw std::runtime_error("Invalid bone node");
        }
        skin.push_back(multiply(global[index], modelMatrix(b.at("offset"))));
    }
    for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
        auto& v = mesh.vertices[i];
        Vec3 p{}, n{};
        float total = 0;
        if (!weights[i].is_array() || weights[i].size() > 4096) {
            throw std::runtime_error("Invalid vertex weights");
        }
        for (const auto& w : weights[i]) {
            int b = w.at(0);
            float weight = w.at(1);
            if (b < 0 || std::size_t(b) >= skin.size() || !std::isfinite(weight) || weight < 0) {
                throw std::runtime_error("Invalid skin weight");
            }
            p = p + modelPoint(skin[b], v.position) * weight;
            n = n + modelNormal(skin[b], v.normal) * weight;
            total += weight;
            if (!std::isfinite(total)) {
                throw std::runtime_error("Bone weight sum overflow");
            }
        }
        if (total > 0) {
            v.position = p * (1 / total);
            v.normal = normalize(n);
        } else {
            v.position = modelPoint(global[node], v.position);
            v.normal = normalize(modelNormal(global[node], v.normal));
        }
    }
}
inline void poseModelMesh(
    MeshData& mesh, const Json& part, const Json& root, int animation = -1, float time = 0) {
    if (!root.contains("nodes") || !part.contains("node")) {
        return;
    }
    const auto global = modelGlobalPose(root, animation, time);
    poseModelMeshGlobal(mesh, part, global, animation >= 0);
}
