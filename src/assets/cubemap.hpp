#pragma once
#include "assets/assets.hpp"
#include "materials/material.hpp"
#include "project/json_io.hpp"
#include <array>
inline bool cubemapAsset(const std::string& path) {
    return materialPathValid(path) && AssetManager::extension(path) == ".cubemap";
}
struct CubemapAsset {
    inline static constexpr const char* names[]{"right", "left", "top", "bottom", "front", "back"};
    std::array<std::string, 6> faces;
};
inline CubemapAsset loadCubemap(const AssetManager& assets, const std::string& path) {
    if (!cubemapAsset(path)) {
        throw std::runtime_error("Expected .cubemap asset");
    }
    auto json = readJsonFile(assets.resolve(path), 64 * 1024);
    if (json.at("format") != "LegacyEngineCubemap" || json.at("version") != 1) {
        throw std::runtime_error("Invalid cubemap asset");
    }
    CubemapAsset result;
    for (int i = 0; i < 6; ++i) {
        result.faces[i] = json.at("faces").at(CubemapAsset::names[i]).get<std::string>();
        if (!materialTexturePath(result.faces[i])) {
            throw std::runtime_error("Invalid cubemap face texture");
        }
    }
    return result;
}
inline void
saveCubemap(const AssetManager& assets, const std::string& path, const CubemapAsset& cubemap) {
    if (!cubemapAsset(path)) {
        throw std::runtime_error("Expected .cubemap asset");
    }
    Json faces = Json::object();
    for (int i = 0; i < 6; ++i) {
        if (!materialTexturePath(cubemap.faces[i])) {
            throw std::runtime_error("Invalid cubemap face");
        }
        faces[CubemapAsset::names[i]] = cubemap.faces[i];
    }
    writeJsonFile(assets.resolve(path),
                  Json{{"format", "LegacyEngineCubemap"}, {"version", 1}, {"faces", faces}});
}
