#pragma once
#include "core/hierarchy.hpp"
#include "models/model_asset.hpp"
#include <tuple>

namespace Waters {
// Bounded shared cache for rendering and physics. Static geometry only.
inline const MeshData&
geometry(World& world, Entity e, const AssetManager& assets, bool refine = true) {
    static std::map<std::string, MeshData> cache;
    static std::filesystem::path root;
    static unsigned revision = ~0u;
    if (root != assets.root || revision != modelAssetRevision) {
        cache.clear();
        root = assets.root;
        revision = modelAssetRevision;
    }
    const auto& w = world.get<Water>(e);
    const auto* renderer = world.tryGet<MeshRenderer>(e);
    const auto path = !w.mesh.empty() ? w.mesh : renderer ? renderer->model : std::string{};
    const unsigned primitive = renderer ? renderer->mesh.id : 1;
    const auto key =
        (path.empty() ? "@" + std::to_string(primitive) : path) + (refine ? "#render" : "#surface");
    auto it = cache.find(key);
    if (it == cache.end()) {
        if (cache.size() >= 32) {
            throw std::runtime_error("Water geometry cache limit reached (32)");
        }
        MeshData mesh;
        if (!path.empty()) {
            mesh = loadModelAsset(assets, path);
        } else {
            switch (primitive) {
            case 1:
                mesh = makePlane(16, 16);
                break;
            case 2:
                mesh = makeBox({1, 1, 1});
                break;
            case 3:
                mesh = makeSphere(.5f);
                break;
            case 4:
                mesh = makeCapsule(.5f, 1);
                break;
            case 5:
                mesh = makeCone(.5f, 1);
                break;
            default:
                throw std::runtime_error("Assign an imported .mesh to Water for custom geometry");
            }
        }
        if (mesh.vertices.size() > 120000) {
            throw std::runtime_error("Water mesh exceeds 120000 vertices");
        }
        // Weld shading normals by position for water only. Imported flat normals
        // should not turn specular highlights into a triangle checkerboard.
        if (refine) {
            std::map<std::tuple<float, float, float>, Vec3> normals;
            auto key = [](Vec3 p) { return std::make_tuple(p.x, p.y, p.z); };
            for (size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
                const auto& a = mesh.vertices[i];
                const auto& b = mesh.vertices[i + 1];
                const auto& c = mesh.vertices[i + 2];
                auto face = cross(b.position - a.position, c.position - a.position);
                if (dot(face, a.normal + b.normal + c.normal) < 0) {
                    face = face * -1.f;
                }
                for (size_t j = 0; j < 3; ++j) {
                    auto& sum = normals[key(mesh.vertices[i + j].position)];
                    sum = sum + face;
                }
            }
            for (auto& vertex : mesh.vertices) {
                const auto sum = normals[key(vertex.position)];
                if (dot(sum, sum) > 1e-12f) {
                    vertex.normal = normalize(sum);
                }
            }
        }
        // Refine large triangles so coarse planes can show displaced waves.
        for (int pass = 0; refine && pass < 4 && mesh.vertices.size() * 4 <= 120000; ++pass) {
            MeshData refined;
            bool split = false;
            for (size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
                auto a = mesh.vertices[i], b = mesh.vertices[i + 1], c = mesh.vertices[i + 2];
                auto midpoint = [](const Vertex& x, const Vertex& y) {
                    return Vertex{(x.position + y.position) * .5f,
                                  normalize(x.normal + y.normal),
                                  (x.u + y.u) * .5f,
                                  (x.v + y.v) * .5f};
                };
                if (std::max({dot(a.position - b.position, a.position - b.position),
                              dot(a.position - c.position, a.position - c.position),
                              dot(c.position - b.position, c.position - b.position)}) > 1.f) {
                    auto ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
                    refined.vertices.insert(refined.vertices.end(),
                                            {a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca});
                    split = true;
                } else {
                    refined.vertices.insert(refined.vertices.end(), {a, b, c});
                }
            }
            if (!split) {
                break;
            }
            mesh = std::move(refined);
        }
        it = cache.emplace(key, std::move(mesh)).first;
    }
    return it->second;
}
// Intersect an upward triangle surface with a vertical line. Holes stay holes.
inline bool surface(Vec3 a, Vec3 b, Vec3 c, Vec3 p, Vec3& hit, Vec3& normal) {
    normal = normalize(cross(b - a, c - a));
    if (normal.y < .25f) {
        return false; // Back/down-facing and near-vertical faces are not a water top.
    }
    hit = p + Vec3{0, dot(a - p, normal) / normal.y, 0};
    auto u = b - a, v = c - a, q = hit - a;
    float aa = dot(u, u), bb = dot(v, v), ab = dot(u, v), det = aa * bb - ab * ab;
    if (det < 1e-10f) {
        return false;
    }
    float x = (dot(q, u) * bb - dot(q, v) * ab) / det, y = (dot(q, v) * aa - dot(q, u) * ab) / det;
    return x >= 0 && y >= 0 && x + y <= 1;
}
} // namespace Waters
