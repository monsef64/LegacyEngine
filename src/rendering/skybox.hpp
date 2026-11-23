#pragma once
#include "materials/material.hpp"
#include <array>
struct Skybox {
    int type = 0; // 0 six textures, 1 HDR equirectangular panorama
    Vec3 color{1, 1, 1};
    bool enabled = true;
    bool affectEnvironment = false;
    bool hdrCompression = true; // Packed RGB9_E5 GPU storage; original HDR asset stays unchanged.
    float environmentIntensity = 1;
    std::string right, left, top, bottom, front, back, hdr; // Legacy faces remain readable.
    std::string cubemap;
};
inline bool skyboxHDRPath(const std::string& path) {
    if (!materialPathValid(path)) {
        return false;
    }
    if (path.empty()) {
        return true;
    }
    auto ext = std::filesystem::path(path).extension().string();
    for (auto& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext == ".hdr";
}
inline bool validSkybox(const Skybox& s) {
    auto color = [](float v) { return std::isfinite(v) && v >= 0 && v <= 100; };
    if (!std::isfinite(s.environmentIntensity) || s.environmentIntensity < 0 ||
        s.environmentIntensity > 5) {
        return false;
    }
    if (s.type < 0 || s.type > 1 || !color(s.color.x) || !color(s.color.y) || !color(s.color.z) ||
        !skyboxHDRPath(s.hdr) ||
        (!s.cubemap.empty() && (!materialPathValid(s.cubemap) ||
                                std::filesystem::path(s.cubemap).extension() != ".cubemap"))) {
        return false;
    }
    for (const auto* path : {&s.right, &s.left, &s.top, &s.bottom, &s.front, &s.back}) {
        if (!materialTexturePath(*path)) {
            return false;
        }
    }
    return true;
}
