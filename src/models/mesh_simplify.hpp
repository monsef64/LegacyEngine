#pragma once
#include "rendering/mesh.hpp"
#include <algorithm>
#include <map>
#include <tuple>
// Vertex clustering keeps triangles instead of constructing a convex hull.
// Maximum detail preserves the original geometry; lower settings can close small holes.
inline std::vector<Vec3> simplifyMeshTriangles(const MeshData& mesh, int detail) {
    detail = std::clamp(detail, 2, 15);
    std::vector<Vec3> result;
    if (mesh.vertices.empty()) {
        return result;
    }
    Vec3 low = mesh.vertices.front().position, high = low;
    for (const auto& v : mesh.vertices) {
        auto p = v.position;
        low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
    }
    const auto size = high - low;
    const int resolution = detail * detail;
    std::map<std::tuple<int, int, int>, Vec3> representatives;
    auto point = [&](Vec3 p) {
        if (detail == 15) {
            return p;
        }
        auto cell = [&](float v, float a, float span) {
            return span > 1e-12f ? std::clamp(int((v - a) / span * resolution), 0, resolution - 1)
                                 : 0;
        };
        auto key = std::make_tuple(
            cell(p.x, low.x, size.x), cell(p.y, low.y, size.y), cell(p.z, low.z, size.z));
        return representatives.emplace(key, p).first->second;
    };
    for (std::size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
        const auto a = point(mesh.vertices[i].position), b = point(mesh.vertices[i + 1].position),
                   c = point(mesh.vertices[i + 2].position);
        const auto n = cross(b - a, c - a);
        if (n.x == 0 && n.y == 0 && n.z == 0) {
            continue;
        }
        result.insert(result.end(), {a, b, c});
    }
    return result;
}
