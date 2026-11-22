#pragma once
#include "core/hierarchy.hpp"
#include "models/model_asset.hpp"
#include "terrain/terrain_tools.hpp"
#include <map>

namespace ScenePicking {
inline bool triangle(const Ray& ray, Vec3 a, Vec3 b, Vec3 c, float& nearest) {
    const auto ab = b - a, ac = c - a, p = cross(ray.direction, ac);
    const float det = dot(ab, p);
    if (std::abs(det) < 1e-9f) {
        return false;
    }
    const auto delta = ray.origin - a;
    const float u = dot(delta, p) / det;
    if (u < 0 || u > 1) {
        return false;
    }
    const auto q = cross(delta, ab);
    const float v = dot(ray.direction, q) / det;
    if (v < 0 || u + v > 1) {
        return false;
    }
    const float distance = dot(ac, q) / det;
    if (distance < 0 || distance >= nearest) {
        return false;
    }
    nearest = distance;
    return true;
}
#if defined(__SSE2__)
// Four independent Moller-Trumbore intersections per SIMD batch. Keep the scalar
// tail/fallback and identical two-sided hit/epsilon rules.
inline bool triangles4(const Ray& ray, const Vertex* vertices, float& nearest) {
    struct V {
        __m128 x, y, z;
    };
    auto gather = [&](int corner) {
        const auto a = vertices[corner].position, b = vertices[corner + 3].position,
                   c = vertices[corner + 6].position, d = vertices[corner + 9].position;
        return V{_mm_set_ps(d.x, c.x, b.x, a.x),
                 _mm_set_ps(d.y, c.y, b.y, a.y),
                 _mm_set_ps(d.z, c.z, b.z, a.z)};
    };
    auto sub = [](V a, V b) {
        return V{_mm_sub_ps(a.x, b.x), _mm_sub_ps(a.y, b.y), _mm_sub_ps(a.z, b.z)};
    };
    auto cross4 = [](V a, V b) {
        return V{_mm_sub_ps(_mm_mul_ps(a.y, b.z), _mm_mul_ps(a.z, b.y)),
                 _mm_sub_ps(_mm_mul_ps(a.z, b.x), _mm_mul_ps(a.x, b.z)),
                 _mm_sub_ps(_mm_mul_ps(a.x, b.y), _mm_mul_ps(a.y, b.x))};
    };
    auto dot4 = [](V a, V b) {
        return _mm_add_ps(_mm_add_ps(_mm_mul_ps(a.x, b.x), _mm_mul_ps(a.y, b.y)),
                          _mm_mul_ps(a.z, b.z));
    };
    auto splat = [](Vec3 v) { return V{_mm_set1_ps(v.x), _mm_set1_ps(v.y), _mm_set1_ps(v.z)}; };
    const auto a = gather(0), ab = sub(gather(1), a), ac = sub(gather(2), a);
    const auto direction = splat(ray.direction), p = cross4(direction, ac);
    const auto det = dot4(ab, p);
    const auto delta = sub(splat(ray.origin), a), q = cross4(delta, ab);
    const auto u = _mm_div_ps(dot4(delta, p), det), v = _mm_div_ps(dot4(direction, q), det),
               distance = _mm_div_ps(dot4(ac, q), det);
    const auto zero = _mm_setzero_ps(), one = _mm_set1_ps(1.f);
    auto mask = _mm_cmpge_ps(_mm_andnot_ps(_mm_set1_ps(-0.0f), det), _mm_set1_ps(1e-9f));
    mask = _mm_and_ps(mask, _mm_and_ps(_mm_cmpge_ps(u, zero), _mm_cmple_ps(u, one)));
    mask = _mm_and_ps(mask, _mm_and_ps(_mm_cmpge_ps(v, zero), _mm_cmple_ps(_mm_add_ps(u, v), one)));
    mask = _mm_and_ps(
        mask,
        _mm_and_ps(_mm_cmpge_ps(distance, zero), _mm_cmplt_ps(distance, _mm_set1_ps(nearest))));
    const auto hits = _mm_movemask_ps(mask);
    if (!hits) {
        return false;
    }
    float values[4];
    _mm_storeu_ps(values, distance);
    for (int i = 0; i < 4; ++i) {
        if (hits & (1 << i)) {
            nearest = std::min(nearest, values[i]);
        }
    }
    return true;
}
#endif
inline Entity pick(
    World& world, const AssetManager& assets, const Ray& ray, float& nearest, Entity initial = {}) {
    static std::filesystem::path root;
    static unsigned revision = ~0u;
    struct CachedMesh {
        MeshData mesh;
        Vec3 low, high;
    };
    static std::map<std::string, CachedMesh> cache;
    static std::size_t cachedVertices = 0;
    if (root != assets.root || revision != modelAssetRevision) {
        cache.clear();
        cachedVertices = 0;
        root = assets.root;
        revision = modelAssetRevision;
    }
    Entity result = initial;
    world.eachReadOnly<Transform>([&](Entity e, const Transform& t) {
        if (!isActive(world, e) || std::abs(t.scale.x * t.scale.y * t.scale.z) < 1e-9f) {
            return;
        }
        const Quat inv{-t.rotation.x, -t.rotation.y, -t.rotation.z, t.rotation.w};
        auto divide = [&](Vec3 p) {
            return Vec3{p.x / t.scale.x, p.y / t.scale.y, p.z / t.scale.z};
        };
        const Ray local{divide(rotateVector(inv, ray.origin - t.position)),
                        divide(rotateVector(inv, ray.direction))};
        if (const auto* terrain = world.tryGet<Terrain>(e); terrain && terrain->data) {
            Vec3 hit;
            if (Terrains::intersect(*terrain->data, local.origin, local.direction, hit)) {
                const Vec3 point = t.position + rotateVector(t.rotation, hit * t.scale);
                const float distance = dot(point - ray.origin, ray.direction);
                if (distance >= 0 && distance < nearest) {
                    nearest = distance;
                    result = e;
                }
            }
        }
        const auto* mesh = world.tryGet<MeshRenderer>(e);
        if (!mesh) {
            return;
        }
        const std::string key =
            mesh->model.empty() ? "@" + std::to_string(mesh->mesh.id) : mesh->model;
        try {
            auto it = cache.find(key);
            if (it == cache.end()) {
                MeshData data;
                if (!mesh->model.empty()) {
                    data = loadModelAsset(assets, mesh->model);
                } else {
                    switch (mesh->mesh.id) {
                    case 1:
                        data = makePlane(16, 16);
                        break;
                    case 2:
                        data = makeBox({1, 1, 1});
                        break;
                    case 3:
                        data = makeSphere(.5f);
                        break;
                    case 4:
                        data = makeCapsule(.5f, 1);
                        break;
                    case 5:
                        data = makeCone(.5f, 1);
                        break;
                    default:
                        return;
                    }
                }
                if (data.vertices.empty()) {
                    return;
                }
                CachedMesh cached;
                cached.low = cached.high = data.vertices.front().position;
                for (const auto& vertex : data.vertices) {
                    const auto p = vertex.position;
                    cached.low = {std::min(cached.low.x, p.x),
                                  std::min(cached.low.y, p.y),
                                  std::min(cached.low.z, p.z)};
                    cached.high = {std::max(cached.high.x, p.x),
                                   std::max(cached.high.y, p.y),
                                   std::max(cached.high.z, p.z)};
                }
                // Bound retained CPU geometry (one oversized model may stand alone).
                if (cache.size() >= 32 || cachedVertices + data.vertices.size() > 1000000) {
                    cache.clear();
                    cachedVertices = 0;
                }
                cachedVertices += data.vertices.size();
                cached.mesh = std::move(data);
                it = cache.emplace(key, std::move(cached)).first;
            }
            const auto& cached = it->second;
            const float origins[]{local.origin.x, local.origin.y, local.origin.z},
                directions[]{local.direction.x, local.direction.y, local.direction.z};
            const float low[]{cached.low.x, cached.low.y, cached.low.z},
                high[]{cached.high.x, cached.high.y, cached.high.z};
            float enter = 0, exit = nearest;
            for (int axis = 0; axis < 3; ++axis) {
                if (std::abs(directions[axis]) < 1e-9f) {
                    if (origins[axis] < low[axis] || origins[axis] > high[axis]) {
                        return;
                    }
                } else {
                    float a = (low[axis] - origins[axis]) / directions[axis],
                          b = (high[axis] - origins[axis]) / directions[axis];
                    if (a > b) {
                        std::swap(a, b);
                    }
                    enter = std::max(enter, a);
                    exit = std::min(exit, b);
                    if (enter > exit) {
                        return;
                    }
                }
            }
            const auto& vertices = cached.mesh.vertices;
            size_t i = 0;
#if defined(__SSE2__)
            for (; i + 11 < vertices.size(); i += 12) {
                if (triangles4(local, vertices.data() + i, nearest))
                    result = e;
            }
#endif
            for (; i + 2 < vertices.size(); i += 3) {
                if (triangle(local,
                             vertices[i].position,
                             vertices[i + 1].position,
                             vertices[i + 2].position,
                             nearest)) {
                    result = e;
                }
            }
        } catch (const std::exception&) { /* Invalid assets remain selectable in Hierarchy. */
        }
    });
    return result;
}
inline Entity modelRoot(World& world, Entity e) {
    // Select an empty model/assembly root through its visible mesh children.
    Entity result = e;
    for (unsigned depth = 0; depth < 128; ++depth) {
        auto p = getParent(world, result);
        if (p == Entity{} || world.has<MeshRenderer>(p) || world.has<Terrain>(p)) {
            break;
        }
        result = p;
    }
    return result;
}
} // namespace ScenePicking
