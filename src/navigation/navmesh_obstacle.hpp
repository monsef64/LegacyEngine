#pragma once
#include "core/math.hpp"
#include "project/json_io.hpp"
struct NavMeshObstacle {
    bool enabled = true;
    Vec3 size{1, 2, 1}, center{0, 1, 0};
};
namespace Navigation {
inline Json obstacleSettings(const NavMeshObstacle& o) {
    return {{"enabled", o.enabled},
            {"size", {o.size.x, o.size.y, o.size.z}},
            {"center", {o.center.x, o.center.y, o.center.z}}};
}
inline NavMeshObstacle parseObstacle(const Json& j) {
    NavMeshObstacle o;
    o.enabled = j.value("enabled", true);
    auto vec = [](const Json& v) {
        Vec3 p{v.at(0).get<float>(), v.at(1).get<float>(), v.at(2).get<float>()};
        for (float f : {p.x, p.y, p.z}) {
            if (!std::isfinite(f) || std::abs(f) > 10000) {
                throw std::runtime_error("Invalid obstacle vector");
            }
        }
        return p;
    };
    if (j.contains("size")) {
        o.size = vec(j.at("size"));
    }
    if (j.contains("center")) {
        o.center = vec(j.at("center"));
    }
    if (o.size.x < .01f || o.size.y < .01f || o.size.z < .01f) {
        throw std::runtime_error("Obstacle size must be positive");
    }
    return o;
}
} // namespace Navigation
