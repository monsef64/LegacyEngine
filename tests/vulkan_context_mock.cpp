// GPU-free lifecycle and swapchain regression tests for the Vulkan prototype.
// This executable supplies its own SDL and Vulkan entry points; it does not
// replace a graphical test with a real Vulkan driver and validation layers.
#include "rendering/vulkan_context.hpp"
#include "rendering/vulkan_surface_format.hpp"
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <cmath>

#include <cassert>
#include <cstring>
#include <set>
#include <stdexcept>
#include <type_traits>

namespace mock {
uintptr_t next = 10;
std::set<uintptr_t> live;
int width = 800, height = 600;
unsigned swaps = 0, submits = 0, presents = 0, waits = 0;
VkPresentModeKHR lastMode = VK_PRESENT_MODE_MAX_ENUM_KHR;
VkSharingMode sharing = VK_SHARING_MODE_MAX_ENUM;
VkResult acquireResult = VK_SUCCESS, presentResult = VK_SUCCESS;
bool separateQueues = false, noDevices = false, failFramebuffer = false, minimized = false;
bool rejectPresent = false;
uint32_t imageIndex = 0;

template <class T> uintptr_t id(T handle) {
    if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<uintptr_t>(handle);
    } else {
        return static_cast<uintptr_t>(handle);
    }
}
template <class T> T handle(uintptr_t value) {
    if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<T>(value);
    } else {
        return static_cast<T>(value);
    }
}
template <class T> T create() {
    auto value = ++next;
    assert(live.insert(value).second);
    return handle<T>(value);
}
template <class T> void destroy(T value) {
    assert(live.erase(id(value)) == 1);
}
template <class T> VkResult enumerate(uint32_t* count, T* out, const T* values, uint32_t n) {
    if (!out) {
        *count = n;
        return VK_SUCCESS;
    }
    uint32_t written = *count < n ? *count : n;
    for (uint32_t i = 0; i < written; ++i) {
        out[i] = values[i];
    }
    *count = written;
    return written == n ? VK_SUCCESS : VK_INCOMPLETE;
}
void reset() {
    assert(live.empty());
    width = 800;
    height = 600;
    swaps = submits = presents = waits = 0;
    lastMode = VK_PRESENT_MODE_MAX_ENUM_KHR;
    sharing = VK_SHARING_MODE_MAX_ENUM;
    acquireResult = presentResult = VK_SUCCESS;
    separateQueues = noDevices = failFramebuffer = minimized = rejectPresent = false;
    imageIndex = 0;
}
} // namespace mock

extern "C" {
const char* SDLCALL SDL_GetError(void) {
    return "mock SDL error";
}
SDL_WindowFlags SDLCALL SDL_GetWindowFlags(SDL_Window*) {
    return mock::minimized ? SDL_WINDOW_MINIMIZED : 0;
}
bool SDLCALL SDL_GetWindowSizeInPixels(SDL_Window*, int* w, int* h) {
    *w = mock::width;
    *h = mock::height;
    return true;
}
char const* const* SDLCALL SDL_Vulkan_GetInstanceExtensions(Uint32* count) {
    static const char* extensions[] = {"VK_KHR_surface"};
    *count = 1;
    return extensions;
}
bool SDLCALL SDL_Vulkan_CreateSurface(SDL_Window*,
                                      VkInstance,
                                      const VkAllocationCallbacks*,
                                      VkSurfaceKHR* out) {
    *out = mock::create<VkSurfaceKHR>();
    return true;
}
void SDLCALL SDL_Vulkan_DestroySurface(VkInstance,
                                       VkSurfaceKHR surface,
                                       const VkAllocationCallbacks*) {
    mock::destroy(surface);
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties(const char*,
                                                                      uint32_t* n,
                                                                      VkExtensionProperties* p) {
    VkExtensionProperties value{};
    std::strcpy(value.extensionName, "VK_KHR_surface");
    return mock::enumerate(n, p, &value, 1);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance(const VkInstanceCreateInfo*,
                                                const VkAllocationCallbacks*,
                                                VkInstance* out) {
    *out = mock::create<VkInstance>();
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance v, const VkAllocationCallbacks*) {
    mock::destroy(v);
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumeratePhysicalDevices(VkInstance,
                                                          uint32_t* n,
                                                          VkPhysicalDevice* p) {
    const auto physical = mock::handle<VkPhysicalDevice>(1);
    return mock::enumerate(n, p, &physical, mock::noDevices ? 0 : 1);
}
VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateDeviceExtensionProperties(VkPhysicalDevice,
                                                                    const char*,
                                                                    uint32_t* n,
                                                                    VkExtensionProperties* p) {
    VkExtensionProperties value{};
    std::strcpy(value.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    return mock::enumerate(n, p, &value, 1);
}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice,
                                                                    uint32_t* n,
                                                                    VkQueueFamilyProperties* p) {
    const uint32_t count = mock::separateQueues ? 2 : 1;
    if (!p) {
        *n = count;
        return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        p[i] = {};
        p[i].queueCount = 1;
    }
    p[0].queueFlags = VK_QUEUE_GRAPHICS_BIT;
    *n = count;
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice,
                                                                    uint32_t family,
                                                                    VkSurfaceKHR,
                                                                    VkBool32* supported) {
    *supported = !mock::rejectPresent && (!mock::separateQueues || family == 1);
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice,
                                                                    VkSurfaceKHR,
                                                                    uint32_t* n,
                                                                    VkSurfaceFormatKHR* p) {
    const VkSurfaceFormatKHR value{VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    return mock::enumerate(n, p, &value, 1);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice,
                                                                         VkSurfaceKHR,
                                                                         uint32_t* n,
                                                                         VkPresentModeKHR* p) {
    const VkPresentModeKHR values[]{VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_MAILBOX_KHR};
    return mock::enumerate(n, p, values, 2);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDevice(VkPhysicalDevice,
                                              const VkDeviceCreateInfo* info,
                                              const VkAllocationCallbacks*,
                                              VkDevice* out) {
    assert(info->queueCreateInfoCount == (mock::separateQueues ? 2u : 1u));
    *out = mock::create<VkDevice>();
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice v, const VkAllocationCallbacks*) {
    mock::destroy(v);
}
VKAPI_ATTR void VKAPI_CALL vkGetDeviceQueue(VkDevice, uint32_t family, uint32_t, VkQueue* out) {
    *out = mock::handle<VkQueue>(family + 2);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateCommandPool(VkDevice,
                                                   const VkCommandPoolCreateInfo*,
                                                   const VkAllocationCallbacks*,
                                                   VkCommandPool* out) {
    *out = mock::create<VkCommandPool>();
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyCommandPool(VkDevice,
                                                VkCommandPool pool,
                                                const VkAllocationCallbacks*) {
    mock::destroy(pool);
}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateCommandBuffers(VkDevice,
                                                        const VkCommandBufferAllocateInfo*,
                                                        VkCommandBuffer* out) {
    *out = mock::handle<VkCommandBuffer>(4);
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSemaphore(VkDevice,
                                                 const VkSemaphoreCreateInfo*,
                                                 const VkAllocationCallbacks*,
                                                 VkSemaphore* out) {
    *out = mock::create<VkSemaphore>();
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroySemaphore(VkDevice,
                                              VkSemaphore v,
                                              const VkAllocationCallbacks*) {
    mock::destroy(v);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFence(VkDevice,
                                             const VkFenceCreateInfo* info,
                                             const VkAllocationCallbacks*,
                                             VkFence* out) {
    assert(info->flags & VK_FENCE_CREATE_SIGNALED_BIT);
    *out = mock::create<VkFence>();
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyFence(VkDevice, VkFence v, const VkAllocationCallbacks*) {
    mock::destroy(v);
}
VKAPI_ATTR VkResult VKAPI_CALL vkDeviceWaitIdle(VkDevice) {
    ++mock::waits;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
    VkPhysicalDevice, VkSurfaceKHR, VkSurfaceCapabilitiesKHR* p) {
    *p = {};
    p->minImageCount = 2;
    p->maxImageCount = 3;
    p->currentExtent = {UINT32_MAX, UINT32_MAX};
    p->minImageExtent = {1, 1};
    p->maxImageExtent = {4096, 4096};
    p->supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    p->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    p->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateSwapchainKHR(VkDevice,
                                                    const VkSwapchainCreateInfoKHR* info,
                                                    const VkAllocationCallbacks*,
                                                    VkSwapchainKHR* out) {
    assert(info->minImageCount == 3);
    assert(info->imageExtent.width == static_cast<uint32_t>(mock::width));
    assert(info->imageExtent.height == static_cast<uint32_t>(mock::height));
    mock::lastMode = info->presentMode;
    mock::sharing = info->imageSharingMode;
    ++mock::swaps;
    *out = mock::create<VkSwapchainKHR>();
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroySwapchainKHR(VkDevice,
                                                 VkSwapchainKHR v,
                                                 const VkAllocationCallbacks*) {
    mock::destroy(v);
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetSwapchainImagesKHR(VkDevice,
                                                       VkSwapchainKHR,
                                                       uint32_t* n,
                                                       VkImage* p) {
    const VkImage images[]{
        mock::handle<VkImage>(101), mock::handle<VkImage>(102), mock::handle<VkImage>(103)};
    return mock::enumerate(n, p, images, 3);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateRenderPass(VkDevice,
                                                  const VkRenderPassCreateInfo* info,
                                                  const VkAllocationCallbacks*,
                                                  VkRenderPass* out) {
    assert(info->pAttachments[0].finalLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    *out = mock::create<VkRenderPass>();
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyRenderPass(VkDevice,
                                               VkRenderPass v,
                                               const VkAllocationCallbacks*) {
    mock::destroy(v);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice,
                                                 const VkImageViewCreateInfo*,
                                                 const VkAllocationCallbacks*,
                                                 VkImageView* out) {
    *out = mock::create<VkImageView>();
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice,
                                              VkImageView v,
                                              const VkAllocationCallbacks*) {
    mock::destroy(v);
}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateFramebuffer(VkDevice,
                                                   const VkFramebufferCreateInfo*,
                                                   const VkAllocationCallbacks*,
                                                   VkFramebuffer* out) {
    if (mock::failFramebuffer) {
        return VK_ERROR_OUT_OF_HOST_MEMORY;
    }
    *out = mock::create<VkFramebuffer>();
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkDestroyFramebuffer(VkDevice,
                                                VkFramebuffer v,
                                                const VkAllocationCallbacks*) {
    mock::destroy(v);
}
VKAPI_ATTR VkResult VKAPI_CALL
vkWaitForFences(VkDevice, uint32_t, const VkFence*, VkBool32, uint64_t) {
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice, uint32_t, const VkFence*) {
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL
vkAcquireNextImageKHR(VkDevice, VkSwapchainKHR, uint64_t, VkSemaphore, VkFence, uint32_t* index) {
    *index = mock::imageIndex++ % 3;
    auto result = mock::acquireResult;
    mock::acquireResult = VK_SUCCESS;
    return result;
}
VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(VkCommandBuffer, VkCommandBufferResetFlags) {
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer,
                                                    const VkCommandBufferBeginInfo*) {
    return VK_SUCCESS;
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer,
                                                const VkRenderPassBeginInfo* info,
                                                VkSubpassContents) {
    assert(info->clearValueCount == 1 && info->pClearValues[0].color.float32[3] == 1.0f);
}
VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer) {}
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer) {
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue,
                                             uint32_t n,
                                             const VkSubmitInfo* info,
                                             VkFence) {
    assert(n == 1 && info->waitSemaphoreCount == 1 && info->signalSemaphoreCount == 1);
    ++mock::submits;
    return VK_SUCCESS;
}
VKAPI_ATTR VkResult VKAPI_CALL vkQueuePresentKHR(VkQueue, const VkPresentInfoKHR* info) {
    assert(info->waitSemaphoreCount == 1 && info->swapchainCount == 1);
    ++mock::presents;
    auto result = mock::presentResult;
    mock::presentResult = VK_SUCCESS;
    return result;
}
} // extern "C"

int main() {
    using Result = VulkanContext::FrameResult;
    const VkColorSpaceKHR srgb = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    assert(chooseEditorSurfaceFormat({{VK_FORMAT_B8G8R8A8_SRGB, srgb},
                                      {VK_FORMAT_R8G8B8A8_UNORM, srgb},
                                      {VK_FORMAT_B8G8R8A8_UNORM, srgb}})
               .format == VK_FORMAT_B8G8R8A8_UNORM);
    assert(chooseEditorSurfaceFormat({{VK_FORMAT_UNDEFINED, srgb}}).format ==
           VK_FORMAT_B8G8R8A8_UNORM);
    assert(chooseEditorSurfaceFormat({{VK_FORMAT_B8G8R8A8_SRGB, srgb}}).format ==
           VK_FORMAT_B8G8R8A8_SRGB);
    assert(vulkanSrgbFormat(VK_FORMAT_B8G8R8A8_SRGB));
    assert(!vulkanSrgbFormat(VK_FORMAT_B8G8R8A8_UNORM));
    assert(std::abs(editorSrgbToLinear(.5f) - .214041f) < .00001f);
    SDL_Window* window = mock::handle<SDL_Window*>(1);
    mock::reset();
    {
        VulkanContext context(window);
        assert(context.swapchainGeneration() == 0 && context.prepare());
        assert(context.swapchainGeneration() == 1 && context.imageCount() == 3 &&
               context.renderPass());
        bool recorded = false;
        assert(context.present(
                   [&](VkCommandBuffer command) {
                       assert(command);
                       recorded = true;
                   },
                   .1f,
                   .2f,
                   .3f) == Result::Presented);
        assert(recorded);
        assert(mock::swaps == 1 && mock::submits == 1 && mock::presents == 1);
        assert(mock::lastMode == VK_PRESENT_MODE_FIFO_KHR &&
               mock::sharing == VK_SHARING_MODE_EXCLUSIVE);
        context.setVsync(false);
        assert(context.presentClear(.1f, .2f, .3f) == Result::Presented);
        assert(mock::swaps == 2 && mock::lastMode == VK_PRESENT_MODE_MAILBOX_KHR);
        context.setVsync(false);
        assert(context.presentClear(.1f, .2f, .3f) == Result::Presented && mock::swaps == 2);
        mock::width = 0;
        assert(context.presentClear(.1f, .2f, .3f) == Result::Skipped && mock::swaps == 2);
        mock::width = 640;
        mock::height = 480;
        assert(context.presentClear(.1f, .2f, .3f) == Result::Presented && mock::swaps == 3);
        mock::acquireResult = VK_ERROR_OUT_OF_DATE_KHR;
        assert(context.presentClear(.1f, .2f, .3f) == Result::Skipped);
        assert(context.presentClear(.1f, .2f, .3f) == Result::Presented && mock::swaps == 4);
        mock::presentResult = VK_SUBOPTIMAL_KHR;
        assert(context.presentClear(.1f, .2f, .3f) == Result::Presented);
        assert(context.presentClear(.1f, .2f, .3f) == Result::Presented && mock::swaps == 5);
        mock::presentResult = VK_ERROR_OUT_OF_DATE_KHR;
        assert(context.presentClear(.1f, .2f, .3f) == Result::Skipped);
        assert(context.presentClear(.1f, .2f, .3f) == Result::Presented && mock::swaps == 6);
        context.requestResize();
        assert(context.presentClear(.1f, .2f, .3f) == Result::Presented && mock::swaps == 7);
        // Some platforms retain a non-zero drawable size while minimized.
        mock::minimized = true;
        assert(context.presentClear(.1f, .2f, .3f) == Result::Skipped && mock::swaps == 7);
        mock::minimized = false;
        assert(context.presentClear(.1f, .2f, .3f) == Result::Presented && mock::swaps == 8);
    }
    assert(mock::live.empty());
    mock::reset();
    {
        VulkanContext owner(window);
        assert(owner.presentClear(0, 0, 0) == Result::Presented);
        {
            VulkanContext second(mock::handle<SDL_Window*>(2), owner);
            assert(second.instance() == owner.instance() && second.device() == owner.device() &&
                   second.graphicsQueue() == owner.graphicsQueue());
            assert(second.presentClear(0, 0, 0) == Result::Presented);
            assert(second.colorFormat() == owner.colorFormat());
            second.requestResize();
            assert(second.presentClear(0, 0, 0) == Result::Presented);
            try {
                VulkanContext nested(mock::handle<SDL_Window*>(3), second);
                assert(false);
            } catch (const std::runtime_error&) {
            }
        }
        mock::rejectPresent = true;
        try {
            VulkanContext unsupported(mock::handle<SDL_Window*>(2), owner);
            assert(false);
        } catch (const std::runtime_error&) {
        }
        mock::rejectPresent = false;
        assert(owner.presentClear(0, 0, 0) == Result::Presented);
    }
    assert(mock::live.empty());
    mock::reset();
    mock::separateQueues = true;
    {
        VulkanContext context(window);
        assert(context.presentClear(0, 0, 0) == Result::Presented);
        assert(mock::sharing == VK_SHARING_MODE_CONCURRENT);
        {
            VulkanContext second(mock::handle<SDL_Window*>(2), context);
            assert(second.presentClear(0, 0, 0) == Result::Presented);
            assert(mock::sharing == VK_SHARING_MODE_CONCURRENT);
        }
        assert(context.presentClear(0, 0, 0) == Result::Presented);
    }
    assert(mock::live.empty());
    mock::reset();
    {
        VulkanContext context(window);
        try {
            context.present(
                [](VkCommandBuffer) { throw std::runtime_error("record failed"); }, 0, 0, 0);
            assert(false);
        } catch (const std::runtime_error&) {
        }
        try {
            context.prepare();
            assert(false);
        } catch (const std::runtime_error&) {
        }
    }
    assert(mock::live.empty());
    mock::reset();
    mock::noDevices = true;
    try {
        VulkanContext context(window);
        assert(false);
    } catch (const std::runtime_error&) {
    }
    assert(mock::live.empty());
    mock::reset();
    mock::failFramebuffer = true;
    {
        VulkanContext context(window);
        try {
            context.presentClear(0, 0, 0);
            assert(false);
        } catch (const std::runtime_error&) {
        }
        mock::failFramebuffer = false;
        try {
            context.presentClear(0, 0, 0);
            assert(false);
        } catch (const std::runtime_error&) {
        }
    }
    assert(mock::live.empty());
}
