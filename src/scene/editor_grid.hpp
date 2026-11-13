#pragma once
#include "core/math.hpp"
struct GridSettings {
    bool visible = true;
    int cells = 64, plane = 0; // XZ, XY, YZ
    float spacing = 1;
    Vec3 position{}, color{.35f, .35f, .35f};
    bool valid() const {
        auto finite = [](float v) { return std::isfinite(v) && std::abs(v) <= 100000; };
        return cells >= 2 && cells <= 512 && plane >= 0 && plane <= 2 && std::isfinite(spacing) &&
               spacing >= .01f && spacing <= 1000 && finite(position.x) && finite(position.y) &&
               finite(position.z) && color.x >= 0 && color.x <= 1 && color.y >= 0 && color.y <= 1 &&
               color.z >= 0 && color.z <= 1;
    }
};
