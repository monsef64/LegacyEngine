#pragma once
#include "core/math.hpp"
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
namespace Terrains {
inline bool openTools = false;
inline constexpr int chunkCells = 16;
inline unsigned nextRevision() {
    static std::atomic<unsigned> value{1};
    return value.fetch_add(1);
}
struct Layer {
    std::string texture;
    Vec3 color{.4f, .55f, .25f};
    float tiling = 8;
    std::string normalMap, material;
    float normalStrength = 1;
};
struct FoliagePrototype {
    bool billboard = false; // Optional upright camera-facing grass; crossed cards remain default.
    std::string asset;
    Vec3 color{1, 1, 1};
    float width = 1, height = 1, minScale = .8f, maxScale = 1.2f, distance = 80;
};
struct FoliageInstance {
    float x = 0, z = 0, scale = 1, yaw = 0;
    int prototype = 0;
};
struct Data {
    std::vector<FoliagePrototype> grassTypes, treeTypes;
    std::vector<FoliageInstance> grass, trees;

    int resolution = 129;
    float size = 128;
    std::string lightmap;
    std::vector<float> heights = std::vector<float>(129 * 129, 0);
    std::vector<std::array<float, 4>> weights =
        std::vector<std::array<float, 4>>(129 * 129, {1, 0, 0, 0});
    std::array<Layer, 4> layers{{{"", {.3f, .5f, .2f}, 8},
                                 {"", {.4f, .3f, .2f}, 8},
                                 {"", {.5f, .5f, .5f}, 8},
                                 {"", {.85f, .85f, .8f}, 8}}};
    unsigned identity = nextRevision(), revision = 0, shapeRevision = 0;
    std::vector<unsigned> chunks = std::vector<unsigned>(64, 0);
    std::vector<unsigned> heightChunks = std::vector<unsigned>(64, 0);
    struct Bounds {
        unsigned revision = ~0u;
        Vec3 center, extent;
        float radius = 0;
    };
    mutable std::vector<Bounds> bounds = std::vector<Bounds>(64);
};
inline int chunkCount(const Data& d) {
    return (d.resolution - 1) / chunkCells;
}
inline void invalidate(Data& d) {
    d.revision = nextRevision();
    d.shapeRevision = d.revision;
    d.chunks.assign(chunkCount(d) * chunkCount(d), d.revision);
    d.heightChunks = d.chunks;
    d.bounds.assign(d.chunks.size(), {});
}
inline Vec3 point(const Data& d, int x, int z) {
    return {x * d.size / (d.resolution - 1),
            d.heights[z * d.resolution + x],
            z * d.size / (d.resolution - 1)};
}
inline Vec3 normal(const Data& d, int x, int z) {
    int a = std::max(0, x - 1), b = std::min(d.resolution - 1, x + 1), c = std::max(0, z - 1),
        e = std::min(d.resolution - 1, z + 1);
    float cell = d.size / (d.resolution - 1);
    return normalize(Vec3{
        -(d.heights[z * d.resolution + b] - d.heights[z * d.resolution + a]) / ((b - a) * cell),
        1,
        -(d.heights[e * d.resolution + x] - d.heights[c * d.resolution + x]) / ((e - c) * cell)});
}
} // namespace Terrains
struct Terrain {
    std::string asset;
    std::shared_ptr<Terrains::Data> data = std::make_shared<Terrains::Data>();
    // Shared immutable snapshots; editor clones before modifying data.
    std::string loaded;
    unsigned savedRevision = 0;
    bool sculpting = false;
};

namespace Terrains {
inline const Data::Bounds& chunkBounds(const Data& d, int x, int z) {
    int index = z * chunkCount(d) + x;
    auto& b = d.bounds[index];
    if (b.revision != d.heightChunks[index]) {
        float lo = 1e10f, hi = -1e10f;
        for (int rz = z * chunkCells; rz <= z * chunkCells + chunkCells; ++rz) {
            for (int rx = x * chunkCells; rx <= x * chunkCells + chunkCells; ++rx) {
                float h = d.heights[rz * d.resolution + rx];
                lo = std::min(lo, h);
                hi = std::max(hi, h);
            }
        }
        float cell = d.size / (d.resolution - 1);
        const float drop = std::max(2.f, 8 * cell);
        b.center = {(x * chunkCells + chunkCells / 2) * cell,
                    (lo + hi - drop) * .5f,
                    (z * chunkCells + chunkCells / 2) * cell};
        b.extent = {chunkCells * .5f * cell, (hi - lo + drop) * .5f, chunkCells * .5f * cell};
        b.radius = std::sqrt(dot(b.extent, b.extent));
        b.revision = d.heightChunks[index];
    }
    return b;
}
} // namespace Terrains
