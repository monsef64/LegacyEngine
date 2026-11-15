#include "rendering/streamed_texture.hpp"
#include "materials/material.hpp"
#include <algorithm>
#include <fstream>
#include <memory>
#include <stb_image.h>
#include <stdexcept>

StreamedTexture decodeStreamedTexture(const AssetManager& assets, const std::string& path) {
    if (!materialTexturePath(path)) {
        throw std::runtime_error("Unsupported material texture");
    }
    const auto file = assets.resolve(path);
    const auto size = std::filesystem::file_size(file);
    const auto stamp = std::filesystem::last_write_time(file);
    if (!size || size > 32 * 1024 * 1024) {
        throw std::runtime_error("Material texture exceeds 32 MiB or is empty");
    }
    std::ifstream in(file, std::ios::binary);
    std::vector<unsigned char> encoded(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(size));
    if (!in || in.peek() != std::char_traits<char>::eof() ||
        std::filesystem::last_write_time(file) != stamp ||
        std::filesystem::file_size(file) != size) {
        throw std::runtime_error("Material texture changed during read");
    }
    int w = 0, h = 0, channels = 0;
    if (!stbi_info_from_memory(encoded.data(), int(size), &w, &h, &channels) || w <= 0 || h <= 0 ||
        w > 4096 || h > 4096) {
        throw std::runtime_error("Material texture requires at most 4096 x 4096 pixels");
    }
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> data(
        stbi_load_from_memory(encoded.data(), int(size), &w, &h, &channels, 4), stbi_image_free);
    if (!data) {
        throw std::runtime_error("Cannot decode material texture");
    }
    StreamedTexture result;
    result.width = uint32_t(w);
    result.height = uint32_t(h);
    result.stamp = stamp;
    result.size = size;
    result.pixels.resize(std::size_t(w) * h * 4);
    // File rows are top-down; mesh UVs follow the engine's bottom-up convention.
    for (int y = 0; y < h; ++y) {
        std::copy_n(data.get() + std::size_t(h - 1 - y) * w * 4,
                    std::size_t(w) * 4,
                    result.pixels.data() + std::size_t(y) * w * 4);
    }
    return result;
}
