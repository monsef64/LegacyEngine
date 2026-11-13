#pragma once
#include "assets/assets.hpp"
#include <vector>
inline bool audioAsset(const std::filesystem::path& path) {
    const auto ext = AssetManager::extension(path);
    return ext == ".wav" || ext == ".mp3";
}
std::vector<float> decodeAudioClip(const AssetManager& assets, const std::string& path);
