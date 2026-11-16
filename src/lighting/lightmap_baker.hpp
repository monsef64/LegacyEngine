#pragma once
#include "assets/assets.hpp"
#include "core/hierarchy.hpp"
#include "lighting/cookie_image.hpp"
#include "lighting/lightmap_state.hpp"
#include "models/model_asset.hpp"
#include "project/project_io.hpp"
#include "scene/editor_grid.hpp"
#include <atomic>
#include <limits>
#include <numeric>

// Offline direct lighting and diffuse path-traced global illumination. Workers own all geometry; no
// GL/ECS access.
namespace Lightmapping {
struct Settings {
    int tile = 16; // one isolated chart per triangle, including a two-texel gutter
    float bias = .002f;
    int bounces = 2, samples = 32;
    float maxDistance = 50;
    bool denoise = true;
};
struct Object {
    Entity entity;
    Transform transform;
    MeshRenderer renderer;
};
struct Light {
    LightSettings settings;
    Vec3 position, direction;
    int type;
    Vec3 right{1, 0, 0}, up{0, 1, 0};
    std::shared_ptr<Lighting::CookieImage> cookie;
};
struct Progress {
    std::atomic<bool> cancel{false};
    std::atomic<std::size_t> done{0}, total{0};
    std::atomic<int> phase{0};
};
struct Assignment {
    Entity entity;
    std::string path;
    int grid;
};
struct Result {
    std::filesystem::path directory;
    std::vector<Assignment> assignments;
    bool keep = false;
    ~Result() {
        if (!keep && !directory.empty()) {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
        }
    }
};
struct Triangle {
    Vec3 p[3], n[3];
    Vec3 albedo{.8f, .8f, .8f};
};
struct Bounds {
    Vec3 low{1e30f, 1e30f, 1e30f}, high{-1e30f, -1e30f, -1e30f};
    void include(Vec3 p) {
        low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
    }
    bool hit(Vec3 p, Vec3 d, float distance) const {
        const float origin[]{p.x, p.y, p.z}, dir[]{d.x, d.y, d.z};
        const float lo[]{low.x, low.y, low.z}, hi[]{high.x, high.y, high.z};
        float near = 0, far = distance;
        for (int a = 0; a < 3; ++a) {
            if (std::abs(dir[a]) < 1e-12f) {
                if (origin[a] < lo[a] || origin[a] > hi[a]) {
                    return false;
                }
            } else {
                float l = (lo[a] - origin[a]) / dir[a], h = (hi[a] - origin[a]) / dir[a];
                if (l > h) {
                    std::swap(l, h);
                }
                near = std::max(near, l);
                far = std::min(far, h);
                if (near > far) {
                    return false;
                }
            }
        }
        return true;
    }
};
class Bvh {
    struct Node {
        Bounds bounds;
        std::size_t first = 0, count = 0;
        int left = -1, right = -1;
    };
    const std::vector<Triangle>& triangles_;
    std::vector<std::size_t> indices_;
    std::vector<Node> nodes_;
    int build(std::size_t first, std::size_t count) {
        int index = int(nodes_.size());
        nodes_.push_back({});
        Bounds b;
        for (std::size_t i = first; i < first + count; ++i) {
            for (auto p : triangles_[indices_[i]].p) {
                b.include(p);
            }
        }
        nodes_[index].bounds = b;
        nodes_[index].first = first;
        nodes_[index].count = count;
        if (count <= 8) {
            return index;
        }
        auto extent = b.high - b.low;
        int axis =
            extent.x > extent.y ? (extent.x > extent.z ? 0 : 2) : (extent.y > extent.z ? 1 : 2);
        auto center = [&](std::size_t i) {
            auto p = triangles_[i].p[0] + triangles_[i].p[1] + triangles_[i].p[2];
            return axis == 0 ? p.x : axis == 1 ? p.y : p.z;
        };
        auto middle = first + count / 2;
        std::nth_element(indices_.begin() + first,
                         indices_.begin() + middle,
                         indices_.begin() + first + count,
                         [&](auto a, auto c) { return center(a) < center(c); });
        int left = build(first, count / 2), right = build(middle, count - count / 2);
        nodes_[index].left = left;
        nodes_[index].right = right;
        return index;
    }
    bool blocked(int node, Vec3 p, Vec3 d, float distance, std::size_t ignore) const {
        const auto& n = nodes_[node];
        if (!n.bounds.hit(p, d, distance)) {
            return false;
        }
        if (n.left >= 0) {
            return blocked(n.left, p, d, distance, ignore) ||
                   blocked(n.right, p, d, distance, ignore);
        }
        for (std::size_t j = n.first; j < n.first + n.count; ++j) {
            auto i = indices_[j];
            if (i == ignore) {
                continue;
            }
            const auto& t = triangles_[i];
            auto e1 = t.p[1] - t.p[0], e2 = t.p[2] - t.p[0], h = cross(d, e2);
            float det = dot(e1, h);
            if (std::abs(det) < 1e-9f) {
                continue;
            }
            auto s = p - t.p[0];
            float u = dot(s, h) / det;
            if (u < 0 || u > 1) {
                continue;
            }
            auto q = cross(s, e1);
            float v = dot(d, q) / det;
            if (v < 0 || u + v > 1) {
                continue;
            }
            float hit = dot(e2, q) / det;
            if (hit > 1e-5f && hit < distance) {
                return true;
            }
        }
        return false;
    }

    void nearest(int node,
                 Vec3 p,
                 Vec3 d,
                 float& distance,
                 std::size_t ignore,
                 std::size_t& face,
                 float& hitU,
                 float& hitV) const {
        const auto& n = nodes_[node];
        if (!n.bounds.hit(p, d, distance)) {
            return;
        }
        if (n.left >= 0) {
            nearest(n.left, p, d, distance, ignore, face, hitU, hitV);
            nearest(n.right, p, d, distance, ignore, face, hitU, hitV);
            return;
        }
        for (std::size_t j = n.first; j < n.first + n.count; ++j) {
            const auto i = indices_[j];
            if (i == ignore) {
                continue;
            }
            const auto& t = triangles_[i];
            const auto e1 = t.p[1] - t.p[0], e2 = t.p[2] - t.p[0], h = cross(d, e2);
            const float det = dot(e1, h);
            if (std::abs(det) < 1e-9f) {
                continue;
            }
            const auto delta = p - t.p[0];
            const float u = dot(delta, h) / det;
            if (u < 0 || u > 1) {
                continue;
            }
            const auto q = cross(delta, e1);
            const float v = dot(d, q) / det;
            if (v < 0 || u + v > 1) {
                continue;
            }
            const float hit = dot(e2, q) / det;
            if (hit > 1e-5f && hit < distance) {
                distance = hit;
                face = i;
                hitU = u;
                hitV = v;
            }
        }
    }

  public:
    explicit Bvh(const std::vector<Triangle>& t) : triangles_(t), indices_(t.size()) {
        std::iota(indices_.begin(), indices_.end(), 0);
        if (!t.empty()) {
            build(0, t.size());
        }
    }
    bool trace(Vec3 p,
               Vec3 d,
               float distance,
               std::size_t ignore,
               std::size_t& face,
               float& u,
               float& v) const {
        face = std::numeric_limits<std::size_t>::max();
        if (!nodes_.empty()) {
            nearest(0, p, d, distance, ignore, face, u, v);
        }
        return face != std::numeric_limits<std::size_t>::max();
    }
    bool blocked(Vec3 p, Vec3 d, float distance, std::size_t ignore) const {
        return !nodes_.empty() && blocked(0, p, d, distance, ignore);
    }
};
inline void check(const Progress& p) {
    if (p.cancel) {
        throw std::runtime_error("Bake cancelled");
    }
}
inline Vec3 directLighting(Vec3 position,
                           Vec3 n,
                           std::size_t face,
                           const std::vector<Light>& lights,
                           const Bvh& bvh,
                           float bias) {
    Vec3 irradiance{};
    for (const auto& light : lights) {
        auto direction = light.direction * -1.f;
        Vec3 cookie{1, 1, 1};
        if (light.type == 2 && light.cookie) {
            const Vec3 delta = position - light.position;
            const float depth = dot(delta, light.direction);
            if (depth <= .0001f) {
                continue;
            }
            const float factor = .5f / (depth * std::tan(radians(light.settings.outerAngle)));
            cookie = light.cookie->sample(dot(delta, light.right) * factor + .5f,
                                          dot(delta, light.up) * factor + .5f);
        }
        float attenuation = 1, distance = 1e30f;
        if (light.type) {
            auto delta = light.position - position;
            distance = std::sqrt(dot(delta, delta));
            if (distance >= light.settings.range) {
                continue;
            }
            direction = delta * (1 / std::max(distance, .0001f));
            float falloff = std::max(
                1 - distance * distance / (light.settings.range * light.settings.range), 0.f);
            attenuation = falloff * falloff / (1 + .09f * distance + .032f * distance * distance);
            if (light.type == 2) {
                const float outer = std::cos(radians(light.settings.outerAngle)),
                            inner = std::cos(radians(light.settings.innerAngle));
                float cone = std::clamp(
                    (dot(direction * -1.f, light.direction) - outer) / (inner - outer), 0.f, 1.f);
                attenuation *= cone * cone * (3 - 2 * cone);
            }
        }
        float diffuse = std::max(dot(n, direction), 0.f) * attenuation;
        if (diffuse <= 0) {
            continue;
        }
        if (light.settings.shadows &&
            bvh.blocked(position + n * bias, direction, distance - bias, face)) {
            continue;
        }
        irradiance =
            irradiance + light.settings.color * cookie * (light.settings.intensity * diffuse);
    }
    return irradiance;
}
// Independent deterministic streams per texel; no global RNG or shared worker state.
struct Random {
    std::uint32_t state;
    float next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return float(state >> 8) * (1.f / 16777216.f);
    }
};
inline Vec3 hemisphere(Vec3 n, Random& random) {
    const float radius = std::sqrt(random.next()), angle = 6.28318530718f * random.next();
    const Vec3 tangent = normalize(cross(std::abs(n.y) < .9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}, n));
    const auto bitangent = cross(n, tangent);
    return normalize(tangent * (radius * std::cos(angle)) + bitangent * (radius * std::sin(angle)) +
                     n * std::sqrt(std::max(0.f, 1 - radius * radius)));
}
inline Vec3 indirectLighting(Vec3 position,
                             Vec3 n,
                             std::size_t face,
                             const std::vector<Triangle>& triangles,
                             const std::vector<Light>& lights,
                             const Bvh& bvh,
                             const Settings& settings,
                             const Progress& progress,
                             std::uint32_t seed) {
    if (settings.bounces == 0) {
        return {};
    }
    Random random{seed ? seed : 1u};
    Vec3 sum{};
    for (int sample = 0; sample < settings.samples; ++sample) {
        if ((sample & 7) == 0) {
            check(progress);
        }
        Vec3 p = position, normal = n, throughput{1, 1, 1};
        auto ignore = face;
        for (int bounce = 0; bounce < settings.bounces; ++bounce) {
            const auto origin = p + normal * settings.bias;
            const auto direction = hemisphere(normal, random);
            std::size_t hit;
            float u = 0, v = 0;
            if (!bvh.trace(origin, direction, settings.maxDistance, ignore, hit, u, v)) {
                break;
            }
            const auto& tri = triangles[hit];
            p = tri.p[0] * (1 - u - v) + tri.p[1] * u + tri.p[2] * v;
            normal = normalize(tri.n[0] * (1 - u - v) + tri.n[1] * u + tri.n[2] * v);
            // Opaque back faces block transport; they do not emit bounce light.
            if (dot(normal, direction) >= 0) {
                break;
            }
            throughput = throughput * tri.albedo;
            sum = sum + throughput * directLighting(p, normal, hit, lights, bvh, settings.bias);
            ignore = hit;
        }
    }
    // Cosine-weighted sampling cancels Lambert's cosine/pi factor.
    return sum * (1.f / settings.samples);
}
inline void
writeAtlas(const std::filesystem::path& file, int size, const std::vector<unsigned char>& image) {
    std::ofstream out(file, std::ios::binary);
    unsigned char header[18]{};
    header[2] = 2;
    header[12] = size & 255;
    header[13] = size >> 8;
    header[14] = size & 255;
    header[15] = size >> 8;
    header[16] = 24;
    out.write(reinterpret_cast<const char*>(header), 18);
    out.write(reinterpret_cast<const char*>(image.data()), std::streamsize(image.size()));
    out.close();
    if (!out) {
        throw std::runtime_error("Cannot write lightmap atlas");
    }
}
inline std::string indirectAtlas(const std::string& direct) {
    auto path = std::filesystem::path(direct);
    return (path.parent_path() / (path.stem().string() + ".indirect.tga")).generic_string();
}
inline MeshData
geometry(const Object& o, const AssetManager& assets, const ProjectIO::Meshes& meshes) {
    if (!o.renderer.model.empty()) {
        return loadModelAsset(assets, o.renderer.model);
    }
    auto id = o.renderer.mesh.id;
    if (id == meshes[1].id) {
        return makePlane(16.f, 16.f);
    }
    if (id == meshes[2].id) {
        return makeBox({1.f, 1.f, 1.f});
    }
    if (id == meshes[3].id) {
        return makeSphere(.5f);
    }
    if (id == meshes[4].id) {
        return makeCapsule(.5f, 1.f);
    }
    if (id == meshes[5].id) {
        return makeCone(.5f, 1.f);
    }
    throw std::runtime_error("Unsupported custom mesh; import as a model before baking");
}
inline std::shared_ptr<Result> bake(const std::vector<Object>& objects,
                                    const std::vector<Light>& sourceLights,
                                    const AssetManager& assets,
                                    const ProjectIO::Meshes& meshes,
                                    Settings settings,
                                    const std::shared_ptr<Progress>& progress,
                                    const std::string& staticState) {
    auto lights = sourceLights;
    if (objects.empty() || lights.empty() || lights.size() > 128 ||
        (settings.tile != 8 && settings.tile != 16 && settings.tile != 32) ||
        settings.bounces < 0 || settings.bounces > 4 || settings.samples < 4 ||
        settings.samples > 256 || !std::isfinite(settings.maxDistance) ||
        settings.maxDistance < 1 || settings.maxDistance > 1000 || !std::isfinite(settings.bias) ||
        settings.bias < .0001f || settings.bias > .1f) {
        throw std::runtime_error("Bake needs objects, 1-128 static lights and valid settings");
    }
    std::map<std::string, std::shared_ptr<Lighting::CookieImage>> cookieImages;
    for (auto& light : lights) {
        check(*progress);
        if (light.type == 2 && !light.settings.cookie.empty()) {
            auto& image = cookieImages[light.settings.cookie];
            if (!image) {
                image = Lighting::CookieImage::load(assets, light.settings.cookie);
            }
            light.cookie = image;
        }
    }
    std::vector<Triangle> triangles;
    struct Range {
        std::size_t first, count;
        int grid, tile;
        Entity entity;
    };
    std::vector<Range> ranges;
    std::size_t pixels = 0, workTexels = 0;
    for (const auto& o : objects) {
        check(*progress);
        auto data = geometry(o, assets, meshes);
        const auto count = data.vertices.size() / 3;
        if (!count || data.topology != MeshData::Topology::Triangles) {
            throw std::runtime_error("Mesh has no triangles");
        }
        const int grid = int(std::ceil(std::sqrt(double(count))));
        // A two-triangle floor needs world-space density, not 16 pixels for 16 metres.
        float longest = 0;
        for (std::size_t i = 0; i < count; ++i) {
            for (int c = 0; c < 3; ++c) {
                const auto d = (data.vertices[i * 3 + c].position -
                                data.vertices[i * 3 + (c + 1) % 3].position) *
                               o.transform.scale;
                longest = std::max(longest, std::sqrt(dot(d, d)));
            }
        }
        const int maxTile = 2048 / grid;
        if (maxTile < 2) {
            throw std::runtime_error("Model exceeds bake triangle budget");
        }
        // Degrade chart density gracefully instead of rejecting high-poly models.
        const int tile =
            std::clamp(int(std::min(2048.f, std::ceil(longest * settings.tile))) + 5, 2, maxTile);
        pixels += std::size_t(grid * tile) * std::size_t(grid * tile);
        if (pixels > 32 * 1024 * 1024 || triangles.size() + count > 500000) {
            throw std::runtime_error(
                "Bake exceeds 32M texel / 500k triangle budget; split the level or lower quality");
        }
        workTexels += count * std::size_t(tile) * tile;
        if (double(workTexels) * settings.samples * settings.bounces > 500000000.0) {
            throw std::runtime_error(
                "GI exceeds 500M path-segment budget; lower density/samples/bounces or split the level");
        }
        ranges.push_back({triangles.size(), count, grid, tile, o.entity});
        const auto& t = o.transform;
        MaterialProperties material;
        material.color = o.renderer.color;
        if (!o.renderer.material.empty()) {
            material = loadMaterial(assets, o.renderer.material);
        }
        material = applyMaterialOverrides(
            material, o.renderer.materialValues, o.renderer.materialOverrides);
        if (!validMaterial(material)) {
            throw std::runtime_error("Invalid bounce material");
        }
        const Vec3 albedo{std::clamp(material.color.x, 0.f, .95f),
                          std::clamp(material.color.y, 0.f, .95f),
                          std::clamp(material.color.z, 0.f, .95f)};
        auto normals = normalMatrix(t.rotation, t.scale);
        for (std::size_t i = 0; i < count; ++i) {
            Triangle tri;
            tri.albedo = albedo;
            for (int c = 0; c < 3; ++c) {
                const auto& v = data.vertices[i * 3 + c];
                tri.p[c] = t.position + rotateVector(t.rotation, v.position * t.scale);
                tri.n[c] = normalize(Vec3{normals.m[0] * v.normal.x + normals.m[3] * v.normal.y +
                                              normals.m[6] * v.normal.z,
                                          normals.m[1] * v.normal.x + normals.m[4] * v.normal.y +
                                              normals.m[7] * v.normal.z,
                                          normals.m[2] * v.normal.x + normals.m[5] * v.normal.y +
                                              normals.m[8] * v.normal.z});
            }
            triangles.push_back(tri);
        }
    }
    check(*progress);
    progress->total = workTexels;
    Bvh bvh(triangles);
    auto result = std::make_shared<Result>();
    const auto parent = assets.resolve("Lightmaps");
    std::filesystem::create_directories(parent);
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    for (unsigned attempt = 0;; ++attempt) {
        auto directory = parent / ("Bake-" + std::to_string(stamp) + "-" + std::to_string(attempt));
        if (std::filesystem::create_directory(directory)) {
            result->directory = directory;
            break;
        }
        if (attempt >= 100) {
            throw std::runtime_error("Cannot allocate bake output folder");
        }
    }
    progress->phase = 1;
    for (const auto& range : ranges) {
        const int tile = range.tile, size = range.grid * tile;
        std::vector<unsigned char> image(std::size_t(size) * size * 3, 0);
        std::vector<unsigned char> indirectImage(image.size(), 0);
        for (std::size_t face = 0; face < range.count; ++face) {
            check(*progress);
            const auto& tri = triangles[range.first + face];
            std::vector<Vec3> bounced(std::size_t(tile) * tile);
            for (int y = 0; y < tile; ++y) {
                check(*progress);
                for (int x = 0; x < tile; ++x) {
                    const float gutter = tile >= 6 ? 2.5f : .5f, span = tile - 2 * gutter;
                    float u = std::clamp((x + .5f - gutter) / span, 0.f, 1.f),
                          v = std::clamp((y + .5f - gutter) / span, 0.f, 1.f);
                    if (u + v > 1) {
                        float sum = u + v;
                        u /= sum;
                        v /= sum;
                    }
                    const auto position = tri.p[0] * (1 - u - v) + tri.p[1] * u + tri.p[2] * v;
                    auto n = normalize(tri.n[0] * (1 - u - v) + tri.n[1] * u + tri.n[2] * v);
                    const auto irradiance =
                        directLighting(position, n, range.first + face, lights, bvh, settings.bias);
                    const auto indirect =
                        indirectLighting(position,
                                         n,
                                         range.first + face,
                                         triangles,
                                         lights,
                                         bvh,
                                         settings,
                                         *progress,
                                         std::uint32_t((range.first + face + 1) * 747796405ull +
                                                       std::size_t(y * tile + x) * 2891336453ull));
                    bounced[std::size_t(y) * tile + x] = indirect;
                    const std::size_t offset = (std::size_t(face / range.grid * tile + y) * size +
                                                face % range.grid * tile + x) *
                                               3;
                    auto encode = [](float v) {
                        return static_cast<unsigned char>(
                            std::sqrt(std::clamp(v / 16.f, 0.f, 1.f)) * 255.f + .5f);
                    };
                    image[offset] = encode(irradiance.z);
                    image[offset + 1] = encode(irradiance.y);
                    image[offset + 2] = encode(irradiance.x);
                }
                progress->done += tile;
            }
            // A chart-local 3x3 filter reduces Monte Carlo noise without atlas bleed.
            for (int y = 0; y < tile; ++y) {
                check(*progress);
                for (int x = 0; x < tile; ++x) {
                    Vec3 value{};
                    float weight = 0;
                    const int radius = settings.denoise ? 1 : 0;
                    for (int dy = -radius; dy <= radius; ++dy) {
                        for (int dx = -radius; dx <= radius; ++dx) {
                            const auto p = std::size_t(std::clamp(y + dy, 0, tile - 1)) * tile +
                                           std::clamp(x + dx, 0, tile - 1);
                            value = value + bounced[p];
                            ++weight;
                        }
                    }
                    value = value * (1 / weight);
                    const auto offset = (std::size_t(face / range.grid * tile + y) * size +
                                         face % range.grid * tile + x) *
                                        3;
                    auto encode = [](float v) {
                        return static_cast<unsigned char>(
                            std::sqrt(std::clamp(v / 16.f, 0.f, 1.f)) * 255.f + .5f);
                    };
                    indirectImage[offset] = encode(value.z);
                    indirectImage[offset + 1] = encode(value.y);
                    indirectImage[offset + 2] = encode(value.x);
                }
            }
        }
        check(*progress);
        const auto file = result->directory / (std::to_string(range.entity.index) + ".tga");
        writeAtlas(file, size, image);
        if (settings.bounces > 0) {
            writeAtlas(indirectAtlas(file.generic_string()), size, indirectImage);
        }
        result->assignments.push_back(
            {range.entity,
             file.lexically_relative(std::filesystem::weakly_canonical(assets.root))
                 .generic_string(),
             range.grid});
    }
    check(*progress);
    Json manifest = {{"format", "LegacyEngineLightmapBake"},
                     {"version", 4},
                     {"indirectBounces", settings.bounces},
                     {"indirectSamples", settings.samples},
                     {"indirectMaxDistance", settings.maxDistance},
                     {"indirectDenoise", settings.denoise},
                     {"staticState", staticState},
                     {"texelsPerMetre", settings.tile},
                     {"minimumChartSize", settings.tile},
                     {"rayBias", settings.bias},
                     {"staticLights", lights.size()},
                     {"triangles", triangles.size()},
                     {"encoding", "sqrt(rgb/16)"},
                     {"objects", Json::array()}};
    for (const auto& a : result->assignments) {
        const auto object = std::find_if(
            objects.begin(), objects.end(), [&](const auto& o) { return o.entity == a.entity; });
        const auto& t = object->transform;
        manifest["objects"].push_back(
            {{"entityIndex", a.entity.index},
             {"atlas", a.path},
             {"grid", a.grid},
             {"position", Json::array({t.position.x, t.position.y, t.position.z})},
             {"rotation", Json::array({t.rotation.x, t.rotation.y, t.rotation.z, t.rotation.w})},
             {"scale", Json::array({t.scale.x, t.scale.y, t.scale.z})},
             {"indirect", settings.bounces > 0 ? indirectAtlas(a.path) : std::string{}}});
    }
    writeJsonFile(result->directory / "bake.json", manifest);
    check(*progress);
    return result;
}
} // namespace Lightmapping
