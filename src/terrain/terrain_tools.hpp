#pragma once
#include "../../third_party/stb/stb_image.h"
#include "core/hierarchy.hpp"
#include "terrain/terrain_brushes.hpp"
#include "terrain/terrain_io.hpp"
namespace Terrains {
inline void writable(Terrain& t) {
    if (!t.data) {
        throw std::runtime_error("Terrain asset unavailable");
    }
    if (!t.data.unique()) {
        t.data = std::make_shared<Data>(*t.data);
    }
}
inline bool intersect(const Data& d, Vec3 origin, Vec3 direction, Vec3& hit) {
    float entry = 0, exit = 1e6f;
    for (int axis : {0, 2}) {
        float o = axis == 0 ? origin.x : origin.z, v = axis == 0 ? direction.x : direction.z;
        if (std::abs(v) < 1e-9f) {
            if (o < 0 || o > d.size) {
                return false;
            }
        } else {
            float a = -o / v, b = (d.size - o) / v;
            if (a > b) {
                std::swap(a, b);
            }
            entry = std::max(entry, a);
            exit = std::min(exit, b);
        }
    }
    if (entry > exit) {
        return false;
    }
    const float cell = d.size / (d.resolution - 1);
    Vec3 start = origin + direction * (entry + 1e-5f);
    int x = std::clamp(int(std::floor(start.x / cell)), 0, d.resolution - 2),
        z = std::clamp(int(std::floor(start.z / cell)), 0, d.resolution - 2);
    int sx = direction.x >= 0 ? 1 : -1, sz = direction.z >= 0 ? 1 : -1;
    auto next = [&](int index, float o, float v, int step) {
        return std::abs(v) < 1e-9f ? 1e30f : ((index + (step > 0 ? 1 : 0)) * cell - o) / v;
    };
    float tx = next(x, origin.x, direction.x, sx), tz = next(z, origin.z, direction.z, sz),
          dx = std::abs(direction.x) < 1e-9f ? 1e30f : std::abs(cell / direction.x),
          dz = std::abs(direction.z) < 1e-9f ? 1e30f : std::abs(cell / direction.z);
    auto triangle = [&](Vec3 a, Vec3 b, Vec3 c, float& t) {
        Vec3 e1 = b - a, e2 = c - a, p = cross(direction, e2);
        float determinant = dot(e1, p);
        if (std::abs(determinant) < 1e-8f) {
            return false;
        }
        float inv = 1 / determinant;
        Vec3 delta = origin - a;
        float u = dot(delta, p) * inv;
        if (u < 0 || u > 1) {
            return false;
        }
        Vec3 q = cross(delta, e1);
        float v = dot(direction, q) * inv;
        if (v < 0 || u + v > 1) {
            return false;
        }
        t = dot(e2, q) * inv;
        return t >= entry - 1e-4f && t <= exit;
    };
    for (int i = 0; i < d.resolution * 2 + 4 && x >= 0 && z >= 0 && x < d.resolution - 1 &&
                    z < d.resolution - 1;
         ++i) {
        float best = 1e30f, t;
        auto a = point(d, x, z), b = point(d, x + 1, z), c = point(d, x, z + 1),
             e = point(d, x + 1, z + 1);
        if (triangle(a, c, b, t)) {
            best = t;
        }
        if (triangle(b, c, e, t)) {
            best = std::min(best, t);
        }
        if (best < 1e29f && best <= std::min(tx, tz) + 1e-4f) {
            hit = origin + direction * best;
            return true;
        }
        if (std::min(tx, tz) > exit) {
            return false;
        }
        const bool advanceX = tx <= tz, advanceZ = tz <= tx;
        if (advanceX) {
            x += sx;
            tx += dx;
        }
        if (advanceZ) {
            z += sz;
            tz += dz;
        }
    }
    return false;
}
inline void brush(Terrain& terrain,
                  Vec3 center,
                  int tool,
                  float radius,
                  float strength,
                  float falloff,
                  float height,
                  int layer,
                  float dt,
                  int shape = 0) {
    writable(terrain);
    auto& d = *terrain.data;
    const int n = d.resolution;
    float cell = d.size / (n - 1);
    int x0 = std::max(0, int(std::floor((center.x - radius) / cell))),
        x1 = std::min(n - 1, int(std::ceil((center.x + radius) / cell))),
        z0 = std::max(0, int(std::floor((center.z - radius) / cell))),
        z1 = std::min(n - 1, int(std::ceil((center.z + radius) / cell)));
    if (x0 > x1 || z0 > z1) {
        return;
    }
    std::vector<float> original;
    if (tool == 2) {
        original = d.heights;
    }
    bool changed = false;
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            float dx = x * cell - center.x, dz = z * cell - center.z,
                  r2 = (dx * dx + dz * dz) / (radius * radius);
            if (r2 > 1) {
                continue;
            }
            float weight = brushWeight(shape, dx / radius, dz / radius, falloff),
                  amount = strength * dt * weight;
            auto i = z * n + x;
            if (tool == 4) {
                auto old = d.weights[i];
                for (int k = 0; k < 4; ++k) {
                    d.weights[i][k] +=
                        ((k == layer ? 1.f : 0.f) - d.weights[i][k]) * std::clamp(amount, 0.f, 1.f);
                }
                changed |= old != d.weights[i];
            } else {
                float old = d.heights[i], value = old;
                if (tool < 2) {
                    value += amount * (tool == 0 ? 1 : -1);
                } else if (tool == 3) {
                    value += (height - old) * std::clamp(amount, 0.f, 1.f);
                } else {
                    float sum = 0;
                    for (int rz = std::max(0, z - 1); rz <= std::min(n - 1, z + 1); ++rz) {
                        for (int rx = std::max(0, x - 1); rx <= std::min(n - 1, x + 1); ++rx) {
                            sum += original[rz * n + rx];
                        }
                    }
                    int count = (std::min(n - 1, z + 1) - std::max(0, z - 1) + 1) *
                                (std::min(n - 1, x + 1) - std::max(0, x - 1) + 1);
                    value += (sum / count - old) * std::clamp(amount, 0.f, 1.f);
                }
                d.heights[i] = std::clamp(value, -10000.f, 10000.f);
                changed |= old != d.heights[i];
            }
        }
    }
    if (changed) {
        d.revision = nextRevision();
        if (tool != 4) {
            d.shapeRevision = d.revision;
        }
        int count = chunkCount(d);
        for (int z = std::max(0, (z0 - 1) / 32); z <= std::min(count - 1, (z1 + 1) / 32); ++z) {
            for (int x = std::max(0, (x0 - 1) / 32); x <= std::min(count - 1, (x1 + 1) / 32); ++x) {
                d.chunks[z * count + x] = d.revision;
                if (tool != 4) {
                    d.heightChunks[z * count + x] = d.shapeRevision;
                }
            }
        }
    }
}
inline void importHeightmap(Terrain& terrain,
                            const AssetManager& assets,
                            const std::string& path,
                            float scale,
                            float offset) {
    auto file = assets.resolve(path);
    auto size = std::filesystem::file_size(file);
    if (!size || size > 64 * 1024 * 1024) {
        throw std::runtime_error("Heightmap exceeds 64MiB");
    }
    std::ifstream in(file, std::ios::binary);
    std::vector<unsigned char> bytes(size);
    if (!in.read(reinterpret_cast<char*>(bytes.data()), std::streamsize(size))) {
        throw std::runtime_error("Cannot read heightmap");
    }
    int w = 0, h = 0, c = 0;
    if (!stbi_info_from_memory(bytes.data(), int(size), &w, &h, &c) || w < 1 || h < 1 || w > 4096 ||
        h > 4096) {
        throw std::runtime_error("Heightmap requires at most 4096x4096 pixels");
    }
    bool high = stbi_is_16_bit_from_memory(bytes.data(), int(size));
    void* pixels =
        high ? static_cast<void*>(stbi_load_16_from_memory(bytes.data(), int(size), &w, &h, &c, 1))
             : static_cast<void*>(stbi_load_from_memory(bytes.data(), int(size), &w, &h, &c, 1));
    if (!pixels) {
        throw std::runtime_error("Cannot decode heightmap");
    }
    std::unique_ptr<void, decltype(&stbi_image_free)> cleanup(pixels, stbi_image_free);
    writable(terrain);
    auto& d = *terrain.data;
    for (int z = 0; z < d.resolution; ++z) {
        for (int x = 0; x < d.resolution; ++x) {
            float fx = float(x) * (w - 1) / (d.resolution - 1),
                  fz = float(z) * (h - 1) / (d.resolution - 1);
            int ix = int(fx), iz = int(fz);
            auto sample = [&](int a, int b) {
                return high ? static_cast<stbi_us*>(pixels)[b * w + a] / 65535.f
                            : static_cast<unsigned char*>(pixels)[b * w + a] / 255.f;
            };
            float a = fx - ix, b = fz - iz;
            float value =
                (sample(ix, iz) * (1 - a) + sample(std::min(ix + 1, w - 1), iz) * a) * (1 - b) +
                (sample(ix, std::min(iz + 1, h - 1)) * (1 - a) +
                 sample(std::min(ix + 1, w - 1), std::min(iz + 1, h - 1)) * a) *
                    b;
            d.heights[z * d.resolution + x] = std::clamp(offset + value * scale, -10000.f, 10000.f);
        }
    }
    invalidate(d);
}
} // namespace Terrains
