#include "rendering/vulkan_material_textures.hpp"
#include "materials/material.hpp"
#include <algorithm>
#include <fstream>
#include <memory>
#include <stb_image.h>
#include <stdexcept>
#include <vector>

const VulkanTexture* VulkanMaterialTextures::get(const AssetManager& assets,
                                                 const std::string& path) {
    if (path.empty()) {
        return nullptr;
    }
    if (!materialTexturePath(path)) {
        throw std::runtime_error("Unsupported material texture");
    }
    const auto now = std::chrono::steady_clock::now();
    auto found = entries_.find(path);
    if (found != entries_.end()) {
        auto& e = found->second;
        if (e.used == frame_ || now - e.checked < std::chrono::seconds(1)) {
            e.used = frame_;
            return e.image.get();
        }
        e.checked = now;
        const auto file = assets.resolve(path);
        if (e.stamp == std::filesystem::last_write_time(file) &&
            e.size == std::filesystem::file_size(file)) {
            e.used = frame_;
            return e.image.get();
        }
        bytes_ -= e.bytes;
        entries_.erase(found);
    }
    const auto file = assets.resolve(path);
    const auto size = std::filesystem::file_size(file);
    if (size > 32 * 1024 * 1024) {
        throw std::runtime_error("Material texture exceeds 32 MiB");
    }
    std::ifstream in(file, std::ios::binary);
    std::vector<unsigned char> encoded(static_cast<std::size_t>(size));
    in.read(reinterpret_cast<char*>(encoded.data()), static_cast<std::streamsize>(size));
    if (!in || in.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("Cannot read material texture");
    }
    int w = 0, h = 0, channels = 0;
    if (!stbi_info_from_memory(encoded.data(), int(size), &w, &h, &channels) || w <= 0 || h <= 0 ||
        w > 4096 || h > 4096) {
        throw std::runtime_error("Material texture requires at most 4096 x 4096 pixels");
    }
    const std::size_t cost = std::size_t(w) * h * 4 * 4 / 3 + 16;
    while (bytes_ + cost > 128 * 1024 * 1024 || entries_.size() >= 128) {
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->second.used != frame_ &&
                (oldest == entries_.end() || it->second.used < oldest->second.used)) {
                oldest = it;
            }
        }
        if (oldest == entries_.end()) {
            throw std::runtime_error("Material texture cache exceeds 128 MiB/frame budget");
        }
        bytes_ -= oldest->second.bytes;
        entries_.erase(oldest);
    }
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> data(
        stbi_load_from_memory(encoded.data(), int(size), &w, &h, &channels, 4), stbi_image_free);
    if (!data) {
        throw std::runtime_error("Cannot decode material texture");
    }
    // File rows are top-down; mesh UVs follow the engine's bottom-up convention.
    std::vector<uint8_t> pixels(std::size_t(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        std::copy_n(data.get() + std::size_t(h - 1 - y) * w * 4,
                    std::size_t(w) * 4,
                    pixels.data() + std::size_t(y) * w * 4);
    }
    Entry entry;
    entry.bytes = cost;
    entry.used = frame_;
    entry.checked = now;
    entry.stamp = std::filesystem::last_write_time(file);
    entry.size = size;
    entry.image = std::make_unique<VulkanTexture>(surface_.physicalDevice(),
                                                  surface_.device(),
                                                  surface_.graphicsQueue(),
                                                  surface_.graphicsFamily(),
                                                  uint32_t(w),
                                                  uint32_t(h),
                                                  pixels,
                                                  VulkanTexture::Address::Repeat,
                                                  true);
    auto result = entries_.emplace(path, std::move(entry)).first;
    bytes_ += cost;
    return result->second.image.get();
}
