#pragma once
#include "rendering/mesh.hpp"
#include "terrain/terrain.hpp"
namespace Terrains {
struct Vertex {
    Vec3 position, normal;
    std::array<float, 4> weights;
};
inline std::vector<Vertex> chunk(const Data& d, int cx, int cz, int step, bool skirts = true) {
    std::vector<Vertex> out;
    auto vertex = [&](int x, int z) {
        return Vertex{point(d, x, z), normal(d, x, z), d.weights[z * d.resolution + x]};
    };
    auto tri = [&](Vertex a, Vertex b, Vertex c) {
        out.push_back(a);
        out.push_back(b);
        out.push_back(c);
    };
    const int x0 = cx * chunkCells, z0 = cz * chunkCells;
    for (int z = z0; z < z0 + chunkCells; z += step) {
        for (int x = x0; x < x0 + chunkCells; x += step) {
            auto a = vertex(x, z), b = vertex(x + step, z), c = vertex(x, z + step),
                 e = vertex(x + step, z + step);
            tri(a, c, b);
            tri(b, c, e);
        }
    }
    if (skirts) {
        float drop = std::max(2.f, d.size / (d.resolution - 1) * 8);
        for (int edge = 0; edge < 4; ++edge) {
            if ((edge == 0 && z0 == 0) || (edge == 1 && x0 + chunkCells == d.resolution - 1) ||
                (edge == 2 && z0 + chunkCells == d.resolution - 1) || (edge == 3 && x0 == 0)) {
                continue;
            }
            for (int i = 0; i < chunkCells; i += step) {
                int x = x0 + (edge == 0   ? i
                              : edge == 1 ? chunkCells
                              : edge == 2 ? chunkCells - i
                                          : 0),
                    z = z0 + (edge == 0   ? 0
                              : edge == 1 ? i
                              : edge == 2 ? chunkCells
                                          : chunkCells - i);
                int nx = x0 + (edge == 0   ? i + step
                               : edge == 1 ? chunkCells
                               : edge == 2 ? chunkCells - i - step
                                           : 0),
                    nz = z0 + (edge == 0   ? 0
                               : edge == 1 ? i + step
                               : edge == 2 ? chunkCells
                                           : chunkCells - i - step);
                auto a = vertex(x, z), b = vertex(nx, nz), c = a, e = b;
                c.position.y -= drop;
                e.position.y -= drop;
                tri(a, b, c);
                tri(b, e, c);
            }
        }
    }
    return out;
}
inline std::vector<Vec3> collision(const Data& d) {
    std::vector<Vec3> mesh;
    mesh.reserve(std::size_t(d.resolution - 1) * (d.resolution - 1) * 6);
    for (int z = 0; z < d.resolution - 1; ++z) {
        for (int x = 0; x < d.resolution - 1; ++x) {
            auto a = point(d, x, z), b = point(d, x + 1, z), c = point(d, x, z + 1),
                 e = point(d, x + 1, z + 1);
            for (auto p : {a, c, b, b, c, e}) {
                mesh.push_back(p);
            }
        }
    }
    return mesh;
}
} // namespace Terrains

namespace Terrains {
// Bake geometry uses the exact full-resolution surface, without render skirts.
inline MeshData bakeMesh(const Data& d) {
    MeshData mesh;
    mesh.vertices.reserve(std::size_t(d.resolution - 1) * (d.resolution - 1) * 6);
    for (int z = 0; z < d.resolution - 1; ++z) {
        for (int x = 0; x < d.resolution - 1; ++x) {
            const int dx[]{0, 0, 1, 1, 0, 1}, dz[]{0, 1, 0, 0, 1, 1};
            for (int k = 0; k < 6; ++k) {
                mesh.vertices.push_back(
                    {point(d, x + dx[k], z + dz[k]), normal(d, x + dx[k], z + dz[k])});
            }
        }
    }
    return mesh;
}
} // namespace Terrains
