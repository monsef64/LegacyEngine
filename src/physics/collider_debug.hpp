// Local-space line geometry for the actual collider, including its scaled offset.
// Apply entity position/rotation only: scale is already baked into these vertices.
#pragma once

#include "core/components.hpp"
#include <algorithm>
#include <cmath>

inline MeshData colliderWireframe(const Collider& collider, Vec3 scale) {
    MeshData mesh;
    mesh.topology = MeshData::Topology::Lines;
    const Vec3 center = collider.center * scale;
    auto line = [&](Vec3 a, Vec3 b) {
        mesh.vertices.push_back({a + center, {}});
        mesh.vertices.push_back({b + center, {}});
    };
    if (collider.shape == ColliderShape::Box) {
        const Vec3 size = collider.size * scale;
        const Vec3 half{std::max(std::abs(size.x) * 0.5f, 0.0005f),
                        std::max(std::abs(size.y) * 0.5f, 0.0005f),
                        std::max(std::abs(size.z) * 0.5f, 0.0005f)};
        Vec3 corners[8];
        for (int i = 0; i < 8; ++i) {
            corners[i] = {
                i & 1 ? half.x : -half.x, i & 2 ? half.y : -half.y, i & 4 ? half.z : -half.z};
        }
        for (int i = 0; i < 8; ++i) {
            for (int bit : {1, 2, 4}) {
                if (!(i & bit)) {
                    line(corners[i], corners[i | bit]);
                }
            }
        }
        return mesh;
    }
    if (collider.shape == ColliderShape::Cone) {
        const float radius = std::max(
            std::abs(collider.radius) * std::max(std::abs(scale.x), std::abs(scale.z)), 0.0005f);
        const float height = std::max(std::abs(collider.height * scale.y), 0.001f);
        const auto points = coneHullVertices();
        const Vec3 dimensions{radius * 2, height, radius * 2};
        for (int i = 0; i < kConeSegments; ++i) {
            line(points[1 + i] * dimensions, points[1 + (i + 1) % kConeSegments] * dimensions);
            if (i % 8 == 0) {
                line(points[1 + i] * dimensions, points[0] * dimensions);
            }
        }
        return mesh;
    }
    const bool capsule = collider.shape == ColliderShape::Capsule;
    const float radialScale =
        capsule ? std::max(std::abs(scale.x), std::abs(scale.z))
                : std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)});
    const float radius = std::max(std::abs(collider.radius) * radialScale, 0.0005f);
    const float halfHeight =
        capsule ? std::max(std::abs(collider.height * scale.y), 0.001f) * 0.5f : 0;
    constexpr int slices = 24, rings = 12;
    if (!capsule) {
        // Three great circles convey the sphere without a dense latitude grid.
        for (int axis = 0; axis < 3; ++axis) {
            auto circle = [&](int i) {
                const float angle = 2 * kPi * i / slices;
                const float a = radius * std::cos(angle), b = radius * std::sin(angle);
                return axis == 0 ? Vec3{0, a, b} : (axis == 1 ? Vec3{a, 0, b} : Vec3{a, b, 0});
            };
            for (int i = 0; i < slices; ++i) {
                line(circle(i), circle(i + 1));
            }
        }
        return mesh;
    }
    auto point = [&](int ring, int slice) {
        // Two equators for a capsule: join them with straight cylinder edges.
        const int latitude = ring <= rings / 2 ? ring : ring - 1;
        const float angle = -kPi * 0.5f + kPi * latitude / rings;
        const float longitude = 2 * kPi * slice / slices;
        return Vec3{radius * std::cos(angle) * std::cos(longitude),
                    radius * std::sin(angle) + (ring <= rings / 2 ? -halfHeight : halfHeight),
                    radius * std::cos(angle) * std::sin(longitude)};
    };
    for (int r = 0; r <= rings + 1; ++r) {
        for (int i = 0; i < slices; ++i) {
            if (r == rings / 2 || r == rings / 2 + 1) {
                line(point(r, i), point(r, i + 1));
            }
            if (r < rings + 1 && i % (slices / 4) == 0) {
                line(point(r, i), point(r + 1, i));
            }
        }
    }
    return mesh;
}
