#pragma once
#include "core/math.hpp"
#include <string>
struct LightShafts {
    bool enabled = true, useBakedTexture = false;
    Vec3 direction{0, -1, 0}, color{1, .92f, .7f};
    float height = 10, width = .3f, endWidth = 3, intensity = 1, density = .35f, falloff = 2,
          streaks = 12, fade = .15f, nearFade = 1;
    std::string texture;
};
