#pragma once
#include "assets/assets.hpp"
#include "assets/image_write.hpp"
#include "effects/light_shafts.hpp"
#include "materials/material.hpp"
#include "project/json_io.hpp"
namespace Shafts {
inline Json settings(const LightShafts& s) {
    return {{"enabled", s.enabled},
            {"useBakedTexture", s.useBakedTexture},
            {"direction", {s.direction.x, s.direction.y, s.direction.z}},
            {"color", {s.color.x, s.color.y, s.color.z}},
            {"height", s.height},
            {"width", s.width},
            {"endWidth", s.endWidth},
            {"intensity", s.intensity},
            {"density", s.density},
            {"falloff", s.falloff},
            {"streaks", s.streaks},
            {"fade", s.fade},
            {"nearFade", s.nearFade},
            {"texture", s.texture}};
}
inline LightShafts parse(const Json& j) {
    LightShafts s;
#define F(name) s.name = j.value(#name, s.name)
    F(enabled);
    F(useBakedTexture);
    F(height);
    F(width);
    F(endWidth);
    F(intensity);
    F(density);
    F(falloff);
    F(streaks);
    F(fade);
    F(nearFade);
    F(texture);
#undef F
    auto vec = [&](const char* key, Vec3& v) {
        if (j.contains(key)) {
            const auto& a = j.at(key);
            if (!a.is_array() || a.size() != 3) {
                throw std::runtime_error("Expected LightShafts vector");
            }
            v = {a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
        }
    };
    vec("color", s.color);
    vec("direction", s.direction);
    auto range = [](float f, float lo, float hi) { return std::isfinite(f) && f >= lo && f <= hi; };
    if (!j.is_object() || !range(s.height, .01f, 1000) || !range(s.width, .001f, 100) ||
        !range(s.endWidth, .001f, 500) || !range(s.intensity, 0, 100) || !range(s.density, 0, 1) ||
        !range(s.falloff, .1f, 8) || !range(s.streaks, 0, 64) || !range(s.fade, .01f, 1) ||
        !range(s.nearFade, .01f, 100) || !range(s.color.x, 0, 16) || !range(s.color.y, 0, 16) ||
        !range(s.color.z, 0, 16) || !range(s.direction.x, -1000, 1000) ||
        !range(s.direction.y, -1000, 1000) || !range(s.direction.z, -1000, 1000) ||
        dot(s.direction, s.direction) < 1e-8f || !materialTexturePath(s.texture)) {
        throw std::runtime_error("Invalid LightShafts settings");
    }
    return s;
}
inline float profile(float u, float v, const LightShafts& s) {
    const float radial = std::pow(std::max(0.f, 1 - std::abs(2 * u - 1)), s.falloff);
    const float ramp = std::clamp(v / s.fade, 0.f, 1.f),
                axial = std::sqrt(std::max(0.f, 1 - v)) * ramp * ramp * (3 - 2 * ramp);
    const float wave = std::sin(u * s.streaks * 6.2831853f);
    return radial * axial * (.65f + .35f * wave * wave);
}
inline std::string bake(const AssetManager& assets, Entity entity, const LightShafts& s) {
    parse(settings(s));
    const auto directory = assets.resolve("LightShafts");
    std::filesystem::create_directories(directory);
    std::string path;
    for (unsigned suffix = 0;; ++suffix) {
        path = "LightShafts/Shaft-" + std::to_string(entity.index) + "-" + std::to_string(suffix) +
               ".png";
        if (!std::filesystem::exists(assets.resolve(path))) {
            break;
        }
    }
    constexpr int size = 256;
    std::vector<unsigned char> image(size * size * 3);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const auto value = static_cast<unsigned char>(
                std::clamp(profile((x + .5f) / size, (y + .5f) / size, s), 0.f, 1.f) * 255 + .5f);
            for (int c = 0; c < 3; ++c) {
                image[(y * size + x) * 3 + c] = value;
            }
        }
    }
    writeBgrPng(assets.resolve(path), size, size, image);
    return path;
}
} // namespace Shafts
