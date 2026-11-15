#include "rendering/vulkan_context.hpp"
#include "rendering/vulkan_surface_format.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed (VkResult " +
                                 std::to_string(static_cast<int>(result)) + ")");
    }
}

template <class T, class F> std::vector<T> enumerate(F call, const char* operation) {
    for (;;) {
        uint32_t count = 0;
        VkResult result = call(&count, nullptr);
        if (result != VK_SUCCESS && result != VK_INCOMPLETE) {
            check(result, operation);
        }
        std::vector<T> values(count);
        if (!count) {
            return values;
        }
        result = call(&count, values.data());
        if (result == VK_INCOMPLETE) {
            continue;
        }
        check(result, operation);
        values.resize(count);
        return values;
    }
}

bool hasExtension(const std::vector<VkExtensionProperties>& properties, const char* name) {
    return std::any_of(properties.begin(), properties.end(), [name](const auto& p) {
        return std::strcmp(p.extensionName, name) == 0;
    });
}

struct QueueFamilies {
    uint32_t graphics = UINT32_MAX, present = UINT32_MAX;
    bool valid() const {
        return graphics != UINT32_MAX && present != UINT32_MAX;
    }
};
QueueFamilies findQueues(VkPhysicalDevice physical, VkSurfaceKHR surface) {
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
    QueueFamilies result;
    for (uint32_t i = 0; i < count; ++i) {
        if (!families[i].queueCount) {
            continue;
        }
        VkBool32 present = VK_FALSE;
        check(vkGetPhysicalDeviceSurfaceSupportKHR(physical, i, surface, &present),
              "vkGetPhysicalDeviceSurfaceSupportKHR");
        const bool graphics = (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        if (graphics && present) {
            return {i, i};
        }
        if (graphics && result.graphics == UINT32_MAX) {
            result.graphics = i;
        }
        if (present && result.present == UINT32_MAX) {
            result.present = i;
        }
    }
    return result;
}
std::vector<VkSurfaceFormatKHR> surfaceFormats(VkPhysicalDevice physical, VkSurfaceKHR surface) {
    return enumerate<VkSurfaceFormatKHR>(
        [&](uint32_t* n, VkSurfaceFormatKHR* p) {
            return vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface, n, p);
        },
        "vkGetPhysicalDeviceSurfaceFormatsKHR");
}
std::vector<VkPresentModeKHR> presentModes(VkPhysicalDevice physical, VkSurfaceKHR surface) {
    return enumerate<VkPresentModeKHR>(
        [&](uint32_t* n, VkPresentModeKHR* p) {
            return vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface, n, p);
        },
        "vkGetPhysicalDeviceSurfacePresentModesKHR");
}
} // namespace

VulkanContext::VulkanContext(SDL_Window* window, bool vsync) : window_(window), vsync_(vsync) {
    if (!window_) {
        throw std::runtime_error("Vulkan requires an SDL window");
    }
    try {
        initialize();
    } catch (...) {
        cleanup();
        throw;
    }
}
VulkanContext::VulkanContext(SDL_Window* window, VulkanContext& owner, bool vsync)
    : window_(window), vsync_(vsync), ownsDevice_(false) {
    if (!window || !owner.ownsDevice_ || owner.failed_ || !owner.device_) {
        throw std::runtime_error("Invalid Vulkan secondary window or owner");
    }
    instance_ = owner.instance_;
    physical_ = owner.physical_;
    device_ = owner.device_;
    graphicsQueue_ = owner.graphicsQueue_;
    presentQueue_ = owner.presentQueue_;
    graphicsFamily_ = owner.graphicsFamily_;
    presentFamily_ = owner.presentFamily_;
    try {
        if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_)) {
            throw std::runtime_error(std::string("SDL Vulkan surface: ") + SDL_GetError());
        }
        VkBool32 supported = VK_FALSE;
        check(vkGetPhysicalDeviceSurfaceSupportKHR(physical_, presentFamily_, surface_, &supported),
              "vkGetPhysicalDeviceSurfaceSupportKHR");
        if (!supported || surfaceFormats(physical_, surface_).empty() ||
            presentModes(physical_, surface_).empty()) {
            throw std::runtime_error("Vulkan present queue does not support the secondary window");
        }
        initializeCommands();
    } catch (...) {
        cleanup();
        throw;
    }
}
VulkanContext::~VulkanContext() {
    cleanup();
}

void VulkanContext::initialize() {
    Uint32 extensionCount = 0;
    const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
    if (!sdlExtensions) {
        throw std::runtime_error(std::string("SDL Vulkan extensions: ") + SDL_GetError());
    }
    std::vector<const char*> extensions(sdlExtensions, sdlExtensions + extensionCount);
    auto available = enumerate<VkExtensionProperties>(
        [](uint32_t* n, VkExtensionProperties* p) {
            return vkEnumerateInstanceExtensionProperties(nullptr, n, p);
        },
        "vkEnumerateInstanceExtensionProperties");
    VkInstanceCreateFlags instanceFlags = 0;
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    if (hasExtension(available, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        if (std::find_if(extensions.begin(), extensions.end(), [](const char* e) {
                return std::strcmp(e, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) == 0;
            }) == extensions.end()) {
            extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        }
        instanceFlags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
#endif
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "LegacyEngine";
    app.apiVersion = VK_API_VERSION_1_0;
    VkInstanceCreateInfo create{};
    create.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create.flags = instanceFlags;
    create.pApplicationInfo = &app;
    create.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    create.ppEnabledExtensionNames = extensions.data();
    check(vkCreateInstance(&create, nullptr, &instance_), "vkCreateInstance");
    if (!SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_)) {
        throw std::runtime_error(std::string("SDL Vulkan surface: ") + SDL_GetError());
    }

    const auto devices = enumerate<VkPhysicalDevice>(
        [&](uint32_t* n, VkPhysicalDevice* p) {
            return vkEnumeratePhysicalDevices(instance_, n, p);
        },
        "vkEnumeratePhysicalDevices");
    std::vector<const char*> deviceExtensions;
    for (auto candidate : devices) {
        const auto props = enumerate<VkExtensionProperties>(
            [&](uint32_t* n, VkExtensionProperties* p) {
                return vkEnumerateDeviceExtensionProperties(candidate, nullptr, n, p);
            },
            "vkEnumerateDeviceExtensionProperties");
        if (!hasExtension(props, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            continue;
        }
        const QueueFamilies queues = findQueues(candidate, surface_);
        if (!queues.valid() || surfaceFormats(candidate, surface_).empty() ||
            presentModes(candidate, surface_).empty()) {
            continue;
        }
        physical_ = candidate;
        graphicsFamily_ = queues.graphics;
        presentFamily_ = queues.present;
        deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
        if (hasExtension(props, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) {
            deviceExtensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
        }
#else
        // The extension name is defined in vulkan_beta.h on some SDKs, but a
        // portability device still requires it to be enabled when advertised.
        if (hasExtension(props, "VK_KHR_portability_subset")) {
            deviceExtensions.push_back("VK_KHR_portability_subset");
        }
#endif
        break;
    }
    if (!physical_) {
        throw std::runtime_error("No Vulkan device supports graphics and presentation");
    }
    const float priority = 1.0f;
    std::array<VkDeviceQueueCreateInfo, 2> queues{};
    queues[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queues[0].queueFamilyIndex = graphicsFamily_;
    queues[0].queueCount = 1;
    queues[0].pQueuePriorities = &priority;
    queues[1] = queues[0];
    queues[1].queueFamilyIndex = presentFamily_;
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = graphicsFamily_ == presentFamily_ ? 1 : 2;
    deviceInfo.pQueueCreateInfos = queues.data();
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();
    check(vkCreateDevice(physical_, &deviceInfo, nullptr, &device_), "vkCreateDevice");
    vkGetDeviceQueue(device_, graphicsFamily_, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, presentFamily_, 0, &presentQueue_);
    initializeCommands();
}

void VulkanContext::initializeCommands() {
    VkCommandPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = graphicsFamily_;
    check(vkCreateCommandPool(device_, &pool, nullptr, &commandPool_), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = commandPool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(device_, &alloc, &commandBuffer_), "vkAllocateCommandBuffers");
    VkSemaphoreCreateInfo semaphore{};
    semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    check(vkCreateSemaphore(device_, &semaphore, nullptr, &imageAvailable_), "vkCreateSemaphore");
    VkFenceCreateInfo fence{};
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    check(vkCreateFence(device_, &fence, nullptr, &inFlight_), "vkCreateFence");
}

bool VulkanContext::createSwapchain() {
    int width = 0, height = 0;
    if (!SDL_GetWindowSizeInPixels(window_, &width, &height)) {
        throw std::runtime_error(std::string("SDL window size: ") + SDL_GetError());
    }
    if (width <= 0 || height <= 0) {
        return false;
    }
    check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
    destroySwapchain();

    VkSurfaceCapabilitiesKHR caps{};
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps),
          "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    const auto formats = surfaceFormats(physical_, surface_);
    const auto modes = presentModes(physical_, surface_);
    if (formats.empty() || modes.empty()) {
        throw std::runtime_error("Vulkan surface has no formats or present modes");
    }
    if (!(caps.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)) {
        throw std::runtime_error("Vulkan surface does not support color attachments");
    }
    const VkSurfaceFormatKHR format = chooseEditorSurfaceFormat(formats);
    colorFormat_ = format.format;
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync_) {
        if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_MAILBOX_KHR) != modes.end()) {
            mode = VK_PRESENT_MODE_MAILBOX_KHR;
        } else if (std::find(modes.begin(), modes.end(), VK_PRESENT_MODE_IMMEDIATE_KHR) !=
                   modes.end()) {
            mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
        }
    }
    if (caps.currentExtent.width != UINT32_MAX) {
        extent_ = caps.currentExtent;
    } else {
        extent_.width = std::clamp(
            static_cast<uint32_t>(width), caps.minImageExtent.width, caps.maxImageExtent.width);
        extent_.height = std::clamp(
            static_cast<uint32_t>(height), caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    if (!extent_.width || !extent_.height) {
        return false;
    }
    uint32_t count = caps.minImageCount;
    if (count < UINT32_MAX) {
        ++count;
    }
    if (caps.maxImageCount && count > caps.maxImageCount) {
        count = caps.maxImageCount;
    }
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (auto bit : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                     VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                     VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                     VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
        if (caps.supportedCompositeAlpha & bit) {
            alpha = bit;
            break;
        }
    }
    const uint32_t families[] = {graphicsFamily_, presentFamily_};
    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = surface_;
    info.minImageCount = count;
    info.imageFormat = format.format;
    info.imageColorSpace = format.colorSpace;
    info.imageExtent = extent_;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (graphicsFamily_ != presentFamily_) {
        info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices = families;
    } else {
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = alpha;
    info.presentMode = mode;
    info.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_), "vkCreateSwapchainKHR");
    images_ = enumerate<VkImage>(
        [&](uint32_t* n, VkImage* p) { return vkGetSwapchainImagesKHR(device_, swapchain_, n, p); },
        "vkGetSwapchainImagesKHR");
    if (images_.empty()) {
        throw std::runtime_error("Vulkan swapchain has no images");
    }

    VkAttachmentDescription attachment{};
    attachment.format = format.format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &reference;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo pass{};
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    pass.attachmentCount = 1;
    pass.pAttachments = &attachment;
    pass.subpassCount = 1;
    pass.pSubpasses = &subpass;
    pass.dependencyCount = 1;
    pass.pDependencies = &dependency;
    check(vkCreateRenderPass(device_, &pass, nullptr, &renderPass_), "vkCreateRenderPass");
    views_.resize(images_.size(), VK_NULL_HANDLE);
    framebuffers_.resize(images_.size(), VK_NULL_HANDLE);
    renderFinished_.resize(images_.size(), VK_NULL_HANDLE);
    for (size_t i = 0; i < images_.size(); ++i) {
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = images_[i];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = format.format;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.layerCount = 1;
        check(vkCreateImageView(device_, &view, nullptr, &views_[i]), "vkCreateImageView");
        VkFramebufferCreateInfo fb{};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = renderPass_;
        fb.attachmentCount = 1;
        fb.pAttachments = &views_[i];
        fb.width = extent_.width;
        fb.height = extent_.height;
        fb.layers = 1;
        check(vkCreateFramebuffer(device_, &fb, nullptr, &framebuffers_[i]), "vkCreateFramebuffer");
        VkSemaphoreCreateInfo semaphore{};
        semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        check(vkCreateSemaphore(device_, &semaphore, nullptr, &renderFinished_[i]),
              "vkCreateSemaphore");
    }
    recreate_ = false;
    ++swapchainGeneration_;
    return true;
}

bool VulkanContext::prepare() {
    if (failed_) {
        throw std::runtime_error("Vulkan context cannot be reused after an error");
    }
    try {
        return prepareImpl();
    } catch (...) {
        failed_ = true;
        throw;
    }
}

bool VulkanContext::prepareImpl() {
    if (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) {
        recreate_ = true;
        return false;
    }
    int width = 0, height = 0;
    if (!SDL_GetWindowSizeInPixels(window_, &width, &height)) {
        throw std::runtime_error(std::string("SDL window size: ") + SDL_GetError());
    }
    if (width <= 0 || height <= 0) {
        recreate_ = true;
        return false;
    }
    return !recreate_ || createSwapchain();
}

VulkanContext::FrameResult
VulkanContext::presentClear(float red, float green, float blue, float alpha) {
    return present({}, red, green, blue, alpha);
}

VulkanContext::FrameResult
VulkanContext::present(const std::function<void(VkCommandBuffer)>& record,
                       float red,
                       float green,
                       float blue,
                       float alpha) {
    if (failed_) {
        throw std::runtime_error("Vulkan context cannot be reused after an error");
    }
    try {
        return presentImpl(record, red, green, blue, alpha);
    } catch (...) {
        failed_ = true;
        throw;
    }
}

VulkanContext::FrameResult
VulkanContext::presentImpl(const std::function<void(VkCommandBuffer)>& record,
                           float red,
                           float green,
                           float blue,
                           float alpha) {
    if (!prepareImpl()) {
        return FrameResult::Skipped;
    }
    check(vkWaitForFences(device_, 1, &inFlight_, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    uint32_t index = 0;
    const VkResult acquired = vkAcquireNextImageKHR(
        device_, swapchain_, UINT64_MAX, imageAvailable_, VK_NULL_HANDLE, &index);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        recreate_ = true;
        return FrameResult::Skipped;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
        check(acquired, "vkAcquireNextImageKHR");
    }
    check(vkResetCommandBuffer(commandBuffer_, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(commandBuffer_, &begin), "vkBeginCommandBuffer");
    VkClearValue clear{};
    clear.color.float32[0] = red;
    clear.color.float32[1] = green;
    clear.color.float32[2] = blue;
    clear.color.float32[3] = alpha;
    VkRenderPassBeginInfo render{};
    render.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render.renderPass = renderPass_;
    render.framebuffer = framebuffers_.at(index);
    render.renderArea.extent = extent_;
    render.clearValueCount = 1;
    render.pClearValues = &clear;
    vkCmdBeginRenderPass(commandBuffer_, &render, VK_SUBPASS_CONTENTS_INLINE);
    if (record) {
        record(commandBuffer_);
    }
    vkCmdEndRenderPass(commandBuffer_);
    check(vkEndCommandBuffer(commandBuffer_), "vkEndCommandBuffer");
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &imageAvailable_;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer_;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &renderFinished_.at(index);
    check(vkResetFences(device_, 1, &inFlight_), "vkResetFences");
    check(vkQueueSubmit(graphicsQueue_, 1, &submit, inFlight_), "vkQueueSubmit");
    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished_[index];
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &index;
    const VkResult result = vkQueuePresentKHR(presentQueue_, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
        acquired == VK_SUBOPTIMAL_KHR) {
        recreate_ = true;
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR && result != VK_ERROR_OUT_OF_DATE_KHR) {
        check(result, "vkQueuePresentKHR");
    }
    return result == VK_ERROR_OUT_OF_DATE_KHR ? FrameResult::Skipped : FrameResult::Presented;
}

void VulkanContext::destroySwapchain() noexcept {
    for (auto semaphore : renderFinished_) {
        if (semaphore) {
            vkDestroySemaphore(device_, semaphore, nullptr);
        }
    }
    for (auto framebuffer : framebuffers_) {
        if (framebuffer) {
            vkDestroyFramebuffer(device_, framebuffer, nullptr);
        }
    }
    for (auto view : views_) {
        if (view) {
            vkDestroyImageView(device_, view, nullptr);
        }
    }
    if (renderPass_) {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
    }
    if (swapchain_) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    }
    renderFinished_.clear();
    framebuffers_.clear();
    views_.clear();
    images_.clear();
    renderPass_ = VK_NULL_HANDLE;
    swapchain_ = VK_NULL_HANDLE;
    extent_ = {};
    colorFormat_ = VK_FORMAT_UNDEFINED;
}

void VulkanContext::cleanup() noexcept {
    if (device_) {
        vkDeviceWaitIdle(device_);
        destroySwapchain();
        if (inFlight_) {
            vkDestroyFence(device_, inFlight_, nullptr);
        }
        if (imageAvailable_) {
            vkDestroySemaphore(device_, imageAvailable_, nullptr);
        }
        if (commandPool_) {
            vkDestroyCommandPool(device_, commandPool_, nullptr);
        }
        if (ownsDevice_) {
            vkDestroyDevice(device_, nullptr);
        }
    }
    if (surface_) {
        SDL_Vulkan_DestroySurface(instance_, surface_, nullptr);
    }
    if (instance_ && ownsDevice_) {
        vkDestroyInstance(instance_, nullptr);
    }
}
