#pragma once
#include "assets/assets.hpp"
#include <cstdint>

// CPU-only decoded texture, safe to create on a background worker.
struct StreamedTexture {
    uint32_t width = 0, height = 0;
    std::vector<uint8_t> pixels;
    std::filesystem::file_time_type stamp{};
    std::uintmax_t size = 0;
};
StreamedTexture decodeStreamedTexture(const AssetManager& assets, const std::string& path);
