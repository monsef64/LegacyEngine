#pragma once
#include <algorithm>
#include <array>
#include <cmath>
namespace Terrains {
inline constexpr std::array<const char*, 15> brushNames{{"Soft round",
                                                         "Hard round",
                                                         "Bell",
                                                         "Plateau",
                                                         "Cone",
                                                         "Square",
                                                         "Diamond",
                                                         "Horizontal ridge",
                                                         "Vertical ridge",
                                                         "Diagonal ridge",
                                                         "Ring",
                                                         "Twin peaks",
                                                         "Crater rim",
                                                         "Rocky",
                                                         "Dunes"}};
// Deterministic, continuous masks shared by thumbnail generation and sculpting.
// Coordinates are normalized to the brush radius. All masks stay in [0,1].
inline float brushMask(int shape, float x, float z) {
    float r = std::sqrt(x * x + z * z);
    if (r >= 1) {
        return 0;
    }
    float edge = std::clamp((1 - r) * 8, 0.f, 1.f), soft = 1 - r * r;
    auto gaussian = [](float x, float z, float sharpness) {
        return std::exp(-(x * x + z * z) * sharpness);
    };
    float value = soft;
    switch (shape) {
    case 0:
        value = soft;
        break;
    case 1:
        value = edge;
        break;
    case 2:
        value = gaussian(x, z, 5) * edge;
        break;
    case 3:
        value = std::clamp((.85f - r) * 5, 0.f, 1.f);
        break;
    case 4:
        value = 1 - r;
        break;
    case 5:
        value = std::clamp((.7f - std::max(std::abs(x), std::abs(z))) * 10, 0.f, 1.f) * edge;
        break;
    case 6:
        value = std::clamp((.95f - std::abs(x) - std::abs(z)) * 5, 0.f, 1.f) * edge;
        break;
    case 7:
        value = std::exp(-z * z * 35) * soft;
        break;
    case 8:
        value = std::exp(-x * x * 35) * soft;
        break;
    case 9:
        value = std::exp(-(x - z) * (x - z) * 20) * soft;
        break;
    case 10:
        value = std::exp(-(r - .58f) * (r - .58f) * 80) * edge;
        break;
    case 11:
        value = std::max(gaussian(x - .38f, z, 18), gaussian(x + .38f, z, 18)) * edge;
        break;
    case 12:
        value = std::exp(-(r - .65f) * (r - .65f) * 45) * (.75f + .25f * std::sin(x * 11 + z * 7)) *
                edge;
        break;
    case 13:
        value = soft * (.5f + .2f * std::sin(x * 19 + z * 7) + .18f * std::cos(z * 23 - x * 9) +
                        .12f * std::sin(x * 43 + z * 37));
        break;
    case 14:
        value = soft * (.5f + .5f * std::cos(z * 15 + std::sin(x * 5) * 1.5f));
        break;
    default:
        break;
    }
    return std::clamp(value, 0.f, 1.f);
}
inline float brushWeight(int shape, float x, float z, float falloff) {
    // Shape 0 retains the original round brush response, including hard falloff=0.
    float r2 = x * x + z * z;
    if (r2 >= 1) {
        return 0;
    }
    float envelope = std::pow(std::max(0.f, 1 - r2), falloff * 4);
    return shape == 0 ? envelope : brushMask(shape, x, z) * envelope;
}
} // namespace Terrains
