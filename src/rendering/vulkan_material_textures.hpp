#pragma once
#include "assets/background_worker.hpp"
#include "rendering/streamed_texture.hpp"
#include "rendering/vulkan_scene_surface.hpp"
#include <map>
#include <memory>

// CPU I/O and image decoding happen off-thread; Vulkan uploads and cache access
// remain on the render thread. Pending textures render with the normal fallback.
// Texture pointers survive until the next beginFrame or clear(). Device, queue
// and the supplied surface must outlive the cache.
class VulkanMaterialTextures {
  public:
    explicit VulkanMaterialTextures(VulkanSceneSurface& surface) : surface_(surface) {}
    VulkanMaterialTextures(const VulkanMaterialTextures&) = delete;
    VulkanMaterialTextures& operator=(const VulkanMaterialTextures&) = delete;
    void beginFrame();
    void clear();
    bool pending() const;
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
    struct Pending {
        std::future<StreamedTexture> future;
        std::chrono::steady_clock::time_point used;
    };
    struct Failure {
        std::string message;
        std::chrono::steady_clock::time_point retry;
    };
    VulkanSceneSurface& surface_;
    std::map<std::string, Entry> entries_;
    std::map<std::string, Pending> pending_;
    std::map<std::string, Failure> failures_;
    std::filesystem::path root_;
    std::size_t bytes_ = 0;
    uint64_t frame_ = 0;
    bool deferred_ = false;
    BackgroundWorker worker_;
};
