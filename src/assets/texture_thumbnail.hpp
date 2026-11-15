#pragma once
#include "assets/assets.hpp"
#include <array>
#include <chrono>

struct TextureThumbnail {
    static constexpr int size = 128;
    std::array<unsigned char, size * size * 4> pixels{};
};
bool textureAsset(const std::string& path);
// Stable per-asset disk entry; metadata in the header invalidates stale content.
std::filesystem::path thumbnailCachePath(const AssetManager& assets, const std::string& path);
TextureThumbnail loadTextureThumbnail(const AssetManager& assets,
                                      const std::string& path,
                                      bool* cacheHit = nullptr,
                                      bool allowDecode = true);
