#include "rendering/vulkan_skybox_cache.hpp"
#include "assets/cubemap.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <stb_image.h>
#include <stdexcept>
#include <vector>

namespace {
std::vector<unsigned char> readSkyImage(const AssetManager& assets, const std::string& path) {
    if (path.empty()) {
        throw std::runtime_error("Assign all six skybox faces, or an HDR panorama");
    }
    const auto file = assets.resolve(path);
    const auto bytes = std::filesystem::file_size(file);
    if (bytes > 64 * 1024 * 1024) {
        throw std::runtime_error("Skybox image exceeds 64 MiB");
    }
    std::ifstream in(file, std::ios::binary);
    std::vector<unsigned char> data(static_cast<std::size_t>(bytes));
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(bytes));
    if (!in || in.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("Cannot read skybox image");
    }
    return data;
}
} // namespace
const VulkanTexture*
VulkanSkyboxCache::get(unsigned slot, const AssetManager& assets, const Skybox& sky) {
    if (slot >= entries_.size() || !validSkybox(sky)) {
        throw std::runtime_error("Invalid skybox properties or cache slot");
    }
    std::vector<std::string> paths =
        sky.type == 1 ? std::vector<std::string>{sky.hdr}
                      : std::vector<std::string>{
                            sky.right, sky.left, sky.top, sky.bottom, sky.back, sky.front};
    if (sky.type == 0 && !sky.cubemap.empty()) {
        auto cube = loadCubemap(assets, sky.cubemap);
        paths = {cube.faces[0],
                 cube.faces[1],
                 cube.faces[2],
                 cube.faces[3],
                 cube.faces[5],
                 cube.faces[4]};
    }
    auto& entry = entries_[slot];
    if (std::all_of(
            paths.begin(), paths.end(), [](const std::string& path) { return path.empty(); })) {
        entry = {};
        return nullptr;
    }
    std::string key = std::to_string(sky.type);
    for (const auto& path : paths) {
        key += "|" + std::to_string(path.size()) + ":" + path;
    }
    const auto now = std::chrono::steady_clock::now();
    if (entry.image && entry.root == assets.root && entry.key == key &&
        now - entry.checked < std::chrono::seconds(1)) {
        return entry.image.get();
    }
    std::string signature = key;
    for (const auto& path : paths) {
        const auto file = assets.resolve(path);
        signature +=
            "|" +
            std::to_string(std::filesystem::last_write_time(file).time_since_epoch().count()) +
            ":" + std::to_string(std::filesystem::file_size(file));
    }
    entry.checked = now;
    if (entry.image && entry.root == assets.root && entry.signature == signature) {
        return entry.image.get();
    }
    // Release the previous image before loading the replacement, preventing
    // repeated failed loads from retaining a stale or oversized environment.
    entry = {};
    if (sky.type == 1) {
        auto encoded = readSkyImage(assets, paths[0]);
        int w = 0, h = 0, c = 0;
        if (!stbi_info_from_memory(encoded.data(), int(encoded.size()), &w, &h, &c) || w <= 0 ||
            h <= 0 || w > 4096 || h > 2048 ||
            !stbi_is_hdr_from_memory(encoded.data(), int(encoded.size()))) {
            throw std::runtime_error("HDR panorama must be Radiance .hdr, at most 4096 x 2048");
        }
        std::unique_ptr<float, decltype(&stbi_image_free)> pixels(
            stbi_loadf_from_memory(encoded.data(), int(encoded.size()), &w, &h, &c, 4),
            stbi_image_free);
        if (!pixels) {
            throw std::runtime_error("Cannot decode HDR panorama");
        }
        std::vector<float> data(std::size_t(w) * h * 4);
        for (std::size_t i = 0; i < data.size(); ++i) {
            data[i] = !std::isfinite(pixels.get()[i]) || pixels.get()[i] < 0
                          ? 0
                          : std::min(pixels.get()[i], 65504.f);
        }
        entry.image = std::make_unique<VulkanTexture>(surface_.physicalDevice(),
                                                      surface_.device(),
                                                      surface_.graphicsQueue(),
                                                      surface_.graphicsFamily(),
                                                      uint32_t(w),
                                                      uint32_t(h),
                                                      data,
                                                      VulkanTexture::Address::Panorama);
    } else {
        int faceSize = 0;
        std::vector<uint8_t> faces;
        for (const auto& path : paths) {
            auto encoded = readSkyImage(assets, path);
            int w = 0, h = 0, c = 0;
            if (!stbi_info_from_memory(encoded.data(), int(encoded.size()), &w, &h, &c) || w <= 0 ||
                w != h || w > 2048 || (faceSize && w != faceSize)) {
                throw std::runtime_error(
                    "Skybox faces must be equal square images, at most 2048 x 2048");
            }
            faceSize = w;
            std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
                stbi_load_from_memory(encoded.data(), int(encoded.size()), &w, &h, &c, 4),
                stbi_image_free);
            if (!pixels) {
                throw std::runtime_error("Cannot decode skybox face");
            }
            faces.insert(faces.end(), pixels.get(), pixels.get() + std::size_t(w) * h * 4);
        }
        entry.image = std::make_unique<VulkanTexture>(surface_.physicalDevice(),
                                                      surface_.device(),
                                                      surface_.graphicsQueue(),
                                                      surface_.graphicsFamily(),
                                                      uint32_t(faceSize),
                                                      uint32_t(faceSize),
                                                      faces,
                                                      VulkanTexture::Address::Clamp,
                                                      false,
                                                      false,
                                                      true);
    }
    entry.root = assets.root;
    entry.key = std::move(key);
    entry.signature = std::move(signature);
    entry.checked = now;
    return entry.image.get();
}
