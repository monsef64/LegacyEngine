#pragma once
#include "assets/assets.hpp"
#include "rendering/skybox.hpp"
#include "rendering/vulkan_scene_surface.hpp"
#include <array>
#include <memory>

// Two independently owned slots support sky transitions. The surface and its
// Vulkan device must outlive this cache; pointers survive until that slot changes.
class VulkanSkyboxCache {
  public:
    explicit VulkanSkyboxCache(VulkanSceneSurface& surface) : surface_(surface) {}
    VulkanSkyboxCache(const VulkanSkyboxCache&) = delete;
    VulkanSkyboxCache& operator=(const VulkanSkyboxCache&) = delete;
    const VulkanTexture* get(unsigned slot, const AssetManager& assets, const Skybox& sky);
    void clear() {
        entries_ = {};
    }

  private:
    struct Entry {
        std::unique_ptr<VulkanTexture> image;
        std::string key, signature;
        std::filesystem::path root;
        std::chrono::steady_clock::time_point checked{};
    };
    VulkanSceneSurface& surface_;
    std::array<Entry, 2> entries_;
};
