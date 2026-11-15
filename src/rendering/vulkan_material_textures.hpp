#pragma once
#include "assets/assets.hpp"
#include "rendering/vulkan_scene_surface.hpp"
#include <map>
#include <memory>

// Vulkan material image cache; get() is synchronous and may evict entries not
// used in the current frame. Texture pointers survive until the next beginFrame
// or clear(). Device, queue and the supplied surface must outlive the cache.
class VulkanMaterialTextures {
  public:
    explicit VulkanMaterialTextures(VulkanSceneSurface& surface) : surface_(surface) {}
    VulkanMaterialTextures(const VulkanMaterialTextures&) = delete;
    VulkanMaterialTextures& operator=(const VulkanMaterialTextures&) = delete;
    void beginFrame() {
        ++frame_;
    }
    void clear() {
        entries_.clear();
        bytes_ = 0;
    }
    const VulkanTexture* get(const AssetManager& assets, const std::string& path);

  private:
    struct Entry {
        std::unique_ptr<VulkanTexture> image;
        std::size_t bytes = 0;
        uint64_t used = 0;
        std::filesystem::file_time_type stamp{};
        std::uintmax_t size = 0;
        std::chrono::steady_clock::time_point checked{};
    };
    VulkanSceneSurface& surface_;
    std::map<std::string, Entry> entries_;
    std::size_t bytes_ = 0;
    uint64_t frame_ = 0;
};
