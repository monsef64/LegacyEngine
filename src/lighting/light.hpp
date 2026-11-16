#pragma once
#include "core/math.hpp"
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string>

namespace Lighting {
inline constexpr int cookieResolution = 512; // Shared runtime and offline-bake resolution.
}

// Local -Z is the emission direction, matching the camera convention.
struct LightSettings {
    std::string cookie; // project-relative image; only SpotLight projects it
    Vec3 color{1, 1, 1};
    float intensity = 1;
    bool enabled = true, shadows = true;
    float range = 20;
    float innerAngle = 20, outerAngle = 30; // spot half-cone angles, degrees
    float shadowDistance = 50;              // directional coverage half extent around the view
    int shadowQuality = 3;                  // 0 Off, 1 Low (256), 2 Medium (512), 3 High (1024)
    float shadowBias = .0015f;              // normalized depth bias
};
struct DirectionalLight : LightSettings {};
struct PointLight : LightSettings {};
struct SpotLight : LightSettings {};
inline bool validLightCookie(const std::string& path) {
    if (path.empty()) {
        return true;
    }
    if (path.size() > 4096 || path.find('\\') != std::string::npos ||
        path.find(':') != std::string::npos || path.find('\0') != std::string::npos) {
        return false;
    }
    const std::filesystem::path p(path);
    if (p.is_absolute()) {
        return false;
    }
    for (const auto& part : p) {
        if (part == "..") {
            return false;
        }
    }
    auto ext = p.extension().string();
    for (auto& c : ext) {
        c = char(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" || ext == ".bmp";
}
inline bool validLight(const LightSettings& l) {
    return validLightCookie(l.cookie) && l.shadowQuality >= 0 && l.shadowQuality <= 3 &&
           std::isfinite(l.color.x) && std::isfinite(l.color.y) && std::isfinite(l.color.z) &&
           l.color.x >= 0 && l.color.y >= 0 && l.color.z >= 0 && l.color.x <= 100 &&
           l.color.y <= 100 && l.color.z <= 100 && std::isfinite(l.intensity) && l.intensity >= 0 &&
           l.intensity <= 1000 && std::isfinite(l.range) && l.range >= .1f && l.range <= 10000 &&
           std::isfinite(l.innerAngle) && std::isfinite(l.outerAngle) && l.innerAngle >= 0 &&
           l.innerAngle < l.outerAngle && l.outerAngle <= 85 && std::isfinite(l.shadowDistance) &&
           l.shadowDistance >= 1 && l.shadowDistance <= 1000 && std::isfinite(l.shadowBias) &&
           l.shadowBias >= 0 && l.shadowBias <= .05f;
}

inline int lightShadowResolution(const LightSettings& l) {
    return l.shadowQuality == 0 ? 0 : 128 * (1 << l.shadowQuality);
}
