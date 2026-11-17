#pragma once
#include "core/math.hpp"
#include "project/json_io.hpp"
struct Fog {
    bool enabled = true;
    Vec3 color{.5f, .6f, .7f};
    float density = .02f, start = 0, end = 1000;
};
namespace Fogs {
inline bool valid(const Fog& f) {
    return std::isfinite(f.color.x) && std::isfinite(f.color.y) && std::isfinite(f.color.z) &&
           f.color.x >= 0 && f.color.x <= 1 && f.color.y >= 0 && f.color.y <= 1 && f.color.z >= 0 &&
           f.color.z <= 1 && std::isfinite(f.density) && f.density >= 0 && f.density <= 10 &&
           std::isfinite(f.start) && f.start >= 0 && std::isfinite(f.end) && f.end > f.start &&
           f.end <= 1000000;
}
inline Json settings(const Fog& f) {
    return {{"enabled", f.enabled},
            {"color", {f.color.x, f.color.y, f.color.z}},
            {"density", f.density},
            {"start", f.start},
            {"end", f.end}};
}
inline Fog parse(const Json& j) {
    Fog f;
    f.enabled = j.value("enabled", true);
    auto c = j.value("color", Json::array({.5, .6, .7}));
    if (!c.is_array() || c.size() != 3) {
        throw std::runtime_error("Invalid Fog color");
    }
    f.color = {c.at(0).get<float>(), c.at(1).get<float>(), c.at(2).get<float>()};
    f.density = j.value("density", .02f);
    f.start = j.value("start", 0.f);
    f.end = j.value("end", 1000.f);
    if (!valid(f)) {
        throw std::runtime_error("Invalid Fog settings");
    }
    return f;
}
} // namespace Fogs
