// SDL3/Dear ImGui Vulkan integration for the editor.
#pragma once
#include "rendering/vulkan_context.hpp"
#include "rendering/vulkan_scene_surface.hpp"
#include "rendering/vulkan_texture.hpp"
#include <imgui.h>
#include <map>

struct SDL_Window;
union SDL_Event;

class VulkanImGui {
  public:
    // An ImGui context must already exist and outlive this object. The SDL
    // window and VulkanContext must also outlive it. Single-threaded use only.
    VulkanImGui(SDL_Window* window, VulkanContext& context);
    ~VulkanImGui();
    VulkanImGui(const VulkanImGui&) = delete;
    VulkanImGui& operator=(const VulkanImGui&) = delete;

    void processEvent(const SDL_Event& event);
    // Handles resize and Vulkan backend recreation before starting ImGui's
    // frame. Normally returns false while the main window is minimized or
    // zero-sized. When a secondary window is active, allowSecondary permits
    // UI construction with the existing backend even while the main swapchain
    // is unavailable; endFrame then skips main presentation.
    bool beginFrame(bool allowSecondary = false);
    // Discard a partially built frame without submitting its draw data.
    void cancelFrame();
    bool frameOpen() const {
        return frameOpen_ || drawReady_;
    }
    // Register a scene image after it has rendered, or an uploaded RGBA image.
    // A registration is replaced when its image generation changes. Call
    // forget() before destroying either image, and do not resize a surface
    // after calling texture() in the same frame.
    ImTextureID texture(const VulkanSceneSurface& surface);
    ImTextureID texture(const VulkanTexture& texture);
    void forget(const VulkanSceneSurface& surface);
    void forget(const VulkanTexture& texture);
    // Finalize draw data separately when callers need to modify it before
    // submission. endFrame() also finalizes automatically when needed.
    void renderFrame();
    VulkanContext::FrameResult endFrame();
    // Present an editor scene image in a second window sharing this context's
    // device. Call after endFrame() with a texture registered in that frame.
    // The secondary context must use a render-pass-compatible color format.
    VulkanContext::FrameResult presentSecondary(VulkanContext& secondary,
                                                ImTextureID image,
                                                const char* overlayText = nullptr);

  private:
    struct Image {
        uint64_t generation = 0;
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
    };
    ImTextureID registerImage(const void* key,
                              uint64_t generation,
                              VkSampler sampler,
                              VkImageView view,
                              VkImageLayout layout);
    void forgetImage(const void* key);
    void resetRenderer();
    void shutdownRenderer() noexcept;
    VulkanContext& context_;
    std::map<const void*, Image> images_;
    uint64_t swapchainGeneration_ = 0;
    bool platformReady_ = false, rendererReady_ = false, frameOpen_ = false;
    bool drawReady_ = false, secondaryUsed_ = false, mainAvailable_ = false;
};
