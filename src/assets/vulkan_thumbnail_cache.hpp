// Graphics-API-independent thumbnail decoding with Vulkan image/ImGui storage.
#pragma once
#include "assets/texture_thumbnail.hpp"
#include "rendering/vulkan_imgui.hpp"
#include <chrono>
#include <map>
#include <memory>

class VulkanThumbnailCache {
  public:
    // The context and ImGui backend must outlive the cache. Destroy/clear it
    // outside an ImGui frame, before destroying the backend or device.
    VulkanThumbnailCache(VulkanContext& context, VulkanImGui& gui) : context_(context), gui_(gui) {}
    ~VulkanThumbnailCache();
    VulkanThumbnailCache(const VulkanThumbnailCache&) = delete;
    VulkanThumbnailCache& operator=(const VulkanThumbnailCache&) = delete;

    // Call before VulkanImGui::beginFrame. Performs invalidation and eviction;
    // no descriptor or GPU image is destroyed during UI construction.
    void beginFrame(const AssetManager& assets);
    // Call inside an ImGui frame. Returns ImTextureID_Invalid when not ready,
    // when the per-frame budget is exhausted, or on failure. Supports image
    // and material preview thumbnails.
    ImTextureID texture(const AssetManager& assets, const std::string& path);
    void invalidate(const std::string& path);
    void clear();
    const std::string& error() const {
        return error_;
    }

  private:
    struct Entry {
        std::unique_ptr<VulkanTexture> image;
        std::filesystem::file_time_type modified{}, dependencyModified{};
        std::uintmax_t bytes = 0, dependencyBytes = 0;
        std::string dependency;
        bool dependencyExists = false;
        uint64_t touched = 0;
        std::chrono::steady_clock::time_point checked{};
    };
    void erase(std::map<std::string, Entry>::iterator entry);
    VulkanContext& context_;
    VulkanImGui& gui_;
    std::filesystem::path root_;
    std::map<std::string, Entry> entries_;
    uint64_t frame_ = 0;
    int uploads_ = 0, decodes_ = 0;
    std::string error_;
};
