#pragma once
#include "rendering/mesh.hpp"
#include <algorithm>
#include <array>
#include <map>
#include <set>
#include <tuple>

// One source point per occupied voxel, plus axis extremes. Keeping source
// points avoids expanding the shape, but a simplified hull may cut into it.
inline std::vector<Vec3> simplifyConvexPoints(const MeshData& mesh, int detail) {
    if (mesh.vertices.empty()) {
        return {};
    }
    detail = std::clamp(detail, 2, 15);
    Vec3 low = mesh.vertices.front().position, high = low;
    std::array<Vec3, 6> extremes;
    extremes.fill(low);
    for (const auto& vertex : mesh.vertices) {
        const auto p = vertex.position;
        if (p.x < low.x) {
            low.x = p.x;
            extremes[0] = p;
        }
        if (p.x > high.x) {
            high.x = p.x;
            extremes[1] = p;
        }
        if (p.y < low.y) {
            low.y = p.y;
            extremes[2] = p;
        }
        if (p.y > high.y) {
            high.y = p.y;
            extremes[3] = p;
        }
        if (p.z < low.z) {
            low.z = p.z;
            extremes[4] = p;
        }
        if (p.z > high.z) {
            high.z = p.z;
            extremes[5] = p;
        }
    }
    const auto size = high - low, center = (high + low) * .5f;
    auto cell = [&](float p, float minimum, float length) {
        return length > 0 ? std::clamp(int((p - minimum) / length * detail), 0, detail - 1) : 0;
    };
    std::map<int, Vec3> cells;
    for (const auto& vertex : mesh.vertices) {
        const auto p = vertex.position;
        const int key = cell(p.x, low.x, size.x) +
                        detail * (cell(p.y, low.y, size.y) + detail * cell(p.z, low.z, size.z));
        auto found = cells.find(key);
        if (found == cells.end() ||
            dot(p - center, p - center) > dot(found->second - center, found->second - center)) {
            cells[key] = p;
        }
    }
    std::set<std::tuple<float, float, float>> points;
    for (const auto& entry : cells) {
        points.emplace(entry.second.x, entry.second.y, entry.second.z);
    }
    for (const auto& p : extremes) {
        points.emplace(p.x, p.y, p.z);
    }
    std::vector<Vec3> result;
    result.reserve(points.size());
    for (const auto& p : points) {
        result.push_back({std::get<0>(p), std::get<1>(p), std::get<2>(p)});
    }
    return result;
}
