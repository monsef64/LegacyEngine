#include "rendering/vulkan_clip.hpp"
#include "rendering/vulkan_scene_surface.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void checkMotion(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed (VkResult " +
                                 std::to_string(static_cast<int>(result)) + ")");
    }
}
VkShaderModule motionShader(VkDevice device, const std::vector<uint32_t>& code) {
    if (code.empty() || code.front() != 0x07230203) {
        throw std::runtime_error("Invalid motion-vector SPIR-V shader");
    }
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size() * sizeof(uint32_t);
    info.pCode = code.data();
    VkShaderModule shader = VK_NULL_HANDLE;
    checkMotion(vkCreateShaderModule(device, &info, nullptr, &shader), "vkCreateShaderModule");
    return shader;
}
struct MotionUniform {
    Mat4 raster, current, previous;
};
static_assert(sizeof(MotionUniform) == 192, "Motion GLSL uniform layout");
bool finiteMatrix(const Mat4& m) {
    for (float value : m.m) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return true;
}
} // namespace

struct VulkanSceneSurface::Motion {
    explicit Motion(VulkanSceneSurface& surface) : owner(surface), device(surface.device_) {}
    ~Motion() {
        destroyImages();
        for (auto pipeline : pipelines) {
            if (pipeline) {
                vkDestroyPipeline(device, pipeline, nullptr);
            }
        }
        if (pipelineLayout) {
            vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        }
        if (setLayout) {
            vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
        }
        if (renderPass) {
            vkDestroyRenderPass(device, renderPass, nullptr);
        }
    }
    void destroyImages() noexcept {
        if (framebuffer) {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
        if (colorView) {
            vkDestroyImageView(device, colorView, nullptr);
        }
        if (depthView) {
            vkDestroyImageView(device, depthView, nullptr);
        }
        if (color) {
            vkDestroyImage(device, color, nullptr);
        }
        if (depth) {
            vkDestroyImage(device, depth, nullptr);
        }
        if (colorMemory) {
            vkFreeMemory(device, colorMemory, nullptr);
        }
        if (depthMemory) {
            vkFreeMemory(device, depthMemory, nullptr);
        }
        framebuffer = VK_NULL_HANDLE;
        colorView = depthView = VK_NULL_HANDLE;
        color = depth = VK_NULL_HANDLE;
        colorMemory = depthMemory = VK_NULL_HANDLE;
        width = height = 0;
        rendered = false;
    }
    void image(uint32_t w,
               uint32_t h,
               VkFormat format,
               VkImageUsageFlags usage,
               VkImageAspectFlags aspect,
               VkImage& result,
               VkDeviceMemory& memory,
               VkImageView& view) {
        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {w, h, 1};
        info.mipLevels = info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        checkMotion(vkCreateImage(device, &info, nullptr, &result), "vkCreateImage motion");
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device, result, &req);
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex =
            owner.memoryType(req.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        checkMotion(vkAllocateMemory(device, &alloc, nullptr, &memory), "vkAllocateMemory motion");
        checkMotion(vkBindImageMemory(device, result, memory, 0), "vkBindImageMemory motion");
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = result;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format;
        vi.subresourceRange = {aspect, 0, 1, 0, 1};
        checkMotion(vkCreateImageView(device, &vi, nullptr, &view), "vkCreateImageView motion");
    }
    void resize(uint32_t w, uint32_t h) {
        checkMotion(vkDeviceWaitIdle(device), "vkDeviceWaitIdle motion");
        destroyImages();
        image(w,
              h,
              VK_FORMAT_R32G32_SFLOAT,
              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                  VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
              VK_IMAGE_ASPECT_COLOR_BIT,
              color,
              colorMemory,
              colorView);
        image(w,
              h,
              VK_FORMAT_D32_SFLOAT,
              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
              VK_IMAGE_ASPECT_DEPTH_BIT,
              depth,
              depthMemory,
              depthView);
        const VkImageView views[]{colorView, depthView};
        VkFramebufferCreateInfo fb{};
        fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass = renderPass;
        fb.attachmentCount = 2;
        fb.pAttachments = views;
        fb.width = w;
        fb.height = h;
        fb.layers = 1;
        checkMotion(vkCreateFramebuffer(device, &fb, nullptr, &framebuffer),
                    "vkCreateFramebuffer motion");
        width = w;
        height = h;
    }
    void initialize(const std::vector<uint32_t>& vert, const std::vector<uint32_t>& frag) {
        VkFormatProperties colorProps{}, depthProps{};
        vkGetPhysicalDeviceFormatProperties(owner.physical_, VK_FORMAT_R32G32_SFLOAT, &colorProps);
        vkGetPhysicalDeviceFormatProperties(owner.physical_, VK_FORMAT_D32_SFLOAT, &depthProps);
        const auto colorFeatures =
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        const auto depthFeatures =
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((colorProps.optimalTilingFeatures & colorFeatures) != colorFeatures ||
            (depthProps.optimalTilingFeatures & depthFeatures) != depthFeatures) {
            throw std::runtime_error(
                "Vulkan device does not support sampled RG32 motion and D32 depth");
        }
        VkAttachmentDescription attachments[2]{};
        attachments[0].format = VK_FORMAT_R32G32_SFLOAT;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        attachments[1].format = VK_FORMAT_D32_SFLOAT;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;
        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        dependencies[0].dstAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                       VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        dependencies[1].srcAccessMask =
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
        VkRenderPassCreateInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rp.attachmentCount = 2;
        rp.pAttachments = attachments;
        rp.subpassCount = 1;
        rp.pSubpasses = &subpass;
        rp.dependencyCount = 2;
        rp.pDependencies = dependencies;
        checkMotion(vkCreateRenderPass(device, &rp, nullptr, &renderPass),
                    "vkCreateRenderPass motion");
        VkDescriptorSetLayoutBinding binding{
            0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo set{};
        set.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        set.bindingCount = 1;
        set.pBindings = &binding;
        checkMotion(vkCreateDescriptorSetLayout(device, &set, nullptr, &setLayout),
                    "vkCreateDescriptorSetLayout motion");
        VkPipelineLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &setLayout;
        checkMotion(vkCreatePipelineLayout(device, &layout, nullptr, &pipelineLayout),
                    "vkCreatePipelineLayout motion");
        VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
        try {
            vs = motionShader(device, vert);
            fs = motionShader(device, frag);
            VkPipelineShaderStageCreateInfo stages[2]{};
            stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = vs;
            stages[0].pName = "main";
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = fs;
            stages[1].pName = "main";
            VkVertexInputBindingDescription bindings[]{
                {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX},
                {1, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}};
            VkVertexInputAttributeDescription attributes[]{
                {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
                {1, 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)}};
            VkPipelineVertexInputStateCreateInfo vertex{};
            vertex.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertex.vertexBindingDescriptionCount = 2;
            vertex.pVertexBindingDescriptions = bindings;
            vertex.vertexAttributeDescriptionCount = 2;
            vertex.pVertexAttributeDescriptions = attributes;
            VkPipelineInputAssemblyStateCreateInfo assembly{};
            assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo viewport{};
            viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport.viewportCount = viewport.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo raster{};
            raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            raster.polygonMode = VK_POLYGON_MODE_FILL;
            raster.lineWidth = 1;
            raster.cullMode = VK_CULL_MODE_BACK_BIT;
            VkPipelineMultisampleStateCreateInfo samples{};
            samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineDepthStencilStateCreateInfo depthState{};
            depthState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depthState.depthTestEnable = depthState.depthWriteEnable = VK_TRUE;
            depthState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            VkPipelineColorBlendAttachmentState blendAttachment{};
            blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT;
            VkPipelineColorBlendStateCreateInfo blend{};
            blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            blend.attachmentCount = 1;
            blend.pAttachments = &blendAttachment;
            VkDynamicState states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dynamic{};
            dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic.dynamicStateCount = 2;
            dynamic.pDynamicStates = states;
            VkGraphicsPipelineCreateInfo pipeline{};
            pipeline.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            pipeline.stageCount = 2;
            pipeline.pStages = stages;
            pipeline.pVertexInputState = &vertex;
            pipeline.pInputAssemblyState = &assembly;
            pipeline.pViewportState = &viewport;
            pipeline.pRasterizationState = &raster;
            pipeline.pMultisampleState = &samples;
            pipeline.pDepthStencilState = &depthState;
            pipeline.pColorBlendState = &blend;
            pipeline.pDynamicState = &dynamic;
            pipeline.layout = pipelineLayout;
            pipeline.renderPass = renderPass;
            for (unsigned i = 0; i < 2; ++i) {
                raster.frontFace = i ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
                checkMotion(vkCreateGraphicsPipelines(
                                device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &pipelines[i]),
                            "vkCreateGraphicsPipelines motion");
            }
        } catch (...) {
            if (fs) {
                vkDestroyShaderModule(device, fs, nullptr);
            }
            if (vs) {
                vkDestroyShaderModule(device, vs, nullptr);
            }
            throw;
        }
        vkDestroyShaderModule(device, fs, nullptr);
        vkDestroyShaderModule(device, vs, nullptr);
    }
    VulkanSceneSurface& owner;
    VkDevice device;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipelines[2]{};
    VkImage color = VK_NULL_HANDLE, depth = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory = VK_NULL_HANDLE, depthMemory = VK_NULL_HANDLE;
    VkImageView colorView = VK_NULL_HANDLE, depthView = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    uint32_t width = 0, height = 0;
    bool rendered = false;
};

void VulkanSceneSurface::enableMotionVectors(const std::vector<uint32_t>& vertex,
                                             const std::vector<uint32_t>& fragment) {
    if (failed_ || motion_) {
        throw std::runtime_error("Motion vectors unavailable or already initialized");
    }
    auto motion = std::make_unique<Motion>(*this);
    motion->initialize(vertex, fragment);
    motion_ = motion.release();
}
void VulkanSceneSurface::cleanupMotion() noexcept {
    delete motion_;
    motion_ = nullptr;
}
VkImage VulkanSceneSurface::motionImage() const {
    return motion_ && motion_->rendered ? motion_->color : VK_NULL_HANDLE;
}
VkImageView VulkanSceneSurface::motionImageView() const {
    return motion_ && motion_->rendered ? motion_->colorView : VK_NULL_HANDLE;
}
VkImage VulkanSceneSurface::motionDepthImage() const {
    return motion_ && motion_->rendered ? motion_->depth : VK_NULL_HANDLE;
}
VkImageView VulkanSceneSurface::motionDepthImageView() const {
    return motion_ && motion_->rendered ? motion_->depthView : VK_NULL_HANDLE;
}

void VulkanSceneSurface::renderMotionVectors(uint32_t width,
                                             uint32_t height,
                                             const std::vector<MotionDraw>& draws) {
    if (failed_ || !motion_ || (meshOwner_ && meshOwner_->failed_)) {
        throw std::runtime_error("Motion vector pass is not available");
    }
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical_, &props);
    if (!width || !height || width > 8192 || height > 8192 ||
        width > props.limits.maxImageDimension2D || height > props.limits.maxImageDimension2D ||
        width > props.limits.maxFramebufferWidth || height > props.limits.maxFramebufferHeight ||
        width > props.limits.maxViewportDimensions[0] ||
        height > props.limits.maxViewportDimensions[1] ||
        props.limits.maxUniformBufferRange < sizeof(MotionUniform)) {
        throw std::runtime_error("Invalid motion vector extent or device limits");
    }
    const auto& meshes = meshOwner_ ? meshOwner_->meshes_ : meshes_;
    for (const auto& draw : draws) {
        const auto previous =
            draw.previousMesh.id == UINT32_MAX ? draw.mesh.id : draw.previousMesh.id;
        if (draw.mesh.id >= meshes.size() || previous >= meshes.size() ||
            !meshes[draw.mesh.id].alive || !meshes[previous].alive || meshes[draw.mesh.id].lines ||
            meshes[previous].lines || meshes[draw.mesh.id].count != meshes[previous].count ||
            !finiteMatrix(draw.rasterViewProjectionModel) ||
            !finiteMatrix(draw.currentViewProjectionModel) ||
            !finiteMatrix(draw.previousViewProjectionModel)) {
            throw std::runtime_error("Invalid motion vector draw");
        }
    }
    const VkDeviceSize alignment =
        std::max<VkDeviceSize>(1, props.limits.minUniformBufferOffsetAlignment);
    const VkDeviceSize stride = (sizeof(MotionUniform) + alignment - 1) / alignment * alignment;
    if (draws.size() > (UINT32_MAX / stride) || draws.size() > 65536) {
        throw std::runtime_error("Too many motion vector draws");
    }
    Buffer uniforms{};
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    try {
        const VkDeviceSize size = stride * std::max<size_t>(1, draws.size());
        uniforms = makeBuffer(size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        std::vector<unsigned char> data(static_cast<size_t>(size), 0);
        for (size_t i = 0; i < draws.size(); ++i) {
            const auto& draw = draws[i];
            MotionUniform values{vulkanClipMatrix(draw.rasterViewProjectionModel),
                                 vulkanClipMatrix(draw.currentViewProjectionModel),
                                 vulkanClipMatrix(draw.previousViewProjectionModel)};
            std::memcpy(data.data() + stride * i, &values, sizeof(values));
        }
        upload(uniforms, data.data(), size);
        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1};
        VkDescriptorPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool.maxSets = 1;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &poolSize;
        checkMotion(vkCreateDescriptorPool(device_, &pool, nullptr, &descriptorPool),
                    "vkCreateDescriptorPool motion");
        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool = descriptorPool;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &motion_->setLayout;
        VkDescriptorSet descriptor = VK_NULL_HANDLE;
        checkMotion(vkAllocateDescriptorSets(device_, &alloc, &descriptor),
                    "vkAllocateDescriptorSets motion");
        VkDescriptorBufferInfo info{uniforms.buffer, 0, sizeof(MotionUniform)};
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptor;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        write.pBufferInfo = &info;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
        if (width != motion_->width || height != motion_->height) {
            motion_->resize(width, height);
        }
        VkCommandBuffer cmd = beginCommands();
        std::array<VkClearValue, 2> clear{};
        clear[1].depthStencil = {1, 0};
        VkRenderPassBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        begin.renderPass = motion_->renderPass;
        begin.framebuffer = motion_->framebuffer;
        begin.renderArea.extent = {width, height};
        begin.clearValueCount = static_cast<uint32_t>(clear.size());
        begin.pClearValues = clear.data();
        vkCmdBeginRenderPass(cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewport{0, 0, float(width), float(height), 0, 1};
        VkRect2D scissor{{0, 0}, {width, height}};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        for (size_t i = 0; i < draws.size(); ++i) {
            const auto& draw = draws[i];
            const auto& current = meshes[draw.mesh.id];
            const auto previous =
                draw.previousMesh.id == UINT32_MAX ? draw.mesh.id : draw.previousMesh.id;
            if (!current.count) {
                continue;
            }
            VkBuffer buffers[]{current.vertices.buffer, meshes[previous].vertices.buffer};
            VkDeviceSize offsets[]{0, 0};
            vkCmdBindPipeline(
                cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, motion_->pipelines[draw.mirrored ? 1 : 0]);
            const auto dynamicOffset = uint32_t(stride * i);
            vkCmdBindDescriptorSets(cmd,
                                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    motion_->pipelineLayout,
                                    0,
                                    1,
                                    &descriptor,
                                    1,
                                    &dynamicOffset);
            vkCmdBindVertexBuffers(cmd, 0, 2, buffers, offsets);
            vkCmdDraw(cmd, current.count, 1, 0, 0);
        }
        vkCmdEndRenderPass(cmd);
        submitCommands();
        motion_->rendered = true;
        vkDestroyDescriptorPool(device_, descriptorPool, nullptr);
        destroyBuffer(uniforms);
    } catch (...) {
        if (descriptorPool) {
            vkDestroyDescriptorPool(device_, descriptorPool, nullptr);
        }
        destroyBuffer(uniforms);
        failed_ = true;
        throw;
    }
}

std::vector<float> VulkanSceneSurface::readMotionVectors() {
    if (failed_ || !motion_ || !motion_->rendered) {
        throw std::runtime_error("No motion vectors rendered");
    }
    const VkDeviceSize size = VkDeviceSize(motion_->width) * motion_->height * 2 * sizeof(float);
    Buffer staging{};
    try {
        staging = makeBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        auto cmd = beginCommands();
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = motion_->color;
        barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &barrier);
        VkBufferImageCopy copy{};
        copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        copy.imageExtent = {motion_->width, motion_->height, 1};
        vkCmdCopyImageToBuffer(
            cmd, motion_->color, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buffer, 1, &copy);
        VkBufferMemoryBarrier host{};
        host.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        host.buffer = staging.buffer;
        host.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT,
                             0,
                             0,
                             nullptr,
                             1,
                             &host,
                             0,
                             nullptr);
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &barrier);
        submitCommands();
        std::vector<float> pixels(static_cast<size_t>(size / sizeof(float)));
        void* mapped = nullptr;
        checkMotion(vkMapMemory(device_, staging.memory, 0, VK_WHOLE_SIZE, 0, &mapped),
                    "vkMapMemory motion");
        if (!staging.coherent) {
            VkMappedMemoryRange range{};
            range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.memory = staging.memory;
            range.size = VK_WHOLE_SIZE;
            const auto result = vkInvalidateMappedMemoryRanges(device_, 1, &range);
            if (result != VK_SUCCESS) {
                vkUnmapMemory(device_, staging.memory);
                checkMotion(result, "vkInvalidateMappedMemoryRanges motion");
            }
        }
        std::memcpy(pixels.data(), mapped, static_cast<size_t>(size));
        vkUnmapMemory(device_, staging.memory);
        destroyBuffer(staging);
        return pixels;
    } catch (...) {
        destroyBuffer(staging);
        failed_ = true;
        throw;
    }
}
