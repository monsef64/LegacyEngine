#pragma once
#include "core/math.hpp"
#include "project/json_io.hpp"
#include <algorithm>
#include <type_traits>
#include <vector>

struct CloudSystem {
    bool enabled = true, volumetric = false;
    Vec3 color{1, 1, 1}, size{100, 15, 100};
    float coverage = .5f, density = 1, scale = .08f, speed = .4f, opacity = .8f;
    int steps = 12;
};
struct River {
    bool enabled = true, editCurve = false;
    bool flowing = true, transparent = false, collisionEffects = true;
    float flowAmount = 1, collisionStrength = .5f;
    Vec3 color{.03f, .28f, .36f};
    float width = 3, opacity = .85f, speed = 1, waveScale = 2, waveStrength = .15f, foam = .3f;
    int subdivisions = 12;
    std::vector<Vec3> points{{-10, 0, 0}, {0, 0, 3}, {10, 0, 0}};
};
namespace Environment {
inline bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
inline bool range(float v, float lo, float hi) {
    return std::isfinite(v) && v >= lo && v <= hi;
}
inline Json vec(Vec3 v) {
    return Json::array({v.x, v.y, v.z});
}
inline Vec3 vec(const Json& j) {
    if (!j.is_array() || j.size() != 3) {
        throw std::runtime_error("Expected three vector coordinates");
    }
    Vec3 v{j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>()};
    if (!finite(v)) {
        throw std::runtime_error("Invalid vector");
    }
    return v;
}
inline bool color(Vec3 v) {
    return finite(v) && range(v.x, 0, 1) && range(v.y, 0, 1) && range(v.z, 0, 1);
}
inline bool valid(const CloudSystem& c) {
    return color(c.color) && finite(c.size) && range(c.size.x, .1f, 10000) &&
           range(c.size.y, .1f, 10000) && range(c.size.z, .1f, 10000) && range(c.coverage, 0, 1) &&
           range(c.density, 0, 10) && range(c.scale, .001f, 10) && range(c.speed, -100, 100) &&
           range(c.opacity, 0, 1) && c.steps >= 4 && c.steps <= 24;
}
inline bool valid(const River& r) {
    if (!color(r.color) || !range(r.width, .05f, 1000) || !range(r.opacity, 0, 1) ||
        !range(r.flowAmount, 0, 2) || !range(r.collisionStrength, 0, 2) ||
        !range(r.speed, -100, 100) || !range(r.waveScale, .01f, 100) ||
        !range(r.waveStrength, 0, 1) || !range(r.foam, 0, 1) || r.subdivisions < 2 ||
        r.subdivisions > 32 || r.points.size() < 2 || r.points.size() > 64) {
        return false;
    }
    for (auto p : r.points) {
        if (!finite(p) || std::abs(p.x) > 100000 || std::abs(p.y) > 100000 ||
            std::abs(p.z) > 100000) {
            return false;
        }
    }
    return true;
}
inline Json settings(const CloudSystem& c) {
    return {{"enabled", c.enabled},
            {"volumetric", c.volumetric},
            {"color", vec(c.color)},
            {"size", vec(c.size)},
            {"coverage", c.coverage},
            {"density", c.density},
            {"scale", c.scale},
            {"speed", c.speed},
            {"opacity", c.opacity},
            {"steps", c.steps}};
}
inline Json settings(const River& r) {
    Json p = Json::array();
    for (auto v : r.points) {
        p.push_back(vec(v));
    }
    return {{"enabled", r.enabled},
            {"color", vec(r.color)},
            {"flowing", r.flowing},
            {"flowAmount", r.flowAmount},
            {"transparent", r.transparent},
            {"collisionEffects", r.collisionEffects},
            {"collisionStrength", r.collisionStrength},
            {"width", r.width},
            {"opacity", r.opacity},
            {"speed", r.speed},
            {"waveScale", r.waveScale},
            {"waveStrength", r.waveStrength},
            {"foam", r.foam},
            {"subdivisions", r.subdivisions},
            {"points", p}};
}
template <class T> T parse(const Json& j) {
    T c;
#define ENV_VALUE(name) c.name = j.value(#name, c.name)
    ENV_VALUE(enabled);
    ENV_VALUE(opacity);
    ENV_VALUE(speed);
    if (j.contains("color")) {
        c.color = vec(j.at("color"));
    }
    if constexpr (std::is_same_v<T, CloudSystem>) {
        ENV_VALUE(volumetric);
        ENV_VALUE(coverage);
        ENV_VALUE(density);
        ENV_VALUE(scale);
        ENV_VALUE(steps);
        if (j.contains("size")) {
            c.size = vec(j.at("size"));
        }
    } else {
        ENV_VALUE(flowing);
        ENV_VALUE(flowAmount);
        ENV_VALUE(transparent);
        ENV_VALUE(collisionEffects);
        ENV_VALUE(collisionStrength);
        ENV_VALUE(width);
        ENV_VALUE(waveScale);
        ENV_VALUE(waveStrength);
        ENV_VALUE(foam);
        ENV_VALUE(subdivisions);
        if (j.contains("points")) {
            const auto& p = j.at("points");
            if (!p.is_array() || p.size() < 2 || p.size() > 64) {
                throw std::runtime_error("River needs 2..64 points");
            }
            c.points.clear();
            for (const auto& v : p) {
                c.points.push_back(vec(v));
            }
        }
    }
#undef ENV_VALUE
    if (!valid(c)) {
        throw std::runtime_error("Environment component value outside supported range");
    }
    return c;
}
inline Vec3 curve(const River& r, float t) {
    int n = int(r.points.size());
    float x = std::clamp(t, 0.f, 1.f) * (n - 1);
    int i = std::min(int(x), n - 2);
    float u = x - i;
    auto p0 = r.points[std::max(0, i - 1)], p1 = r.points[i], p2 = r.points[i + 1],
         p3 = r.points[std::min(n - 1, i + 2)];
    return (p1 * 2 + (p2 - p0) * u + (p0 * 2 - p1 * 5 + p2 * 4 - p3) * (u * u) +
            (p1 * 3 - p0 - p2 * 3 + p3) * (u * u * u)) *
           .5f;
}
} // namespace Environment
