#pragma once
#include "core/math.hpp"
#include <cctype>
#include <cmath>
#include <filesystem>
#include <string>
struct MaterialProperties {
    int shader = 0; // 0 Diffuse, 1 Diffuse Bumped, 2 Diffuse Transparent (texture alpha)
    Vec3 color{1, 1, 1};
    std::string texture, normalMap;
    float normalStrength = 1, tilingX = 1, tilingY = 1, specular = 0;
};
enum MaterialOverride : unsigned {
    MaterialColor = 1,
    MaterialTexture = 2,
    MaterialNormal = 4,
    MaterialShader = 8,
    MaterialStrength = 16,
    MaterialTiling = 32,
    MaterialSpecular = 64
};
inline bool materialPathValid(const std::string& path) {
    if (path.size() > 4096 || path.find('\0') != std::string::npos ||
        std::filesystem::path(path).is_absolute()) {
        return false;
    }
    for (const auto& part : std::filesystem::path(path)) {
        if (part == "..") {
            return false;
        }
    }
    return true;
}
inline bool materialTexturePath(const std::string& path) {
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
    return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga";
}
inline bool validMaterial(const MaterialProperties& p) {
    auto color = [](float x) { return std::isfinite(x) && x >= 0 && x <= 100; };
    return (p.shader >= 0 && p.shader <= 2) && color(p.color.x) && color(p.color.y) &&
           color(p.color.z) && materialTexturePath(p.texture) && materialTexturePath(p.normalMap) &&
           std::isfinite(p.normalStrength) && p.normalStrength >= 0 && p.normalStrength <= 4 &&
           std::isfinite(p.tilingX) && std::isfinite(p.tilingY) && std::abs(p.tilingX) >= .001f &&
           std::abs(p.tilingY) >= .001f && std::abs(p.tilingX) <= 1000 &&
           std::abs(p.tilingY) <= 1000 && std::isfinite(p.specular) && p.specular >= 0 &&
           p.specular <= 1;
}
inline MaterialProperties
applyMaterialOverrides(MaterialProperties p, const MaterialProperties& v, unsigned mask) {
    if (mask & MaterialColor) {
        p.color = v.color;
    }
    if (mask & MaterialTexture) {
        p.texture = v.texture;
    }
    if (mask & MaterialNormal) {
        p.normalMap = v.normalMap;
    }
    if (mask & MaterialShader) {
        p.shader = v.shader;
    }
    if (mask & MaterialStrength) {
        p.normalStrength = v.normalStrength;
    }
    if (mask & MaterialTiling) {
        p.tilingX = v.tilingX;
        p.tilingY = v.tilingY;
    }
    if (mask & MaterialSpecular) {
        p.specular = v.specular;
    }
    return p;
}
