#pragma once
#include "core/math.hpp"
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
namespace Terrains {
inline bool openTools = false;
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
struct Data {
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
    std::vector<unsigned> chunks = std::vector<unsigned>(16, 0);
};
inline int chunkCount(const Data& d) {
    return (d.resolution - 1) / 32;
}
inline void invalidate(Data& d) {
    d.revision = nextRevision();
    d.shapeRevision = d.revision;
    d.chunks.assign(chunkCount(d) * chunkCount(d), d.revision);
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
