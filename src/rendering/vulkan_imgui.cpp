#include "rendering/vulkan_imgui.hpp"
#include <SDL3/SDL.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {
void check(VkResult result) {
    if (result < 0) {
        throw std::runtime_error("ImGui Vulkan error " + std::to_string(static_cast<int>(result)));
    }
}
template <class Handle> ImTextureID textureId(Handle handle) {
    if constexpr (std::is_pointer<Handle>::value) {
        return static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(handle));
    } else {
        return static_cast<ImTextureID>(handle);
    }
}
} // namespace
VulkanImGui::VulkanImGui(SDL_Window* window, VulkanContext& context) : context_(context) {
    if (!window || !ImGui::GetCurrentContext()) {
        throw std::runtime_error("Vulkan ImGui requires a window and ImGui context");
    }
    if (!ImGui_ImplSDL3_InitForVulkan(window)) {
        throw std::runtime_error("Could not initialize SDL3 ImGui backend");
    }
    platformReady_ = true;
}
VulkanImGui::~VulkanImGui() {
    cancelFrame();
    shutdownRenderer();
    if (platformReady_) {
        ImGui_ImplSDL3_Shutdown();
    }
}
void VulkanImGui::shutdownRenderer() noexcept {
    if (!rendererReady_) {
        return;
    }
    vkDeviceWaitIdle(context_.device());
    for (const auto& pair : images_) {
        try {
            ImGui_ImplVulkan_RemoveTexture(pair.second.descriptor);
        } catch (...) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Could not release an ImGui Vulkan texture");
        }
    }
    images_.clear();
    try {
        ImGui_ImplVulkan_Shutdown();
    } catch (...) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Could not shut down ImGui Vulkan backend");
    }
    rendererReady_ = false;
}
void VulkanImGui::resetRenderer() {
    shutdownRenderer();
    if (context_.imageCount() < 2) {
        throw std::runtime_error("ImGui Vulkan requires at least two swapchain images");
    }
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_0;
    info.Instance = context_.instance();
    info.PhysicalDevice = context_.physicalDevice();
    info.Device = context_.device();
    info.QueueFamily = context_.graphicsFamily();
    info.Queue = context_.graphicsQueue();
    info.DescriptorPoolSize = 1024;
    info.MinImageCount = 2;
    info.ImageCount = context_.imageCount();
    info.PipelineInfoMain.RenderPass = context_.renderPass();
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.CheckVkResultFn = check;
    try {
        if (!ImGui_ImplVulkan_Init(&info)) {
            throw std::runtime_error("Could not initialize ImGui Vulkan backend");
        }
    } catch (...) {
        // The upstream backend stores its state before creating device objects.
        // Clean up any partially created state before propagating the failure.
        if (ImGui::GetIO().BackendRendererUserData) {
            try {
                ImGui_ImplVulkan_Shutdown();
            } catch (...) {
            }
        }
        throw;
    }
    rendererReady_ = true;
    swapchainGeneration_ = context_.swapchainGeneration();
}
void VulkanImGui::processEvent(const SDL_Event& event) {
    ImGui_ImplSDL3_ProcessEvent(&event);
}
bool VulkanImGui::beginFrame(bool allowSecondary) {
    if (frameOpen_ || drawReady_) {
        throw std::runtime_error("Vulkan ImGui frame is already open or awaiting submission");
    }
    mainAvailable_ = context_.prepare();
    if (!mainAvailable_ && (!allowSecondary || !rendererReady_)) {
        return false;
    }
    if (mainAvailable_ &&
        (!rendererReady_ || swapchainGeneration_ != context_.swapchainGeneration())) {
        resetRenderer();
    }
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    frameOpen_ = true;
    return true;
}
void VulkanImGui::cancelFrame() {
    if (frameOpen_) {
        ImGui::EndFrame();
        frameOpen_ = false;
    }
    drawReady_ = false;
}
ImTextureID VulkanImGui::texture(const VulkanSceneSurface& surface) {
    return registerImage(&surface,
                         surface.imageGeneration(),
                         surface.sampler(),
                         surface.imageView(),
                         surface.imageLayout());
}
ImTextureID VulkanImGui::texture(const VulkanTexture& texture) {
    return registerImage(
        &texture, 1, texture.sampler(), texture.imageView(), texture.imageLayout());
}
ImTextureID VulkanImGui::registerImage(const void* key,
                                       uint64_t generation,
                                       VkSampler sampler,
                                       VkImageView view,
                                       VkImageLayout layout) {
    if (!frameOpen_ || !rendererReady_ || !view || !sampler) {
        throw std::runtime_error("Vulkan image is not ready for ImGui");
    }
    auto it = images_.find(key);
    if (it != images_.end() && it->second.generation != generation) {
        // SceneSurface::resize waits for all device work before replacing the image.
        ImGui_ImplVulkan_RemoveTexture(it->second.descriptor);
        images_.erase(it);
        it = images_.end();
    }
    if (it == images_.end()) {
        Image image{generation, ImGui_ImplVulkan_AddTexture(sampler, view, layout)};
        if (!image.descriptor) {
            throw std::runtime_error("Could not register Vulkan image");
        }
        try {
            it = images_.emplace(key, image).first;
        } catch (...) {
            ImGui_ImplVulkan_RemoveTexture(image.descriptor);
            throw;
        }
    }
    return textureId(it->second.descriptor);
}
void VulkanImGui::forget(const VulkanSceneSurface& surface) {
    forgetImage(&surface);
}
void VulkanImGui::forget(const VulkanTexture& texture) {
    forgetImage(&texture);
}
void VulkanImGui::forgetImage(const void* key) {
    auto it = images_.find(key);
    if (it == images_.end()) {
        return;
    }
    if (frameOpen_ || drawReady_) {
        throw std::runtime_error("Cannot forget an ImGui image during a frame");
    }
    check(vkDeviceWaitIdle(context_.device()));
    ImGui_ImplVulkan_RemoveTexture(it->second.descriptor);
    images_.erase(it);
}
void VulkanImGui::renderFrame() {
    if (!frameOpen_) {
        throw std::runtime_error("No Vulkan ImGui frame is open");
    }
    ImGui::Render();
    frameOpen_ = false;
    drawReady_ = true;
}
VulkanContext::FrameResult VulkanImGui::endFrame() {
    if (frameOpen_) {
        renderFrame();
    }
    if (!drawReady_) {
        throw std::runtime_error("No Vulkan ImGui frame is ready");
    }
    drawReady_ = false;
    // A resize or VSync change requested during UI construction must be
    // applied before building the next frame, not behind this pipeline's back.
    if (!mainAvailable_ || context_.needsRecreation() ||
        context_.swapchainGeneration() != swapchainGeneration_) {
        return VulkanContext::FrameResult::Skipped;
    }
    // The ImGui backend rotates a single set of upload buffers across calls,
    // not across windows. Serialize once a second window participates so a
    // buffer cannot be overwritten while either window is using it.
    if (secondaryUsed_) {
        check(vkDeviceWaitIdle(context_.device()));
    }
    return context_.present(
        [](VkCommandBuffer cmd) { ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd); },
        .07f,
        .08f,
        .10f);
}

VulkanContext::FrameResult VulkanImGui::presentSecondary(VulkanContext& secondary,
                                                         ImTextureID image,
                                                         const char* overlayText) {
    if (frameOpen_ || drawReady_ || !rendererReady_ || !ImGui::GetDrawData()) {
        throw std::runtime_error("Finish the Vulkan ImGui frame before secondary presentation");
    }
    // A pending main-window recreation does not invalidate the old render
    // pass until prepare() actually recreates it. This lets a game window
    // keep presenting while the editor window is minimized.
    if (context_.swapchainGeneration() != swapchainGeneration_) {
        return VulkanContext::FrameResult::Skipped;
    }
    if (secondary.device() != context_.device() ||
        secondary.graphicsQueue() != context_.graphicsQueue() ||
        secondary.graphicsFamily() != context_.graphicsFamily() || &secondary == &context_) {
        throw std::runtime_error("Secondary Vulkan window must share the editor device and queue");
    }
    bool registered = false;
    for (const auto& pair : images_) {
        if (textureId(pair.second.descriptor) == image) {
            registered = true;
            break;
        }
    }
    if (!registered) {
        throw std::runtime_error("Secondary Vulkan window image is not registered");
    }
    if (!secondary.prepare()) {
        return VulkanContext::FrameResult::Skipped;
    }
    if (secondary.colorFormat() != context_.colorFormat()) {
        throw std::runtime_error("Secondary Vulkan window has an incompatible swapchain format");
    }
    secondaryUsed_ = true;
    check(vkDeviceWaitIdle(context_.device()));
    const auto extent = secondary.extent();
    ImDrawList list(ImGui::GetDrawListSharedData());
    list._ResetForNewFrame();
    list.PushClipRect(ImVec2(0, 0), ImVec2(float(extent.width), float(extent.height)), true);
    list.AddImage(image, ImVec2(0, 0), ImVec2(float(extent.width), float(extent.height)));
    if (overlayText && *overlayText) {
        list.PushTexture(ImGui::GetIO().Fonts->TexRef);
        const auto text = ImGui::CalcTextSize(overlayText);
        list.AddRectFilled(ImVec2(4, 4), ImVec2(text.x + 16, text.y + 16), IM_COL32(0, 0, 0, 180));
        list.AddText(ImVec2(8, 8), IM_COL32_WHITE, overlayText);
        list.PopTexture();
    }
    list.PopClipRect();
    ImDrawData data;
    data.Valid = true;
    data.DisplayPos = ImVec2(0, 0);
    data.DisplaySize = ImVec2(float(extent.width), float(extent.height));
    data.FramebufferScale = ImVec2(1, 1);
    data.Textures = ImGui::GetDrawData()->Textures;
    data.OwnerViewport = ImGui::GetDrawData()->OwnerViewport;
    data.AddDrawList(&list);
    return secondary.present(
        [&](VkCommandBuffer commands) { ImGui_ImplVulkan_RenderDrawData(&data, commands); },
        .07f,
        .08f,
        .10f);
}
