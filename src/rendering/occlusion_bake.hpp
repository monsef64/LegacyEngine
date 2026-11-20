#pragma once
#include "core/hierarchy.hpp"
#include "models/model_asset.hpp"
#include "terrain/terrain_mesh.hpp"
#include <array>
#include <cstring>
#include <functional>
#include <map>
#include <sstream>

// Conservative baked triangle occlusion, independent of view direction/camera cells.
// A single opaque triangle must cover the entire candidate bounds. No center-ray
// guesses, depth-buffer readbacks, or previous-frame visibility are used.
namespace Occlusion {
inline bool eligible(World& world, Entity e, const Transform& t, const MeshRenderer& r) {
    if (!t.isStatic || !isActive(world, e) || r.materialOverrides || world.has<Water>(e) ||
        world.has<Camera>(e)) {
        return false;
    }
    for (unsigned depth = 0; world.alive(e); ++depth, e = getParent(world, e)) {
        if (depth >= 128 || world.has<Animation>(e) || world.has<RigidBody>(e) ||
            world.has<CharacterController>(e) || world.has<BoneAttachment>(e)) {
            return false;
        }
    }
    return std::abs(t.scale.x * t.scale.y * t.scale.z) > 1e-10f;
}
inline std::string
state(World& world, unsigned revision, const std::function<bool(const MeshRenderer&)>& opaque) {
    // Exact value signature, not struct padding or mutable GPU model handles.
    std::ostringstream out;
    out << std::hexfloat << revision << ':';
    world.eachReadOnly<Transform, MeshRenderer>(
        [&](Entity e, const Transform& t, const MeshRenderer& r) {
            if (!t.isStatic) {
                return;
            }
            out << e.index << ':' << e.generation << ':' << (eligible(world, e, t, r) && opaque(r))
                << ':' << t.position.x << ',' << t.position.y << ',' << t.position.z << ','
                << t.rotation.x << ',' << t.rotation.y << ',' << t.rotation.z << ',' << t.rotation.w
                << ',' << t.scale.x << ',' << t.scale.y << ',' << t.scale.z << ':' << r.model.size()
                << ':' << r.model << ':' << (r.model.empty() ? r.mesh.id : 0) << ':'
                << r.material.size() << ':' << r.material << ';';
        });
    world.eachReadOnly<Transform, Terrain>(
        [&](Entity e, const Transform& t, const Terrain& terrain) {
            if (!t.isStatic) {
                return;
            }
            out << "terrain:" << e.index << ':' << e.generation << ':'
                << eligible(world, e, t, MeshRenderer{}) << ':' << terrain.asset << ':'
                << t.position.x << ',' << t.position.y << ',' << t.position.z << ',' << t.rotation.x
                << ',' << t.rotation.y << ',' << t.rotation.z << ',' << t.rotation.w << ','
                << t.scale.x << ',' << t.scale.y << ',' << t.scale.z << ':';
            if (terrain.data) {
                const auto& d = *terrain.data;
                struct Hash {
                    unsigned revision = ~0u;
                    std::uint64_t value = 0;
                };
                static std::map<unsigned, Hash> hashes;
                if (hashes.size() > 1024) {
                    hashes.clear();
                }
                auto& h = hashes[d.identity];
                if (h.revision != d.shapeRevision) {
                    h.value = 14695981039346656037ull;
                    for (float v : d.heights) {
                        unsigned bits;
                        std::memcpy(&bits, &v, sizeof(bits));
                        h.value = (h.value ^ bits) * 1099511628211ull;
                    }
                    h.revision = d.shapeRevision;
                }
                out << d.resolution << ':' << d.size << ':' << h.value;
            }
            out << ';';
        });
    return out.str();
}
inline Vec3 point(const Transform& t, Vec3 p) {
    return t.position +
           rotateVector(t.rotation, {p.x * t.scale.x, p.y * t.scale.y, p.z * t.scale.z});
}
struct Triangle {
    Vec3 a, b, c;
    Entity owner;
    float area = 0;
};
class Bake {
  public:
    bool enabled = false;
    std::string signature, status;
    std::vector<Triangle> triangles;
    std::size_t culled = 0, tested = 0;
    bool valid = false;
    std::vector<const Triangle*> viewTriangles;
    void clear() {
        enabled = valid = false;
        signature.clear();
        triangles.clear();
    }
    void begin(World& world,
               unsigned revision,
               const Mat4& vp,
               const std::function<bool(const MeshRenderer&)>& opaque) {
        culled = tested = 0;
        viewTriangles.clear();
        // Point-camera shadow cones apply only to perspective views.
        const float perspective = vp.m[3] * vp.m[3] + vp.m[7] * vp.m[7] + vp.m[11] * vp.m[11];
        valid = enabled && perspective > .5f && !triangles.empty() &&
                signature == state(world, revision, opaque);
        if (!valid) {
            return;
        }
        for (const auto& tri : triangles) {
            bool unclipped = true;
            for (const auto p : {tri.a, tri.b, tri.c}) {
                const float z = vp.m[2] * p.x + vp.m[6] * p.y + vp.m[10] * p.z + vp.m[14];
                const float w = vp.m[3] * p.x + vp.m[7] * p.y + vp.m[11] * p.z + vp.m[15];
                unclipped &= w > .001f && z > -w + .001f && z < w - .001f;
            }
            if (unclipped) {
                viewTriangles.push_back(&tri);
            }
        }
    }
    void generate(World& world,
                  unsigned revision,
                  const std::function<MeshData(const MeshRenderer&)>& geometry,
                  const std::function<bool(const MeshRenderer&)>& opaque) {
        std::vector<Triangle> result;
        std::size_t scanned = 0;
        auto append = [&](Entity e, const Transform& t, const MeshData& mesh) {
            if (mesh.topology != MeshData::Topology::Triangles) {
                return;
            }
            for (std::size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
                if (++scanned > 2000000) {
                    throw std::runtime_error("Occlusion bake limit: two million source triangles");
                }
                Triangle tri{point(t, mesh.vertices[i].position),
                             point(t, mesh.vertices[i + 1].position),
                             point(t, mesh.vertices[i + 2].position),
                             e};
                // Renderer flips GL front-face for mirrored transforms.
                if (t.scale.x * t.scale.y * t.scale.z < 0) {
                    std::swap(tri.b, tri.c);
                }
                const auto n = cross(tri.b - tri.a, tri.c - tri.a);
                tri.area = dot(n, n);
                if (std::isfinite(tri.area) && tri.area > 1e-10f) {
                    result.push_back(tri);
                }
                if (result.size() >= 4096) {
                    std::partial_sort(result.begin(),
                                      result.begin() + 2048,
                                      result.end(),
                                      [](const auto& a, const auto& b) { return a.area > b.area; });
                    result.resize(2048);
                }
            }
        };
        world.eachReadOnly<Transform, MeshRenderer>(
            [&](Entity e, const Transform& t, const MeshRenderer& r) {
                if (eligible(world, e, t, r) && opaque(r)) {
                    append(e, t, geometry(r));
                }
            });
        world.eachReadOnly<Transform, Terrain>(
            [&](Entity e, const Transform& t, const Terrain& terrain) {
                if (terrain.data && eligible(world, e, t, MeshRenderer{})) {
                    const auto& d = *terrain.data;
                    for (int z = 0; z < Terrains::chunkCount(d); ++z) {
                        for (int x = 0; x < Terrains::chunkCount(d); ++x) {
                            MeshData mesh;
                            for (const auto& v : Terrains::chunk(d, x, z, 1)) {
                                mesh.vertices.push_back({v.position, v.normal});
                            }
                            append(e, t, mesh);
                        }
                    }
                }
            });
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
            return a.area > b.area;
        });
        if (result.size() > 2048) {
            result.resize(2048);
        }
        signature = state(world, revision, opaque);
        triangles = std::move(result);
        enabled = true;
        status = "Baked " + std::to_string(triangles.size()) +
                 " opaque triangles. Largest 2048 retained.";
    }
    bool hidden(Entity e, const Transform& t, Vec3 low, Vec3 high, Vec3 camera) {
        if (!valid) {
            return false;
        }
        ++tested;
        // Transform all eight corners; enclose them in a world AABB.
        Vec3 a = point(t, low), b = a;
        for (int i = 0; i < 8; ++i) {
            const auto p =
                point(t, {i & 1 ? high.x : low.x, i & 2 ? high.y : low.y, i & 4 ? high.z : low.z});
            a = {std::min(a.x, p.x), std::min(a.y, p.y), std::min(a.z, p.z)};
            b = {std::max(b.x, p.x), std::max(b.y, p.y), std::max(b.z, p.z)};
        }
        // Expand, never shrink candidate bounds (including very flat meshes).
        a = a - Vec3{.01f, .01f, .01f};
        b = b + Vec3{.01f, .01f, .01f};
        for (const auto* triangle : viewTriangles) {
            const auto& tri = *triangle;
            if (tri.owner == e) {
                continue;
            }
            const auto ab = tri.b - tri.a, ac = tri.c - tri.a, n = cross(ab, ac);
            if (dot(n, camera - tri.a) <= .0001f) {
                continue; // backface / camera on plane
            }
            bool covered = true;
            for (int i = 0; i < 8 && covered; ++i) {
                const Vec3 end{i & 1 ? b.x : a.x, i & 2 ? b.y : a.y, i & 4 ? b.z : a.z};
                const auto dir = end - camera, h = cross(dir, ac);
                const float determinant = dot(ab, h);
                if (std::abs(determinant) < 1e-8f) {
                    covered = false;
                    break;
                }
                const auto s = camera - tri.a;
                const float u = dot(s, h) / determinant;
                const auto q = cross(s, ab);
                const float v = dot(dir, q) / determinant, distance = dot(ac, q) / determinant;
                // Strict interior and depth margin avoids edge/floating-point leaks.
                covered = u > .001f && v > .001f && u + v < .999f && distance > .0001f &&
                          distance < .999f;
            }
            if (covered) {
                ++culled;
                return true;
            }
        }
        return false;
    }
};
} // namespace Occlusion
