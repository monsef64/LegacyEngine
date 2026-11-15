#pragma once
#include "assets/vulkan_thumbnail_cache.hpp"
#include "editor/asset_thumbnail_source.hpp"
#include <stdexcept>

// Adapts the editor browser callbacks to Vulkan's descriptor lifetime rules.
// The source, ImGui backend and context must outlive this adapter. Construct
// outside a UI frame and call prepare() before VulkanImGui::beginFrame().
class VulkanBrowserThumbnails {
  public:
    VulkanBrowserThumbnails(AssetThumbnailSource& source, VulkanContext& context, VulkanImGui& gui)
        : source_(source), gui_(gui), cache_(context, gui) {
        if (source_.beginFrame || source_.texture || source_.clear) {
            throw std::runtime_error("Asset thumbnail provider is already installed");
        }
        AssetThumbnailSource callbacks;
        callbacks.beginFrame = [](const AssetManager&) {};
        callbacks.texture = [this](const AssetManager& assets, const std::string& path) {
            if (!prepared_ || pendingClear_ || assets.root != preparedRoot_) {
                return ImTextureID_Invalid;
            }
            return cache_.texture(assets, path);
        };
        // Browser operations can invalidate assets after Image commands were
        // recorded. Defer destruction until the next frame, before NewFrame.
        callbacks.clear = [this] { pendingClear_ = true; };
        source_ = std::move(callbacks);
    }
    ~VulkanBrowserThumbnails() {
        source_ = {};
    }
    VulkanBrowserThumbnails(const VulkanBrowserThumbnails&) = delete;
    VulkanBrowserThumbnails& operator=(const VulkanBrowserThumbnails&) = delete;
    void prepare(const AssetManager& assets) {
        if (gui_.frameOpen()) {
            throw std::runtime_error("Prepare browser thumbnails before the ImGui frame");
        }
        prepared_ = false;
        if (pendingClear_) {
            cache_.clear();
            pendingClear_ = false;
        }
        cache_.beginFrame(assets);
        preparedRoot_ = assets.root;
        prepared_ = true;
    }
    const std::string& error() const {
        return cache_.error();
    }

  private:
    AssetThumbnailSource& source_;
    VulkanImGui& gui_;
    VulkanThumbnailCache cache_;
    std::filesystem::path preparedRoot_;
    bool pendingClear_ = false, prepared_ = false;
};
