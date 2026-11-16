#pragma once
#include "core/math.hpp"

// Local -Z emits a continuous, collider-clipped beam. Runtime hit data is never saved.
struct LaserSettings {
    bool enabled = true;
    Vec3 color{1, .03f, .01f};
    float intensity = 4, range = 50, width = .025f, glowWidth = .18f, impactSize = .25f;
};
struct Laser : LaserSettings {
    Vec3 end{}, hitNormal{0, 1, 0};
    bool hit = false, ready = false;
};
