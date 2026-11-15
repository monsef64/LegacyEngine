#include "rendering/vulkan_scene_surface.hpp"
#include "core/profiler.hpp"
#include "rendering/vulkan_clip.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed (VkResult " +
                                 std::to_string(static_cast<int>(result)) + ")");
    }
}
void meshBoundsData(const MeshData& mesh, Vec3& low, Vec3& high) {
    low = high = {};
    if (!mesh.vertices.empty()) {
        low = high = mesh.vertices.front().position;
    }
    for (const auto& vertex : mesh.vertices) {
        const auto p = vertex.position;
        low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
    }
}
VkShaderModule shaderModule(VkDevice device, const std::vector<uint32_t>& words) {
    if (words.empty() || words.front() != 0x07230203) {
        throw std::runtime_error("Invalid SPIR-V shader");
    }
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = words.size() * sizeof(uint32_t);
    info.pCode = words.data();
    VkShaderModule result = VK_NULL_HANDLE;
    check(vkCreateShaderModule(device, &info, nullptr, &result), "vkCreateShaderModule");
    return result;
}
} // namespace

VulkanSceneSurface::VulkanSceneSurface(VkPhysicalDevice physical,
                                       VkDevice device,
                                       VkQueue graphicsQueue,
                                       uint32_t queueFamily,
                                       const std::vector<uint32_t>& vertexShader,
                                       const std::vector<uint32_t>& fragmentShader,
                                       VulkanSceneSurface* meshOwner)
    : physical_(physical), device_(device), queue_(graphicsQueue), family_(queueFamily),
      meshOwner_(meshOwner) {
    if (!physical_ || !device_ || !queue_) {
        throw std::runtime_error("Invalid Vulkan scene device or queue");
    }
    if (meshOwner_ && (meshOwner_->meshOwner_ || meshOwner_->physical_ != physical_ ||
                       meshOwner_->device_ != device_ || meshOwner_->queue_ != queue_ ||
                       meshOwner_->family_ != family_ || meshOwner_->failed_)) {
        throw std::runtime_error("Invalid Vulkan scene mesh owner");
    }
    try {
        initialize(vertexShader, fragmentShader);
    } catch (...) {
        cleanup();
        throw;
    }
}
VulkanSceneSurface::~VulkanSceneSurface() {
    cleanup();
}

uint32_t VulkanSceneSurface::memoryType(uint32_t bits,
                                        VkMemoryPropertyFlags required,
                                        VkMemoryPropertyFlags preferred) const {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_, &properties);
    uint32_t fallback = UINT32_MAX;
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        auto flags = properties.memoryTypes[i].propertyFlags;
        if ((bits & (1u << i)) && (flags & required) == required) {
            if ((flags & preferred) == preferred) {
                return i;
            }
            if (fallback == UINT32_MAX) {
                fallback = i;
            }
        }
    }
    if (fallback == UINT32_MAX) {
        throw std::runtime_error("No compatible Vulkan memory type");
    }
    return fallback;
}
VulkanSceneSurface::Buffer VulkanSceneSurface::makeBuffer(VkDeviceSize size,
                                                          VkBufferUsageFlags usage) {
    Buffer buffer;
    try {
        VkBufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device_, &info, nullptr, &buffer.buffer), "vkCreateBuffer");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, buffer.buffer, &requirements);
        const auto type = memoryType(requirements.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(physical_, &properties);
        buffer.coherent = (properties.memoryTypes[type].propertyFlags &
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = requirements.size;
        alloc.memoryTypeIndex = type;
        check(vkAllocateMemory(device_, &alloc, nullptr, &buffer.memory), "vkAllocateMemory");
        check(vkBindBufferMemory(device_, buffer.buffer, buffer.memory, 0), "vkBindBufferMemory");
        return buffer;
    } catch (...) {
        destroyBuffer(buffer);
        throw;
    }
}
void VulkanSceneSurface::destroyBuffer(Buffer& buffer) noexcept {
    if (buffer.buffer) {
        vkDestroyBuffer(device_, buffer.buffer, nullptr);
    }
    if (buffer.memory) {
        vkFreeMemory(device_, buffer.memory, nullptr);
    }
    buffer = {};
}
void VulkanSceneSurface::upload(Buffer& buffer, const void* data, VkDeviceSize size) {
    void* mapped = nullptr;
    check(vkMapMemory(device_, buffer.memory, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory");
    std::memcpy(mapped, data, static_cast<size_t>(size));
    if (!buffer.coherent) {
        VkMappedMemoryRange range{};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = buffer.memory;
        range.size = VK_WHOLE_SIZE;
        const VkResult result = vkFlushMappedMemoryRanges(device_, 1, &range);
        vkUnmapMemory(device_, buffer.memory);
        check(result, "vkFlushMappedMemoryRanges");
    } else {
        vkUnmapMemory(device_, buffer.memory);
    }
}

void VulkanSceneSurface::initialize(const std::vector<uint32_t>& vertexShader,
                                    const std::vector<uint32_t>& fragmentShader) {
    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(physical_, &deviceProperties);
    if (deviceProperties.limits.maxPushConstantsSize < sizeof(VulkanShaderLayout::UnlitDraw)) {
        throw std::runtime_error("Insufficient Vulkan push constant size");
    }
    for (auto format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM}) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physical_, format, &properties);
        if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            depthFormat_ = format;
            break;
        }
    }
    if (depthFormat_ == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("No Vulkan depth attachment format");
    }
    VkFormatProperties colorProperties{};
    vkGetPhysicalDeviceFormatProperties(physical_, VK_FORMAT_R8G8B8A8_UNORM, &colorProperties);
    const auto features = VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                          VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((colorProperties.optimalTilingFeatures & features) != features) {
        throw std::runtime_error("Vulkan RGBA8 render/sampling format is unsupported");
    }

    VkCommandPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = family_;
    check(vkCreateCommandPool(device_, &pool, nullptr, &pool_), "vkCreateCommandPool");
    VkCommandBufferAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc.commandPool = pool_;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(device_, &alloc, &commands_), "vkAllocateCommandBuffers");

    std::array<VkAttachmentDescription, 2> attachments{};
    attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachments[1].format = depthFormat_;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;
    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask =
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependencies[0].dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    dependencies[0].dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask =
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT;
    VkRenderPassCreateInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = static_cast<uint32_t>(attachments.size());
    rp.pAttachments = attachments.data();
    rp.subpassCount = 1;
    rp.pSubpasses = &subpass;
    rp.dependencyCount = static_cast<uint32_t>(dependencies.size());
    rp.pDependencies = dependencies.data();
    check(vkCreateRenderPass(device_, &rp, nullptr, &renderPass_), "vkCreateRenderPass");

    VkPushConstantRange range{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                              0,
                              sizeof(VulkanShaderLayout::UnlitDraw)};
    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &range;
    check(vkCreatePipelineLayout(device_, &layout, nullptr, &pipelineLayout_),
          "vkCreatePipelineLayout");
    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod = 0;
    check(vkCreateSampler(device_, &sampler, nullptr, &sampler_), "vkCreateSampler");

    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
    try {
        vs = shaderModule(device_, vertexShader);
        fs = shaderModule(device_, fragmentShader);
        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs;
        stages[0].pName = "main";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs;
        stages[1].pName = "main";
        VkVertexInputBindingDescription vertexBinding{
            0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription vertexAttribute{
            0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)};
        VkPipelineVertexInputStateCreateInfo vertex{};
        vertex.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex.vertexBindingDescriptionCount = 1;
        vertex.pVertexBindingDescriptions = &vertexBinding;
        vertex.vertexAttributeDescriptionCount = 1;
        vertex.pVertexAttributeDescriptions = &vertexAttribute;
        VkPipelineInputAssemblyStateCreateInfo assembly{};
        assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        VkPipelineViewportStateCreateInfo viewport{};
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo samples{};
        samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
        // Match the existing editor renderer: transparent surfaces blend RGB
        // but leave the target alpha unchanged.
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1;
        blend.pAttachments = &blendAttachment;
        const VkDynamicState states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = states;
        VkGraphicsPipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount = 2;
        info.pStages = stages.data();
        info.pVertexInputState = &vertex;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &samples;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = pipelineLayout_;
        info.renderPass = renderPass_;
        // Triangle normal/mirrored and lines each have opaque/transparent
        // variants; the final line pipeline draws blended overlays through depth.
        for (unsigned i = 0; i < 7; ++i) {
            const bool lines = i >= 4, transparent = i % 2 == 1 || i == 6;
            assembly.topology =
                lines ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            raster.cullMode = lines ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
            raster.frontFace =
                i >= 2 && !lines ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
            depth.depthTestEnable = i == 6 ? VK_FALSE : VK_TRUE;
            depth.depthWriteEnable = transparent ? VK_FALSE : VK_TRUE;
            blendAttachment.blendEnable = transparent ? VK_TRUE : VK_FALSE;
            check(vkCreateGraphicsPipelines(
                      device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipelines_[i]),
                  "vkCreateGraphicsPipelines");
        }
    } catch (...) {
        if (fs) {
            vkDestroyShaderModule(device_, fs, nullptr);
        }
        if (vs) {
            vkDestroyShaderModule(device_, vs, nullptr);
        }
        throw;
    }
    vkDestroyShaderModule(device_, fs, nullptr);
    vkDestroyShaderModule(device_, vs, nullptr);
}

void VulkanSceneSurface::cleanupLighting() noexcept {
    cleanupShadows();
    for (auto& pipeline : litPipelines_) {
        if (pipeline) {
            vkDestroyPipeline(device_, pipeline, nullptr);
        }
        pipeline = VK_NULL_HANDLE;
    }
    if (litPipelineLayout_) {
        vkDestroyPipelineLayout(device_, litPipelineLayout_, nullptr);
    }
    litPipelineLayout_ = VK_NULL_HANDLE;
    for (auto& layout : litSetLayouts_) {
        if (layout) {
            vkDestroyDescriptorSetLayout(device_, layout, nullptr);
        }
        layout = VK_NULL_HANDLE;
    }
    white_.reset();
    normal_.reset();
    checker_.reset();
}

void VulkanSceneSurface::enableLighting(const std::vector<uint32_t>& vertexShader,
                                        const std::vector<uint32_t>& fragmentShader,
                                        const std::vector<uint32_t>& shadowVertexShader) {
    if (failed_ || litPipelineLayout_) {
        throw std::runtime_error("Vulkan lighting already initialized or surface failed");
    }
    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
    try {
        VkDescriptorSetLayoutBinding object{};
        object.binding = 0;
        object.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        object.descriptorCount = 1;
        object.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        VkDescriptorSetLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout.bindingCount = 1;
        layout.pBindings = &object;
        check(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &litSetLayouts_[0]),
              "vkCreateDescriptorSetLayout object");
        std::array<VkDescriptorSetLayoutBinding, 5> material{};
        for (uint32_t i = 0; i < 4; ++i) {
            material[i].binding = i;
            material[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            material[i].descriptorCount = 1;
            material[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        material[4].binding = 4;
        material[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        material[4].descriptorCount = 1;
        material[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        layout.bindingCount = static_cast<uint32_t>(material.size());
        layout.pBindings = material.data();
        check(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &litSetLayouts_[1]),
              "vkCreateDescriptorSetLayout material");
        VkPipelineLayoutCreateInfo pipelineLayout{};
        pipelineLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayout.setLayoutCount = 2;
        pipelineLayout.pSetLayouts = litSetLayouts_;
        check(vkCreatePipelineLayout(device_, &pipelineLayout, nullptr, &litPipelineLayout_),
              "vkCreatePipelineLayout lit");
        vs = shaderModule(device_, vertexShader);
        fs = shaderModule(device_, fragmentShader);
        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs;
        stages[0].pName = "main";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs;
        stages[1].pName = "main";
        VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        std::array<VkVertexInputAttributeDescription, 3> attributes{
            {{0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
             {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
             {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, u)}}};
        VkPipelineVertexInputStateCreateInfo vertex{};
        vertex.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertex.vertexBindingDescriptionCount = 1;
        vertex.pVertexBindingDescriptions = &binding;
        vertex.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
        vertex.pVertexAttributeDescriptions = attributes.data();
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
        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        attachment.colorBlendOp = VK_BLEND_OP_ADD;
        attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        attachment.alphaBlendOp = VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1;
        blend.pAttachments = &attachment;
        const VkDynamicState states[]{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = states;
        VkGraphicsPipelineCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        info.stageCount = 2;
        info.pStages = stages.data();
        info.pVertexInputState = &vertex;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &samples;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend;
        info.pDynamicState = &dynamic;
        info.layout = litPipelineLayout_;
        info.renderPass = renderPass_;
        for (unsigned i = 0; i < 4; ++i) {
            raster.frontFace = i >= 2 ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
            const bool transparent = i % 2 != 0;
            depth.depthWriteEnable = transparent ? VK_FALSE : VK_TRUE;
            attachment.blendEnable = transparent ? VK_TRUE : VK_FALSE;
            check(vkCreateGraphicsPipelines(
                      device_, VK_NULL_HANDLE, 1, &info, nullptr, &litPipelines_[i]),
                  "vkCreateGraphicsPipelines lit");
        }
        vkDestroyShaderModule(device_, fs, nullptr);
        fs = VK_NULL_HANDLE;
        vkDestroyShaderModule(device_, vs, nullptr);
        vs = VK_NULL_HANDLE;
        white_ = std::make_unique<VulkanTexture>(physical_,
                                                 device_,
                                                 queue_,
                                                 family_,
                                                 1,
                                                 1,
                                                 std::vector<uint8_t>{255, 255, 255, 255},
                                                 VulkanTexture::Address::Repeat);
        normal_ = std::make_unique<VulkanTexture>(physical_,
                                                  device_,
                                                  queue_,
                                                  family_,
                                                  1,
                                                  1,
                                                  std::vector<uint8_t>{128, 128, 255, 255},
                                                  VulkanTexture::Address::Repeat);
        checker_ = std::make_unique<VulkanTexture>(
            physical_,
            device_,
            queue_,
            family_,
            2,
            2,
            std::vector<uint8_t>{
                0, 0, 0, 255, 255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 255},
            VulkanTexture::Address::Repeat,
            true,
            true);
        if (!shadowVertexShader.empty()) {
            initializeShadows(shadowVertexShader);
        }
    } catch (...) {
        if (fs) {
            vkDestroyShaderModule(device_, fs, nullptr);
        }
        if (vs) {
            vkDestroyShaderModule(device_, vs, nullptr);
        }
        cleanupLighting();
        throw;
    }
}

void VulkanSceneSurface::destroyLightingFrame(LightingFrame& frame) noexcept {
    if (frame.pool) {
        vkDestroyDescriptorPool(device_, frame.pool, nullptr);
    }
    frame.pool = VK_NULL_HANDLE;
    destroyBuffer(frame.uniforms);
}
VulkanSceneSurface::LightingFrame
VulkanSceneSurface::prepareLighting(const std::vector<Draw>& draws,
                                    const VulkanShaderLayout::Lighting& lighting,
                                    bool softShadows) {
    LightingFrame frame{};
    std::size_t count = 0;
    for (const auto& d : draws) {
        if (d.lit) {
            ++count;
        }
    }
    if (!count) {
        return frame;
    }
    if (count > 4096 || lighting.lightCount < 0 || lighting.lightCount > 16) {
        throw std::runtime_error("Invalid Vulkan lit draw or light count");
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_, &properties);
    if (properties.limits.maxUniformBufferRange < sizeof(VulkanShaderLayout::Lighting)) {
        throw std::runtime_error("Vulkan uniform range is too small for lighting");
    }
    const VkDeviceSize alignment =
        std::max<VkDeviceSize>(1, properties.limits.minUniformBufferOffsetAlignment);
    constexpr VkDeviceSize maxUniformBytes = 256ull * 1024 * 1024;
    if (alignment > maxUniformBytes / 2) {
        throw std::runtime_error("Vulkan uniform alignment is too large");
    }
    auto align = [alignment](VkDeviceSize size) {
        return (size + alignment - 1) / alignment * alignment;
    };
    const VkDeviceSize lightOffset = align(sizeof(VulkanShaderLayout::Object));
    const VkDeviceSize stride = lightOffset + align(sizeof(VulkanShaderLayout::Lighting));
    if (stride > maxUniformBytes / count ||
        stride * count > std::numeric_limits<std::size_t>::max()) {
        throw std::runtime_error("Vulkan lighting uniforms exceed the frame budget");
    }
    try {
        frame.uniforms = makeBuffer(stride * count, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        VkDescriptorPoolSize sizes[]{
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, static_cast<uint32_t>(count * 2)},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, static_cast<uint32_t>(count * 4)}};
        VkDescriptorPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool.maxSets = static_cast<uint32_t>(count * 2);
        pool.poolSizeCount = 2;
        pool.pPoolSizes = sizes;
        check(vkCreateDescriptorPool(device_, &pool, nullptr, &frame.pool),
              "vkCreateDescriptorPool lighting");
        std::vector<VkDescriptorSetLayout> layouts;
        layouts.reserve(count * 2);
        for (std::size_t i = 0; i < count; ++i) {
            layouts.push_back(litSetLayouts_[0]);
            layouts.push_back(litSetLayouts_[1]);
        }
        std::vector<VkDescriptorSet> sets(count * 2);
        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool = frame.pool;
        alloc.descriptorSetCount = static_cast<uint32_t>(sets.size());
        alloc.pSetLayouts = layouts.data();
        check(vkAllocateDescriptorSets(device_, &alloc, sets.data()),
              "vkAllocateDescriptorSets lighting");
        frame.objects.resize(draws.size());
        frame.materials.resize(draws.size());
        std::vector<uint8_t> uniformData(static_cast<std::size_t>(stride * count), 0);
        std::size_t n = 0;
        for (std::size_t i = 0; i < draws.size(); ++i) {
            const auto& draw = draws[i];
            if (!draw.lit) {
                continue;
            }
            const VkDeviceSize base = stride * n;
            VulkanShaderLayout::Object object{
                vulkanClipMatrix(draw.viewProjectionModel), draw.model, draw.normalMatrix};
            auto material = lighting;
            material.cameraPositionAndSpecular.w = draw.specular;
            material.colorAndNormalStrength = {
                draw.color.x, draw.color.y, draw.color.z, draw.normalStrength};
            material.tiling[0] = draw.tilingX;
            material.tiling[1] = draw.tilingY;
            material.hasMaterialTexture = draw.materialTexture != nullptr;
            material.bumped = draw.normalTexture != nullptr;
            material.transparent = draw.transparent;
            material.checkerboard = draw.checkerboard;
            std::memcpy(uniformData.data() + base, &object, sizeof(object));
            std::memcpy(uniformData.data() + base + lightOffset, &material, sizeof(material));
            frame.objects[i] = sets[n * 2];
            frame.materials[i] = sets[n * 2 + 1];
            VkDescriptorBufferInfo buffers[]{
                {frame.uniforms.buffer, base, sizeof(object)},
                {frame.uniforms.buffer, base + lightOffset, sizeof(material)}};
            const VulkanTexture* textures[]{draw.materialTexture ? draw.materialTexture
                                                                 : white_.get(),
                                            draw.normalTexture ? draw.normalTexture : normal_.get(),
                                            checker_.get()};
            VkDescriptorImageInfo images[3]{};
            for (int t = 0; t < 3; ++t) {
                images[t] = {
                    textures[t]->sampler(), textures[t]->imageView(), textures[t]->imageLayout()};
            }
            VkDescriptorImageInfo shadowImage{shadowSamplers_[softShadows ? 1 : 0],
                                              shadowArrayView_,
                                              VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
            VkWriteDescriptorSet writes[6]{};
            for (auto& write : writes) {
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.descriptorCount = 1;
            }
            writes[0].dstSet = frame.objects[i];
            writes[0].dstBinding = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[0].pBufferInfo = &buffers[0];
            for (int t = 0; t < 3; ++t) {
                writes[t + 1].dstSet = frame.materials[i];
                writes[t + 1].dstBinding = t;
                writes[t + 1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[t + 1].pImageInfo = &images[t];
            }
            writes[4].dstSet = frame.materials[i];
            writes[4].dstBinding = 4;
            writes[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[4].pBufferInfo = &buffers[1];
            if (supportsShadows()) {
                writes[5].dstSet = frame.materials[i];
                writes[5].dstBinding = 3;
                writes[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[5].pImageInfo = &shadowImage;
            }
            vkUpdateDescriptorSets(device_, supportsShadows() ? 6 : 5, writes, 0, nullptr);
            ++n;
        }
        upload(frame.uniforms, uniformData.data(), stride * count);
        return frame;
    } catch (...) {
        destroyLightingFrame(frame);
        throw;
    }
}

MeshHandle VulkanSceneSurface::createMesh(const MeshData& mesh) {
    if (failed_) {
        throw std::runtime_error("Vulkan scene surface cannot be reused after an error");
    }
    if (meshOwner_) {
        return meshOwner_->createMesh(mesh);
    }
    if (mesh.vertices.size() > UINT32_MAX || meshes_.size() >= UINT32_MAX) {
        throw std::runtime_error("Vulkan mesh is too large");
    }
    const auto count = static_cast<uint32_t>(mesh.vertices.size());
    Mesh stored{};
    stored.count = count;
    stored.revision = ++nextMeshRevision_;
    stored.lines = mesh.topology == MeshData::Topology::Lines;
    stored.alive = true;
    meshBoundsData(mesh, stored.low, stored.high);
    try {
        if (count) {
            VkDeviceSize size = VkDeviceSize(count) * sizeof(Vertex);
            stored.vertices = makeBuffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
            upload(stored.vertices, mesh.vertices.data(), size);
        }
        const MeshHandle result{static_cast<uint32_t>(meshes_.size())};
        meshes_.push_back(stored);
        return result;
    } catch (...) {
        destroyBuffer(stored.vertices);
        throw;
    }
}

void VulkanSceneSurface::updateMesh(MeshHandle handle, const MeshData& mesh) {
    if (failed_) {
        throw std::runtime_error("Vulkan scene surface cannot be reused after an error");
    }
    if (meshOwner_) {
        meshOwner_->updateMesh(handle, mesh);
        return;
    }
    if (handle.id >= meshes_.size() || !meshes_[handle.id].alive ||
        mesh.vertices.size() > UINT32_MAX) {
        throw std::runtime_error("Invalid Vulkan mesh handle or size");
    }
    Buffer replacement{};
    try {
        check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
        auto& current = meshes_[handle.id];
        const auto count = static_cast<uint32_t>(mesh.vertices.size());
        if (count != current.count) {
            if (count) {
                replacement = makeBuffer(VkDeviceSize(count) * sizeof(Vertex),
                                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
                upload(replacement, mesh.vertices.data(), VkDeviceSize(count) * sizeof(Vertex));
            }
            destroyBuffer(current.vertices);
            current.vertices = replacement;
            replacement = {};
        } else if (count) {
            upload(current.vertices, mesh.vertices.data(), VkDeviceSize(count) * sizeof(Vertex));
        }
        current.count = count;
        current.revision = ++nextMeshRevision_;
        current.lines = mesh.topology == MeshData::Topology::Lines;
        meshBoundsData(mesh, current.low, current.high);
    } catch (...) {
        destroyBuffer(replacement);
        failed_ = true;
        throw;
    }
}

bool VulkanSceneSurface::meshIsLines(MeshHandle handle) const {
    const auto& meshes = meshOwner_ ? meshOwner_->meshes_ : meshes_;
    if (handle.id >= meshes.size() || !meshes[handle.id].alive) {
        throw std::runtime_error("Invalid Vulkan mesh handle");
    }
    return meshes[handle.id].lines;
}

uint64_t VulkanSceneSurface::meshRevision(MeshHandle handle) const {
    const auto& meshes = meshOwner_ ? meshOwner_->meshes_ : meshes_;
    if (handle.id >= meshes.size() || !meshes[handle.id].alive) {
        throw std::runtime_error("Invalid Vulkan mesh handle");
    }
    return meshes[handle.id].revision;
}

bool VulkanSceneSurface::meshBounds(MeshHandle handle, Vec3& low, Vec3& high) const {
    const auto& meshes = meshOwner_ ? meshOwner_->meshes_ : meshes_;
    if (handle.id >= meshes.size() || !meshes[handle.id].alive || !meshes[handle.id].count) {
        return false;
    }
    low = meshes[handle.id].low;
    high = meshes[handle.id].high;
    return true;
}

void VulkanSceneSurface::destroyMesh(MeshHandle handle) {
    if (failed_) {
        throw std::runtime_error("Vulkan scene surface cannot be reused after an error");
    }
    if (meshOwner_) {
        meshOwner_->destroyMesh(handle);
        return;
    }
    if (handle.id >= meshes_.size() || !meshes_[handle.id].alive) {
        throw std::runtime_error("Invalid Vulkan mesh handle");
    }
    try {
        // Other surfaces on this device may have used the shared geometry.
        check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
        destroyBuffer(meshes_[handle.id].vertices);
        meshes_[handle.id] = {};
    } catch (...) {
        failed_ = true;
        throw;
    }
}

void VulkanSceneSurface::resize(uint32_t width, uint32_t height) {
    check(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
    destroyImages();
    auto createImage = [&](VkFormat format,
                           VkImageUsageFlags usage,
                           VkImageAspectFlags aspect,
                           VkImage& image,
                           VkDeviceMemory& memory,
                           VkImageView& view) {
        VkImageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = format;
        info.extent = {width, height, 1};
        info.mipLevels = info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        check(vkCreateImage(device_, &info, nullptr, &image), "vkCreateImage");
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device_, image, &requirements);
        VkMemoryAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc.allocationSize = requirements.size;
        alloc.memoryTypeIndex =
            memoryType(requirements.memoryTypeBits, 0, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device_, &alloc, nullptr, &memory), "vkAllocateMemory image");
        check(vkBindImageMemory(device_, image, memory, 0), "vkBindImageMemory");
        VkImageViewCreateInfo vi{};
        vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        vi.image = image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format;
        vi.subresourceRange.aspectMask = aspect;
        vi.subresourceRange.levelCount = vi.subresourceRange.layerCount = 1;
        check(vkCreateImageView(device_, &vi, nullptr, &view), "vkCreateImageView");
    };
    createImage(VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                    (transferDestination_ ? VK_IMAGE_USAGE_TRANSFER_DST_BIT : 0),
                VK_IMAGE_ASPECT_COLOR_BIT,
                color_,
                colorMemory_,
                colorView_);
    createImage(depthFormat_,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT,
                depth_,
                depthMemory_,
                depthView_);
    const VkImageView attachments[]{colorView_, depthView_};
    VkFramebufferCreateInfo fb{};
    fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fb.renderPass = renderPass_;
    fb.attachmentCount = 2;
    fb.pAttachments = attachments;
    fb.width = width;
    fb.height = height;
    fb.layers = 1;
    check(vkCreateFramebuffer(device_, &fb, nullptr, &framebuffer_), "vkCreateFramebuffer");
    width_ = width;
    height_ = height;
    ++imageGeneration_;
}
VkCommandBuffer VulkanSceneSurface::beginCommands() {
    check(vkResetCommandBuffer(commands_, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(commands_, &begin), "vkBeginCommandBuffer");
    return commands_;
}
void VulkanSceneSurface::submitCommands() {
    check(vkEndCommandBuffer(commands_), "vkEndCommandBuffer");
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commands_;
    check(vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE), "vkQueueSubmit");
    check(vkQueueWaitIdle(queue_), "vkQueueWaitIdle");
}
void VulkanSceneSurface::render(uint32_t width,
                                uint32_t height,
                                const std::vector<Draw>& draws,
                                VulkanShaderLayout::Float4 clear,
                                const VulkanShaderLayout::Lighting* lighting,
                                const ShadowPlan* shadows,
                                const std::vector<SkyDraw>& skies,
                                const RenderRegion* region) {
    if (failed_) {
        throw std::runtime_error("Vulkan scene surface cannot be reused after an error");
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physical_, &properties);
    if (!width || !height || width > 8192 || height > 8192 ||
        width > properties.limits.maxImageDimension2D ||
        height > properties.limits.maxImageDimension2D) {
        throw std::runtime_error("Invalid Vulkan scene extent");
    }
    if (meshOwner_ && meshOwner_->failed_) {
        throw std::runtime_error("Vulkan scene mesh owner cannot be reused after an error");
    }
    if (region && (!region->width || !region->height || region->x >= width || region->y >= height ||
                   region->width > width - region->x || region->height > height - region->y)) {
        throw std::runtime_error("Invalid Vulkan viewport rectangle");
    }
    const RenderRegion rect = region ? *region : RenderRegion{0, 0, width, height};
    if (rect.width > properties.limits.maxViewportDimensions[0] ||
        rect.height > properties.limits.maxViewportDimensions[1] ||
        float(rect.x) < properties.limits.viewportBoundsRange[0] ||
        float(rect.y) < properties.limits.viewportBoundsRange[0] ||
        float(rect.x + rect.width) > properties.limits.viewportBoundsRange[1] ||
        float(rect.y + rect.height) > properties.limits.viewportBoundsRange[1]) {
        throw std::runtime_error("Vulkan viewport exceeds device limits");
    }
    const auto& meshes = meshOwner_ ? meshOwner_->meshes_ : meshes_;
    for (const auto& draw : draws) {
        if (draw.mesh.id >= meshes.size() || !meshes[draw.mesh.id].alive) {
            throw std::runtime_error("Invalid Vulkan mesh handle");
        }
        if (draw.lit && (!litPipelineLayout_ || !lighting || meshes[draw.mesh.id].lines)) {
            throw std::runtime_error("Vulkan lit draw requires lighting and triangle geometry");
        }
        if (draw.depthAlways && (!meshes[draw.mesh.id].lines || draw.lit || !draw.transparent)) {
            throw std::runtime_error(
                "Vulkan depth-independent overlays require transparent unlit lines");
        }
    }
    if (shadows && (!supportsShadows() || !lighting || !shadows->atlasResolution)) {
        throw std::runtime_error("Vulkan shadow plan requires a shadow-capable lit pipeline");
    }
    if (skies.size() > 2 || (!skies.empty() && !supportsSkybox())) {
        throw std::runtime_error("Invalid Vulkan skybox draw count or pipeline");
    }
    for (const auto& sky : skies) {
        if ((sky.texture && sky.texture->cubemap() == sky.hdr) ||
            !std::isfinite(sky.tintOpacity.x) || !std::isfinite(sky.tintOpacity.y) ||
            !std::isfinite(sky.tintOpacity.z) || !std::isfinite(sky.tintOpacity.w) ||
            sky.tintOpacity.w < 0 || sky.tintOpacity.w > 1) {
            throw std::runtime_error("Invalid Vulkan skybox parameters");
        }
    }
    VulkanShaderLayout::Lighting effectiveLighting{};
    if (lighting) {
        effectiveLighting = *lighting;
        if (lighting->lightCount < 0 || lighting->lightCount > 16) {
            throw std::runtime_error("Invalid Vulkan light count");
        }
        if (!shadows) {
            for (int i = 0; i < effectiveLighting.lightCount; ++i) {
                effectiveLighting.lightShadow[i].x = -1;
            }
        } else {
            effectiveLighting.softShadows = shadows->soft ? 1 : 0;
            for (std::size_t index = 0; index < shadows->layers.size(); ++index) {
                const auto& layer = shadows->layers[index];
                if (layer.resolution > shadows->atlasResolution) {
                    throw std::runtime_error("Invalid Vulkan shadow layer resolution");
                }
                if (layer.resolution) {
                    for (float value : layer.viewProjection.m) {
                        if (!std::isfinite(value)) {
                            throw std::runtime_error("Invalid Vulkan shadow matrix");
                        }
                    }
                    effectiveLighting.shadowMatrix[index] = vulkanClipMatrix(layer.viewProjection);
                }
            }
            for (int i = 0; i < effectiveLighting.lightCount; ++i) {
                const float baseValue = effectiveLighting.lightShadow[i].x;
                if (!std::isfinite(baseValue)) {
                    throw std::runtime_error("Invalid Vulkan shadow index");
                }
                if (baseValue < 0) {
                    continue;
                }
                if (baseValue >= 12 || std::floor(baseValue) != baseValue) {
                    throw std::runtime_error("Invalid Vulkan shadow index");
                }
                const int base = int(baseValue);
                const float typeValue = effectiveLighting.lightPosition[i].w;
                if (!std::isfinite(typeValue) || typeValue < 0 || typeValue > 2 ||
                    std::floor(typeValue) != typeValue) {
                    throw std::runtime_error("Invalid Vulkan shadow light type");
                }
                const int type = int(typeValue);
                const int cascadeCount = effectiveLighting.cascadeCount[i].x;
                if (type < 0 || type > 2 || (type == 0 && cascadeCount != 1 && cascadeCount != 4)) {
                    throw std::runtime_error("Invalid Vulkan shadow light type or cascade count");
                }
                const int count = type == 1 ? 6 : (type == 0 ? cascadeCount : 1);
                const float scale = effectiveLighting.lightShadow[i].w;
                if (base + count > 12 || !std::isfinite(scale) || scale <= 0 || scale > 1) {
                    throw std::runtime_error("Invalid Vulkan shadow layer range or scale");
                }
                for (int layer = base; layer < base + count; ++layer) {
                    if (!shadows->layers[layer].resolution ||
                        std::abs(float(shadows->layers[layer].resolution) /
                                     shadows->atlasResolution -
                                 scale) > 1e-5f) {
                        throw std::runtime_error(
                            "Vulkan shadow descriptor does not match layer plan");
                    }
                }
            }
        }
    }
    LightingFrame frame{};
    SkyFrame skyFrame{};
    try {
        {
            ProfileScope lightProfile(ProfilePart::Lights);
            if (shadows) {
                ensureShadowAtlas(shadows->atlasResolution);
            }
            if (lighting) {
                frame = prepareLighting(draws, effectiveLighting, shadows && shadows->soft);
            }
        }
        {
            ProfileScope skyProfile(ProfilePart::Skybox);
            skyFrame = prepareSky(skies);
        }
        if (width != width_ || height != height_) {
            resize(width, height);
        }
        auto cmd = beginCommands();
        if (shadows) {
            ProfileScope lightProfile(ProfilePart::Lights);
            recordShadows(cmd, draws, *shadows);
        }
        std::array<VkClearValue, 2> values{};
        values[0].color.float32[0] = clear.x;
        values[0].color.float32[1] = clear.y;
        values[0].color.float32[2] = clear.z;
        values[0].color.float32[3] = clear.w;
        values[1].depthStencil = {1.0f, 0};
        VkRenderPassBeginInfo rp{};
        rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rp.renderPass = renderPass_;
        rp.framebuffer = framebuffer_;
        rp.renderArea.extent = {width, height};
        rp.clearValueCount = static_cast<uint32_t>(values.size());
        rp.pClearValues = values.data();
        vkCmdBeginRenderPass(cmd, &rp, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport viewport{static_cast<float>(rect.x),
                            static_cast<float>(rect.y),
                            static_cast<float>(rect.width),
                            static_cast<float>(rect.height),
                            0,
                            1};
        VkRect2D scissor{{static_cast<int32_t>(rect.x), static_cast<int32_t>(rect.y)},
                         {rect.width, rect.height}};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        if (!skies.empty()) {
            ProfileScope skyProfile(ProfilePart::Skybox);
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline_);
            for (const auto set : skyFrame.sets) {
                vkCmdBindDescriptorSets(cmd,
                                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        skyPipelineLayout_,
                                        0,
                                        1,
                                        &set,
                                        0,
                                        nullptr);
                vkCmdDraw(cmd, 3, 1, 0, 0);
            }
        }
        {
            ProfileScope surfacesProfile(ProfilePart::Surfaces);
            for (std::size_t drawIndex = 0; drawIndex < draws.size(); ++drawIndex) {
                const auto& draw = draws[drawIndex];
                const Mesh& mesh = meshes[draw.mesh.id];
                if (!mesh.count) {
                    continue;
                }
                const unsigned pipeline = draw.depthAlways ? 6
                                                           : (mesh.lines      ? 4
                                                              : draw.mirrored ? 2
                                                                              : 0) +
                                                                 (draw.transparent ? 1 : 0);
                vkCmdBindPipeline(cmd,
                                  VK_PIPELINE_BIND_POINT_GRAPHICS,
                                  draw.lit ? litPipelines_[pipeline] : pipelines_[pipeline]);
                const VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(cmd, 0, 1, &mesh.vertices.buffer, &offset);
                if (draw.lit) {
                    const VkDescriptorSet sets[]{frame.objects[drawIndex],
                                                 frame.materials[drawIndex]};
                    vkCmdBindDescriptorSets(cmd,
                                            VK_PIPELINE_BIND_POINT_GRAPHICS,
                                            litPipelineLayout_,
                                            0,
                                            2,
                                            sets,
                                            0,
                                            nullptr);
                } else {
                    const VulkanShaderLayout::UnlitDraw push{
                        vulkanClipMatrix(draw.viewProjectionModel), draw.color};
                    vkCmdPushConstants(cmd,
                                       pipelineLayout_,
                                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                       0,
                                       sizeof(push),
                                       &push);
                }
                vkCmdDraw(cmd, mesh.count, 1, 0, 0);
            }
            vkCmdEndRenderPass(cmd);
            submitCommands();
        }
        destroySkyFrame(skyFrame);
        destroyLightingFrame(frame);
        rendered_ = true;
    } catch (...) {
        // A failure after submission may leave resources in use.
        vkDeviceWaitIdle(device_);
        destroySkyFrame(skyFrame);
        destroyLightingFrame(frame);
        failed_ = true;
        throw;
    }
}
void VulkanSceneSurface::enableTransferDestination() {
    if (failed_ || color_ || rendered_) {
        throw std::runtime_error("Transfer destination must be enabled before rendering");
    }
    transferDestination_ = true;
}
void VulkanSceneSurface::prepare(uint32_t width, uint32_t height) {
    if (failed_) {
        throw std::runtime_error("Vulkan scene surface cannot be reused after an error");
    }
    if (!rendered_ || width != width_ || height != height_) {
        render(width, height, {});
    }
}
std::vector<uint8_t> VulkanSceneSurface::readPixels() {
    if (failed_ || !rendered_) {
        throw std::runtime_error("Vulkan scene surface has not rendered");
    }
    Buffer staging{};
    try {
        const VkDeviceSize size = VkDeviceSize(width_) * height_ * 4;
        staging = makeBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        auto cmd = beginCommands();
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = color_;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = barrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &barrier);
        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {width_, height_, 1};
        vkCmdCopyImageToBuffer(
            cmd, color_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, staging.buffer, 1, &copy);
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             0,
                             0,
                             nullptr,
                             0,
                             nullptr,
                             1,
                             &barrier);
        VkMemoryBarrier host{};
        host.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        host.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        host.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT,
                             0,
                             1,
                             &host,
                             0,
                             nullptr,
                             0,
                             nullptr);
        submitCommands();
        std::vector<uint8_t> pixels(static_cast<size_t>(size));
        void* mapped = nullptr;
        check(vkMapMemory(device_, staging.memory, 0, VK_WHOLE_SIZE, 0, &mapped),
              "vkMapMemory readback");
        if (!staging.coherent) {
            VkMappedMemoryRange range{};
            range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.memory = staging.memory;
            range.size = VK_WHOLE_SIZE;
            VkResult result = vkInvalidateMappedMemoryRanges(device_, 1, &range);
            if (result != VK_SUCCESS) {
                vkUnmapMemory(device_, staging.memory);
                check(result, "vkInvalidateMappedMemoryRanges");
            }
        }
        std::memcpy(pixels.data(), mapped, pixels.size());
        vkUnmapMemory(device_, staging.memory);
        destroyBuffer(staging);
        return pixels;
    } catch (...) {
        destroyBuffer(staging);
        failed_ = true;
        throw;
    }
}
void VulkanSceneSurface::destroyImages() noexcept {
    if (framebuffer_) {
        vkDestroyFramebuffer(device_, framebuffer_, nullptr);
    }
    if (depthView_) {
        vkDestroyImageView(device_, depthView_, nullptr);
    }
    if (colorView_) {
        vkDestroyImageView(device_, colorView_, nullptr);
    }
    if (depth_) {
        vkDestroyImage(device_, depth_, nullptr);
    }
    if (color_) {
        vkDestroyImage(device_, color_, nullptr);
    }
    if (depthMemory_) {
        vkFreeMemory(device_, depthMemory_, nullptr);
    }
    if (colorMemory_) {
        vkFreeMemory(device_, colorMemory_, nullptr);
    }
    framebuffer_ = VK_NULL_HANDLE;
    depthView_ = colorView_ = VK_NULL_HANDLE;
    depth_ = color_ = VK_NULL_HANDLE;
    depthMemory_ = colorMemory_ = VK_NULL_HANDLE;
    width_ = height_ = 0;
    rendered_ = false;
}
void VulkanSceneSurface::cleanup() noexcept {
    if (!device_) {
        return;
    }
    vkDeviceWaitIdle(device_);
    destroyImages();
    cleanupMotion();
    cleanupSky();
    cleanupLighting();
    for (auto& mesh : meshes_) {
        destroyBuffer(mesh.vertices);
    }
    for (auto pipeline : pipelines_) {
        if (pipeline) {
            vkDestroyPipeline(device_, pipeline, nullptr);
        }
    }
    if (sampler_) {
        vkDestroySampler(device_, sampler_, nullptr);
    }
    if (pipelineLayout_) {
        vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    }
    if (renderPass_) {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
    }
    if (pool_) {
        vkDestroyCommandPool(device_, pool_, nullptr);
    }
}
