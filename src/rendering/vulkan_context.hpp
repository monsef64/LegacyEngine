// Vulkan device and presentation context.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>
#include <vulkan/vulkan.h>

struct SDL_Window;

class VulkanContext {
  public:
    enum class FrameResult { Presented, Skipped };

    // The SDL window must have SDL_WINDOW_VULKAN and outlive this context.
    // Throws std::runtime_error on failure; partial initialization is cleaned up.
    explicit VulkanContext(SDL_Window* window, bool vsync = true);
    // A secondary window uses the owner's Vulkan instance/device/queues, so
    // scene images and descriptors can be shared. The owner must outlive it.
    // The owner's present queue must also support this window's surface.
    VulkanContext(SDL_Window* window, VulkanContext& owner, bool vsync = true);
    ~VulkanContext();
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    VulkanContext(VulkanContext&&) = delete;
    VulkanContext& operator=(VulkanContext&&) = delete;

    // Clears and presents one frame. Skips rendering while the window is minimized,
    // zero-sized or when the swapchain needs recreation. Not safe to call from multiple threads.
    // If this throws, destroy the context instead of attempting to reuse it.
    FrameResult presentClear(float red, float green, float blue, float alpha = 1.0f);
    // Prepare before building UI; a changed swapchainGeneration requires the
    // caller to recreate render-pass-dependent pipelines and image-count state.
    // Returns false while minimized or zero-sized. Throws on fatal errors.
    bool prepare();
    // Calls record inside the swapchain render pass. Record must not submit,
    // end the render pass or retain the command buffer. An exception poisons
    // the context; destroy it rather than retrying.
    FrameResult present(const std::function<void(VkCommandBuffer)>& record,
                        float red,
                        float green,
                        float blue,
                        float alpha = 1.0f);
    VkInstance instance() const {
        return instance_;
    }
    VkRenderPass renderPass() const {
        return renderPass_;
    }
    VkFormat colorFormat() const {
        return colorFormat_;
    }
    VkExtent2D extent() const {
        return extent_;
    }
    uint32_t imageCount() const {
        return static_cast<uint32_t>(images_.size());
    }
    uint64_t swapchainGeneration() const {
        return swapchainGeneration_;
    }
    bool needsRecreation() const {
        return recreate_;
    }
    // The context owns these handles and must outlive resources created from them.
    // External queue use must be synchronized with presentClear().
    VkPhysicalDevice physicalDevice() const {
        return physical_;
    }
    VkDevice device() const {
        return device_;
    }
    VkQueue graphicsQueue() const {
        return graphicsQueue_;
    }
    uint32_t graphicsFamily() const {
        return graphicsFamily_;
    }
    void requestResize() {
        recreate_ = true;
    }
    void setVsync(bool enabled) {
        if (vsync_ != enabled) {
            vsync_ = enabled;
            recreate_ = true;
        }
    }

  private:
    void initialize();
    void initializeCommands();
    bool prepareImpl();
    FrameResult presentImpl(const std::function<void(VkCommandBuffer)>& record,
                            float red,
                            float green,
                            float blue,
                            float alpha);
    bool createSwapchain();
    void destroySwapchain() noexcept;
    void cleanup() noexcept;

    SDL_Window* window_ = nullptr;
    bool vsync_ = true, recreate_ = true, failed_ = false, ownsDevice_ = true;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE, presentQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsFamily_ = 0, presentFamily_ = 0;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;
    VkFence inFlight_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    uint64_t swapchainGeneration_ = 0;
    std::vector<VkImage> images_;
    std::vector<VkImageView> views_;
    std::vector<VkFramebuffer> framebuffers_;
    // A present-wait semaphore belongs to each swapchain image, not to the
    // CPU frame: a graphics fence does not guarantee presentation has finished.
    std::vector<VkSemaphore> renderFinished_;
};
