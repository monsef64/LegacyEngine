#pragma once
#include "effects/laser.hpp"
#include "project/json_io.hpp"
namespace Lasers {
inline Json settings(const LaserSettings& s) {
    return {{"enabled", s.enabled},
            {"color", {s.color.x, s.color.y, s.color.z}},
            {"intensity", s.intensity},
            {"range", s.range},
            {"width", s.width},
            {"glowWidth", s.glowWidth},
            {"impactSize", s.impactSize}};
}
inline LaserSettings parse(const Json& j) {
    LaserSettings s;
    s.enabled = j.value("enabled", s.enabled);
    if (j.contains("color")) {
        const auto& c = j.at("color");
        if (!c.is_array() || c.size() != 3) {
            throw std::runtime_error("Expected laser RGB color");
        }
        s.color = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>()};
    }
#define F(name) s.name = j.value(#name, s.name)
    F(intensity);
    F(range);
    F(width);
    F(glowWidth);
    F(impactSize);
#undef F
    auto valid = [](float x, float lo, float hi) { return std::isfinite(x) && x >= lo && x <= hi; };
    if (!j.is_object() || !valid(s.color.x, 0, 16) || !valid(s.color.y, 0, 16) ||
        !valid(s.color.z, 0, 16) || !valid(s.intensity, 0, 100) || !valid(s.range, .01f, 10000) ||
        !valid(s.width, .001f, 10) || !valid(s.glowWidth, s.width, 100) ||
        !valid(s.impactSize, .001f, 100)) {
        throw std::runtime_error("Invalid laser settings");
    }
    return s;
}
} // namespace Lasers
