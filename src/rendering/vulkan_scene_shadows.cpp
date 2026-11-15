#include "rendering/vulkan_clip.hpp"
#include "rendering/vulkan_scene_surface.hpp"
#include <array>
#include <stdexcept>
#include <string>

namespace {
void checkShadow(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed (VkResult " +
                                 std::to_string(int(result)) + ")");
    }
}
} // namespace

void VulkanSceneSurface::initializeShadows(const std::vector<uint32_t>& vertexShader) {
    if (vertexShader.empty() || vertexShader.front() != 0x07230203) {
        throw std::runtime_error("Invalid shadow SPIR-V");
    }
    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(physical_, &deviceProperties);
    if (deviceProperties.limits.maxImageArrayLayers < 12) {
        throw std::runtime_error("Vulkan device has too few shadow layers");
    }
    for (const auto format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D16_UNORM}) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physical_, format, &properties);
        const auto required = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
                              VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                              VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if ((properties.optimalTilingFeatures & required) == required) {
            shadowFormat_ = format;
            break;
        }
    }
    if (shadowFormat_ == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("No filterable Vulkan shadow depth format");
    }
    VkShaderModule shader = VK_NULL_HANDLE;
    try {
        VkAttachmentDescription attachment{};
        attachment.format = shadowFormat_;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        VkAttachmentReference reference{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.pDepthStencilAttachment = &reference;
        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[0].dstStageMask =
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask =
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp.attachmentCount = 1;
        rp.pAttachments = &attachment;
        rp.subpassCount = 1;
        rp.pSubpasses = &subpass;
        rp.dependencyCount = 2;
        rp.pDependencies = dependencies;
        checkShadow(vkCreateRenderPass(device_, &rp, nullptr, &shadowRenderPass_),
                    "vkCreateRenderPass shadow");
        VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mat4)};
        VkPipelineLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.pushConstantRangeCount = 1;
        layout.pPushConstantRanges = &range;
        checkShadow(vkCreatePipelineLayout(device_, &layout, nullptr, &shadowPipelineLayout_),
                    "vkCreatePipelineLayout shadow");
        VkShaderModuleCreateInfo module{};
        module.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        module.codeSize = vertexShader.size() * sizeof(uint32_t);
        module.pCode = vertexShader.data();
        checkShadow(vkCreateShaderModule(device_, &module, nullptr, &shader),
                    "vkCreateShaderModule shadow");
        VkPipelineShaderStageCreateInfo stage{};
        stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        stage.module = shader;
        stage.pName = "main";
        VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription position{
            0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)};
        VkPipelineVertexInputStateCreateInfo input{};
        input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        input.vertexBindingDescriptionCount = 1;
        input.pVertexBindingDescriptions = &binding;
        input.vertexAttributeDescriptionCount = 1;
        input.pVertexAttributeDescriptions = &position;
        VkPipelineInputAssemblyStateCreateInfo assembly{};
        assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{};
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_BACK_BIT;
        raster.lineWidth = 1;
        raster.depthBiasEnable = VK_TRUE;
        raster.depthBiasConstantFactor = 4;
        raster.depthBiasSlopeFactor = 2;
        VkPipelineMultisampleStateCreateInfo samples{};
        samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        const VkDynamicState states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = states;
        VkGraphicsPipelineCreateInfo pipeline{};
        pipeline.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeline.stageCount = 1;
        pipeline.pStages = &stage;
        pipeline.pVertexInputState = &input;
        pipeline.pInputAssemblyState = &assembly;
        pipeline.pViewportState = &viewport;
        pipeline.pRasterizationState = &raster;
        pipeline.pMultisampleState = &samples;
        pipeline.pDepthStencilState = &depth;
        pipeline.pDynamicState = &dynamic;
        pipeline.layout = shadowPipelineLayout_;
        pipeline.renderPass = shadowRenderPass_;
        for (int i = 0; i < 2; ++i) {
            raster.frontFace = i ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
            checkShadow(vkCreateGraphicsPipelines(
                            device_, VK_NULL_HANDLE, 1, &pipeline, nullptr, &shadowPipelines_[i]),
                        "vkCreateGraphicsPipelines shadow");
        }
        vkDestroyShaderModule(device_, shader, nullptr);
        shader = VK_NULL_HANDLE;
        for (int i = 0; i < 2; ++i) {
            VkSamplerCreateInfo sampler{};
            sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            sampler.magFilter = sampler.minFilter = i ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
            sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
            sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
                VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
            sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
            sampler.compareEnable = VK_TRUE;
            sampler.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            sampler.maxLod = 0;
            checkShadow(vkCreateSampler(device_, &sampler, nullptr, &shadowSamplers_[i]),
                        "vkCreateSampler shadow");
        }
        ensureShadowAtlas(1); // valid empty texture for scenes without shadow-casting lights
    } catch (...) {
        if (shader) {
            vkDestroyShaderModule(device_, shader, nullptr);
        }
        cleanupShadows();
        throw;
    }
}

void VulkanSceneSurface::ensureShadowAtlas(uint32_t resolution) {
    if (resolution == shadowResolution_ && shadowImage_) {
        return;
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_, &properties);
    if (!resolution || resolution > 2048 || resolution > properties.limits.maxImageDimension2D ||
        resolution > properties.limits.maxFramebufferWidth ||
        resolution > properties.limits.maxFramebufferHeight) {
        throw std::runtime_error("Invalid Vulkan shadow atlas resolution");
    }
    checkShadow(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
    destroyShadowAtlas();
    try {
        VkImageCreateInfo image{};
        image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = shadowFormat_;
        image.extent = {resolution, resolution, 1};
        image.mipLevels = 1;
        image.arrayLayers = 12;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                      VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        checkShadow(vkCreateImage(device_, &image, nullptr, &shadowImage_), "vkCreateImage shadow");
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device_, shadowImage_, &requirements);
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = requirements.size;
        alloc.memoryTypeIndex =
            memoryType(requirements.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        checkShadow(vkAllocateMemory(device_, &alloc, nullptr, &shadowMemory_),
                    "vkAllocateMemory shadow");
        checkShadow(vkBindImageMemory(device_, shadowImage_, shadowMemory_, 0),
                    "vkBindImageMemory shadow");
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = shadowImage_;
        view.format = shadowFormat_;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.layerCount = 12;
        checkShadow(vkCreateImageView(device_, &view, nullptr, &shadowArrayView_),
                    "vkCreateImageView shadow array");
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.subresourceRange.layerCount = 1;
        for (uint32_t layer = 0; layer < 12; ++layer) {
            view.subresourceRange.baseArrayLayer = layer;
            checkShadow(vkCreateImageView(device_, &view, nullptr, &shadowLayerViews_[layer]),
                        "vkCreateImageView shadow layer");
            VkFramebufferCreateInfo fb{};
            fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb.renderPass = shadowRenderPass_;
            fb.attachmentCount = 1;
            fb.pAttachments = &shadowLayerViews_[layer];
            fb.width = fb.height = resolution;
            fb.layers = 1;
            checkShadow(vkCreateFramebuffer(device_, &fb, nullptr, &shadowFramebuffers_[layer]),
                        "vkCreateFramebuffer shadow");
        }
        auto commands = beginCommands();
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = shadowImage_;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 12;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(commands,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &barrier);
        VkClearDepthStencilValue clear{1, 0};
        vkCmdClearDepthStencilImage(commands,
                                    shadowImage_,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                    &clear,
                                    1,
                                    &barrier.subresourceRange);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(commands,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &barrier);
        submitCommands();
        shadowResolution_ = resolution;
    } catch (...) {
        vkDeviceWaitIdle(device_);
        destroyShadowAtlas();
        throw;
    }
}
void VulkanSceneSurface::recordShadows(VkCommandBuffer commands,
                                       const std::vector<Draw>& draws,
                                       const ShadowPlan& plan) {
    const auto& meshes = meshOwner_ ? meshOwner_->meshes_ : meshes_;
    for (uint32_t layer = 0; layer < plan.layers.size(); ++layer) {
        const auto& pass = plan.layers[layer];
        if (!pass.resolution) {
            continue;
        }
        VkClearValue clear{};
        clear.depthStencil = {1, 0};
        VkRenderPassBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin.renderPass = shadowRenderPass_;
        begin.framebuffer = shadowFramebuffers_[layer];
        begin.renderArea.extent = {plan.atlasResolution, plan.atlasResolution};
        begin.clearValueCount = 1;
        begin.pClearValues = &clear;
        vkCmdBeginRenderPass(commands, &begin, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewport{0, 0, float(pass.resolution), float(pass.resolution), 0, 1};
        VkRect2D scissor{{0, 0}, {pass.resolution, pass.resolution}};
        vkCmdSetViewport(commands, 0, 1, &viewport);
        vkCmdSetScissor(commands, 0, 1, &scissor);
        for (const auto& draw : draws) {
            const auto& mesh = meshes[draw.mesh.id];
            if (!draw.lit || !draw.castsShadow || draw.transparent || mesh.lines || !mesh.count) {
                continue;
            }
            vkCmdBindPipeline(
                commands, VK_PIPELINE_BIND_POINT_GRAPHICS, shadowPipelines_[draw.mirrored ? 1 : 0]);
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(commands, 0, 1, &mesh.vertices.buffer, &offset);
            const Mat4 mvp = vulkanClipMatrix(multiply(pass.viewProjection, draw.model));
            vkCmdPushConstants(
                commands, shadowPipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(mvp), &mvp);
            vkCmdDraw(commands, mesh.count, 1, 0, 0);
        }
        vkCmdEndRenderPass(commands);
    }
}
void VulkanSceneSurface::destroyShadowAtlas() noexcept {
    for (auto& fb : shadowFramebuffers_) {
        if (fb) {
            vkDestroyFramebuffer(device_, fb, nullptr);
        }
        fb = VK_NULL_HANDLE;
    }
    for (auto& view : shadowLayerViews_) {
        if (view) {
            vkDestroyImageView(device_, view, nullptr);
        }
        view = VK_NULL_HANDLE;
    }
    if (shadowArrayView_) {
        vkDestroyImageView(device_, shadowArrayView_, nullptr);
    }
    if (shadowImage_) {
        vkDestroyImage(device_, shadowImage_, nullptr);
    }
    if (shadowMemory_) {
        vkFreeMemory(device_, shadowMemory_, nullptr);
    }
    shadowArrayView_ = VK_NULL_HANDLE;
    shadowImage_ = VK_NULL_HANDLE;
    shadowMemory_ = VK_NULL_HANDLE;
    shadowResolution_ = 0;
}
void VulkanSceneSurface::cleanupShadows() noexcept {
    destroyShadowAtlas();
    for (auto& sampler : shadowSamplers_) {
        if (sampler) {
            vkDestroySampler(device_, sampler, nullptr);
        }
        sampler = VK_NULL_HANDLE;
    }
    for (auto& pipeline : shadowPipelines_) {
        if (pipeline) {
            vkDestroyPipeline(device_, pipeline, nullptr);
        }
        pipeline = VK_NULL_HANDLE;
    }
    if (shadowPipelineLayout_) {
        vkDestroyPipelineLayout(device_, shadowPipelineLayout_, nullptr);
    }
    if (shadowRenderPass_) {
        vkDestroyRenderPass(device_, shadowRenderPass_, nullptr);
    }
    shadowPipelineLayout_ = VK_NULL_HANDLE;
    shadowRenderPass_ = VK_NULL_HANDLE;
    shadowFormat_ = VK_FORMAT_UNDEFINED;
}
