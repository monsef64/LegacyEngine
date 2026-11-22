#pragma once
#include "core/math.hpp"
#include "materials/material.hpp"
#include "project/json_io.hpp"

// A box projector along local Z; texture coordinates use local XY.
struct Decal {
    bool enabled = true;
    std::string texture;
    Vec3 color{1, 1, 1};
    float opacity = 1, width = 1, height = 1, depth = .2f;
    int sortingOrder = 0;
};
namespace Decals {
inline Json settings(const Decal& d) {
    return {{"enabled", d.enabled},
            {"texture", d.texture},
            {"color", {d.color.x, d.color.y, d.color.z}},
            {"opacity", d.opacity},
            {"width", d.width},
            {"height", d.height},
            {"depth", d.depth},
            {"sortingOrder", d.sortingOrder}};
}
inline Decal parse(const Json& j) {
    Decal d;
    d.enabled = j.value("enabled", true);
    d.texture = j.value("texture", std::string{});
    d.opacity = j.value("opacity", 1.f);
    d.width = j.value("width", 1.f);
    d.height = j.value("height", 1.f);
    d.depth = j.value("depth", .2f);
    d.sortingOrder = j.value("sortingOrder", 0);
    if (j.contains("color")) {
        const auto& c = j.at("color");
        if (!c.is_array() || c.size() != 3) {
            throw std::runtime_error("Invalid Decal color");
        }
        d.color = {c[0].get<float>(), c[1].get<float>(), c[2].get<float>()};
    }
    auto range = [](float x, float a, float b) { return std::isfinite(x) && x >= a && x <= b; };
    if (!materialTexturePath(d.texture) || !range(d.opacity, 0, 1) ||
        !range(d.width, .001f, 1000) || !range(d.height, .001f, 1000) ||
        !range(d.depth, .001f, 1000) || !range(d.color.x, 0, 16) || !range(d.color.y, 0, 16) ||
        !range(d.color.z, 0, 16) || d.sortingOrder < -10000 || d.sortingOrder > 10000) {
        throw std::runtime_error("Invalid Decal settings");
    }
    return d;
}
} // namespace Decals
