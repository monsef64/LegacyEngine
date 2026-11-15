#include "assets/vulkan_thumbnail_cache.hpp"
#include "assets/material_thumbnail.hpp"
#include "materials/material_io.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <stdexcept>
#include <vector>

VulkanThumbnailCache::~VulkanThumbnailCache() {
    // Avoid leaving pending ImGui draw commands referring to destroyed images.
    // Normal callers clear the cache before starting UI construction.
    if (gui_.frameOpen()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER,
                    "Cancelling ImGui frame while destroying Vulkan thumbnail cache");
        gui_.cancelFrame();
    }
    try {
        clear();
    } catch (...) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Could not clear Vulkan thumbnail cache");
    }
}
void VulkanThumbnailCache::erase(std::map<std::string, Entry>::iterator entry) {
    gui_.forget(*entry->second.image);
    entries_.erase(entry);
}
void VulkanThumbnailCache::clear() {
    if (gui_.frameOpen()) {
        throw std::runtime_error("Cannot clear thumbnails during an ImGui frame");
    }
    while (!entries_.empty()) {
        erase(entries_.begin());
    }
    root_.clear();
    error_.clear();
}
void VulkanThumbnailCache::invalidate(const std::string& path) {
    if (gui_.frameOpen()) {
        throw std::runtime_error("Cannot invalidate thumbnails during an ImGui frame");
    }
    const auto it = entries_.find(path);
    if (it != entries_.end()) {
        erase(it);
    }
}
void VulkanThumbnailCache::beginFrame(const AssetManager& assets) {
    if (gui_.frameOpen()) {
        throw std::runtime_error("Prepare thumbnails before starting an ImGui frame");
    }
    if (root_ != assets.root) {
        clear();
        root_ = assets.root;
    }
    ++frame_;
    uploads_ = 4;
    decodes_ = 1;
    error_.clear();
    const auto now = std::chrono::steady_clock::now();
    for (auto it = entries_.begin(); it != entries_.end();) {
        auto& entry = it->second;
        if (now - entry.checked < std::chrono::seconds(1)) {
            ++it;
            continue;
        }
        entry.checked = now;
        bool changed = false;
        try {
            const auto file = assets.resolve(it->first);
            changed = entry.modified != std::filesystem::last_write_time(file) ||
                      entry.bytes != std::filesystem::file_size(file);
            if (!entry.dependency.empty()) {
                const auto dependency = assets.resolve(entry.dependency);
                const bool exists = std::filesystem::is_regular_file(dependency);
                changed |=
                    exists != entry.dependencyExists ||
                    (exists &&
                     (entry.dependencyModified != std::filesystem::last_write_time(dependency) ||
                      entry.dependencyBytes != std::filesystem::file_size(dependency)));
            }
        } catch (...) {
            changed = true;
        }
        if (changed) {
            auto old = it++;
            erase(old);
        } else {
            ++it;
        }
    }
    // Make room for new previews without evicting images used last frame.
    while (entries_.size() > 252) {
        auto oldest = entries_.end();
        for (auto it = entries_.begin(); it != entries_.end(); ++it) {
            if (it->second.touched + 1 < frame_ &&
                (oldest == entries_.end() || it->second.touched < oldest->second.touched)) {
                oldest = it;
            }
        }
        if (oldest == entries_.end()) {
            break;
        }
        erase(oldest);
    }
}
ImTextureID VulkanThumbnailCache::texture(const AssetManager& assets, const std::string& path) {
    if (!gui_.frameOpen()) {
        throw std::runtime_error("Thumbnail requested outside an ImGui frame");
    }
    if (assets.root != root_) {
        throw std::runtime_error("Call beginFrame after changing the asset root");
    }
    if (!textureAsset(path) && !materialAsset(path)) {
        return ImTextureID_Invalid;
    }
    auto found = entries_.find(path);
    if (found != entries_.end()) {
        found->second.touched = frame_;
        return gui_.texture(*found->second.image);
    }
    if (uploads_ <= 0 || entries_.size() >= 256 || (materialAsset(path) && decodes_ <= 0)) {
        return ImTextureID_Invalid;
    }
    --uploads_;
    try {
        const auto file = assets.resolve(path);
        Entry entry;
        entry.modified = std::filesystem::last_write_time(file);
        entry.bytes = std::filesystem::file_size(file);
        bool cacheHit = false;
        TextureThumbnail thumb;
        if (materialAsset(path)) {
            const auto material = loadMaterial(assets, path);
            entry.dependency = material.texture;
            if (!entry.dependency.empty()) {
                const auto dependency = assets.resolve(entry.dependency);
                entry.dependencyExists = std::filesystem::is_regular_file(dependency);
                if (entry.dependencyExists) {
                    entry.dependencyModified = std::filesystem::last_write_time(dependency);
                    entry.dependencyBytes = std::filesystem::file_size(dependency);
                }
            }
            thumb = materialThumbnail(assets, material);
        } else {
            thumb = loadTextureThumbnail(assets, path, &cacheHit, decodes_ > 0);
        }
        if (!cacheHit) {
            --decodes_;
        }
        entry.image = std::make_unique<VulkanTexture>(
            context_.physicalDevice(),
            context_.device(),
            context_.graphicsQueue(),
            context_.graphicsFamily(),
            TextureThumbnail::size,
            TextureThumbnail::size,
            std::vector<uint8_t>(thumb.pixels.begin(), thumb.pixels.end()));
        entry.touched = frame_;
        entry.checked = std::chrono::steady_clock::now();
        found = entries_.emplace(path, std::move(entry)).first;
        return gui_.texture(*found->second.image);
    } catch (const std::exception& e) {
        // A cached miss after consuming the decode budget is a normal defer.
        if (decodes_ <= 0 && std::string(e.what()) == "Thumbnail is not cached") {
            return ImTextureID_Invalid;
        }
        error_ = path + ": " + e.what();
        return ImTextureID_Invalid;
    }
}
