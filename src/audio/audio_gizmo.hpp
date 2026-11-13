#pragma once
#include "audio/audio_source.hpp"
#include "rendering/mesh.hpp"
#include <cmath>

// Scene-only speaker glyph. No solid surfaces or gameplay geometry.
inline MeshData audioSourceGlyph() {
    MeshData mesh;
    mesh.topology = MeshData::Topology::Lines;
    auto line = [&](Vec3 a, Vec3 b) {
        mesh.vertices.push_back({a, {}});
        mesh.vertices.push_back({b, {}});
    };
    const Vec3 points[]{{-.45f, -.2f, 0},
                        {-.15f, -.2f, 0},
                        {.2f, -.5f, 0},
                        {.2f, .5f, 0},
                        {-.15f, .2f, 0},
                        {-.45f, .2f, 0}};
    for (int i = 0; i < 6; ++i) {
        line(points[i], points[(i + 1) % 6]);
    }
    for (float radius : {.45f, .7f}) {
        for (int i = 0; i < 16; ++i) {
            const float a = -kPi / 3 + 2 * kPi / 3 * i / 16,
                        b = -kPi / 3 + 2 * kPi / 3 * (i + 1) / 16;
            line({.2f + radius * std::cos(a), radius * std::sin(a), 0},
                 {.2f + radius * std::cos(b), radius * std::sin(b), 0});
        }
    }
    return mesh;
}
// Distance is a world-space hearing range, not scaled by the owner's Transform.
inline MeshData audioSourceRange(const AudioSource& source) {
    MeshData mesh;
    mesh.topology = MeshData::Topology::Lines;
    if (!std::isfinite(source.distance) || source.distance <= 0) {
        return mesh;
    }
    const float radius = std::min(source.distance, 10000.0f);
    constexpr int segments = 24;
    for (int axis = 0; axis < 3; ++axis) {
        for (int i = 0; i < segments; ++i) {
            auto point = [&](int j) {
                float a = 2 * kPi * j / segments;
                float u = radius * std::cos(a), v = radius * std::sin(a);
                return axis == 0 ? Vec3{0, u, v} : axis == 1 ? Vec3{u, 0, v} : Vec3{u, v, 0};
            };
            mesh.vertices.push_back({point(i), {}});
            mesh.vertices.push_back({point(i + 1), {}});
        }
    }
    return mesh;
}
