#pragma once
#include "editor/preview_backend.hpp"
#include "rendering/vulkan_imgui.hpp"
#include "rendering/vulkan_scene_renderer.hpp"
#include <array>
#include <memory>
#include <stdexcept>
#include <vector>

// Model and animation preview rendering on the editor's Vulkan device. The
// owner, renderer and ImGui backend must outlive this object. Destroy outside
// a UI frame. Each preview slot is displayed at most once in a frame.
class VulkanEditorPreviewBackend final : public EditorPreviewBackend {
  public:
    struct Shaders {
        std::vector<uint32_t> unlitVertex, unlitFragment, litVertex, litFragment, skyVertex,
            skyFragment;
    };
    VulkanEditorPreviewBackend(VulkanSceneSurface& owner,
                               VulkanSceneRenderer& renderer,
                               VulkanImGui& gui,
                               Shaders shaders)
        : owner_(owner), renderer_(renderer), gui_(gui), shaders_(std::move(shaders)) {}
    ~VulkanEditorPreviewBackend() override {
        // Never destroy a descriptor referenced by pending draw commands.
        if (gui_.frameOpen()) {
            gui_.cancelFrame();
        }
        for (auto& surface : surfaces_) {
            if (surface) {
                try {
                    gui_.forget(*surface);
                } catch (...) { /* device is being destroyed */
                }
            }
        }
    }
    VulkanEditorPreviewBackend(const VulkanEditorPreviewBackend&) = delete;
    VulkanEditorPreviewBackend& operator=(const VulkanEditorPreviewBackend&) = delete;
    bool meshBounds(MeshHandle mesh, Vec3& low, Vec3& high) const override {
        return renderer_.meshBounds(mesh, low, high);
    }
    bool modelBounds(const AssetManager& assets,
                     const std::string& path,
                     Vec3& low,
                     Vec3& high) override {
        return renderer_.modelBounds(assets, path, low, high);
    }
    Image image(Slot slot,
                World& world,
                const AssetManager& assets,
                const Mat4& vp,
                Vec3 eye,
                int width,
                int height,
                bool redraw) override {
        const auto index = static_cast<unsigned>(slot);
        if (index >= surfaces_.size() || width <= 0 || height <= 0) {
            throw std::runtime_error("Invalid Vulkan editor preview");
        }
        auto& surface = surfaces_[index];
        if (!surface) {
            auto created = std::make_unique<VulkanSceneSurface>(owner_.physicalDevice(),
                                                                owner_.device(),
                                                                owner_.graphicsQueue(),
                                                                owner_.graphicsFamily(),
                                                                shaders_.unlitVertex,
                                                                shaders_.unlitFragment,
                                                                &owner_);
            created->enableLighting(shaders_.litVertex, shaders_.litFragment);
            created->enableSkybox(shaders_.skyVertex, shaders_.skyFragment);
            surface = std::move(created);
            redraw = true;
        }
        if (redraw || surface->width() != uint32_t(width) ||
            surface->height() != uint32_t(height)) {
            renderer_.setAssetRoot(assets.root);
            renderer_.drawLitTo(*surface, world, vp, uint32_t(width), uint32_t(height), eye);
        }
        return {gui_.texture(*surface), false};
    }
    VulkanSceneSurface* surface(Slot slot) {
        const auto index = static_cast<unsigned>(slot);
        return index < surfaces_.size() ? surfaces_[index].get() : nullptr;
    }

  private:
    VulkanSceneSurface& owner_;
    VulkanSceneRenderer& renderer_;
    VulkanImGui& gui_;
    Shaders shaders_;
    std::array<std::unique_ptr<VulkanSceneSurface>, 2> surfaces_;
};
