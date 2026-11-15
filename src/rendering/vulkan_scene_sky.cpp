#include "rendering/vulkan_clip.hpp"
#include "rendering/vulkan_scene_surface.hpp"
#include "scene/scene_camera.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>

namespace {
void checkSky(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed (VkResult " +
                                 std::to_string(int(result)) + ")");
    }
}
VkShaderModule skyShader(VkDevice device, const std::vector<uint32_t>& words) {
    if (words.empty() || words.front() != 0x07230203) {
        throw std::runtime_error("Invalid skybox SPIR-V");
    }
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = words.size() * sizeof(uint32_t);
    info.pCode = words.data();
    VkShaderModule shader = VK_NULL_HANDLE;
    checkSky(vkCreateShaderModule(device, &info, nullptr, &shader), "vkCreateShaderModule sky");
    return shader;
}
} // namespace
void VulkanSceneSurface::enableSkybox(const std::vector<uint32_t>& vertexShader,
                                      const std::vector<uint32_t>& fragmentShader) {
    if (failed_ || skyPipeline_) {
        throw std::runtime_error("Vulkan skybox already initialized or surface failed");
    }
    VkShaderModule vs = VK_NULL_HANDLE, fs = VK_NULL_HANDLE;
    try {
        std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        for (uint32_t i = 1; i < 3; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo set{};
        set.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        set.bindingCount = uint32_t(bindings.size());
        set.pBindings = bindings.data();
        checkSky(vkCreateDescriptorSetLayout(device_, &set, nullptr, &skySetLayout_),
                 "vkCreateDescriptorSetLayout sky");
        VkPipelineLayoutCreateInfo layout{};
        layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layout.setLayoutCount = 1;
        layout.pSetLayouts = &skySetLayout_;
        checkSky(vkCreatePipelineLayout(device_, &layout, nullptr, &skyPipelineLayout_),
                 "vkCreatePipelineLayout sky");
        vs = skyShader(device_, vertexShader);
        fs = skyShader(device_, fragmentShader);
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs;
        stages[0].pName = "main";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs;
        stages[1].pName = "main";
        VkPipelineVertexInputStateCreateInfo input{};
        input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        VkPipelineInputAssemblyStateCreateInfo assembly{};
        assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{};
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount = viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.lineWidth = 1;
        VkPipelineMultisampleStateCreateInfo samples{};
        samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        VkPipelineColorBlendAttachmentState attachment{};
        attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        attachment.blendEnable = VK_TRUE;
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
        VkGraphicsPipelineCreateInfo pipeline{};
        pipeline.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeline.stageCount = 2;
        pipeline.pStages = stages;
        pipeline.pVertexInputState = &input;
        pipeline.pInputAssemblyState = &assembly;
        pipeline.pViewportState = &viewport;
        pipeline.pRasterizationState = &raster;
        pipeline.pMultisampleState = &samples;
        pipeline.pDepthStencilState = &depth;
        pipeline.pColorBlendState = &blend;
        pipeline.pDynamicState = &dynamic;
        pipeline.layout = skyPipelineLayout_;
        pipeline.renderPass = renderPass_;
        checkSky(vkCreateGraphicsPipelines(
                     device_, VK_NULL_HANDLE, 1, &pipeline, nullptr, &skyPipeline_),
                 "vkCreateGraphicsPipelines sky");
        vkDestroyShaderModule(device_, fs, nullptr);
        fs = VK_NULL_HANDLE;
        vkDestroyShaderModule(device_, vs, nullptr);
        vs = VK_NULL_HANDLE;
        skyWhiteCube_ = std::make_unique<VulkanTexture>(physical_,
                                                        device_,
                                                        queue_,
                                                        family_,
                                                        1,
                                                        1,
                                                        std::vector<uint8_t>(24, 255),
                                                        VulkanTexture::Address::Clamp,
                                                        false,
                                                        false,
                                                        true);
        skyWhitePanorama_ = std::make_unique<VulkanTexture>(
            physical_, device_, queue_, family_, 1, 1, std::vector<uint8_t>(4, 255));
    } catch (...) {
        if (fs) {
            vkDestroyShaderModule(device_, fs, nullptr);
        }
        if (vs) {
            vkDestroyShaderModule(device_, vs, nullptr);
        }
        cleanupSky();
        throw;
    }
}
void VulkanSceneSurface::destroySkyFrame(SkyFrame& frame) noexcept {
    if (frame.pool) {
        vkDestroyDescriptorPool(device_, frame.pool, nullptr);
    }
    frame.pool = VK_NULL_HANDLE;
    destroyBuffer(frame.uniforms);
}
VulkanSceneSurface::SkyFrame VulkanSceneSurface::prepareSky(const std::vector<SkyDraw>& skies) {
    SkyFrame frame{};
    if (skies.empty()) {
        return frame;
    }
    if (skies.size() > 2 || !supportsSkybox()) {
        throw std::runtime_error("Invalid Vulkan skybox draw count or pipeline");
    }
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physical_, &props);
    const VkDeviceSize align =
        std::max<VkDeviceSize>(1, props.limits.minUniformBufferOffsetAlignment);
    if (align > 128ull * 1024 * 1024 ||
        props.limits.maxUniformBufferRange < sizeof(VulkanShaderLayout::Skybox)) {
        throw std::runtime_error("Vulkan skybox uniform limits unsupported");
    }
    const VkDeviceSize stride = (sizeof(VulkanShaderLayout::Skybox) + align - 1) / align * align;
    try {
        frame.uniforms = makeBuffer(stride * skies.size(), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        VkDescriptorPoolSize sizes[]{
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uint32_t(skies.size())},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, uint32_t(skies.size() * 2)}};
        VkDescriptorPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool.maxSets = uint32_t(skies.size());
        pool.poolSizeCount = 2;
        pool.pPoolSizes = sizes;
        checkSky(vkCreateDescriptorPool(device_, &pool, nullptr, &frame.pool),
                 "vkCreateDescriptorPool sky");
        std::vector<VkDescriptorSetLayout> layouts(skies.size(), skySetLayout_);
        frame.sets.resize(skies.size());
        VkDescriptorSetAllocateInfo alloc{};
        alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        alloc.descriptorPool = frame.pool;
        alloc.descriptorSetCount = uint32_t(skies.size());
        alloc.pSetLayouts = layouts.data();
        checkSky(vkAllocateDescriptorSets(device_, &alloc, frame.sets.data()),
                 "vkAllocateDescriptorSets sky");
        std::vector<uint8_t> values(size_t(stride * skies.size()), 0);
        for (size_t i = 0; i < skies.size(); ++i) {
            const auto& sky = skies[i];
            if (sky.texture && sky.texture->cubemap() == sky.hdr) {
                throw std::runtime_error("Incorrect Vulkan skybox texture type");
            }
            VulkanShaderLayout::Skybox u{};
            u.inverseVP = inverseCameraMatrix(vulkanClipMatrix(sky.viewProjection));
            u.tintOpacity = sky.tintOpacity;
            u.hasTexture = sky.texture != nullptr;
            u.isHDR = sky.hdr;
            std::memcpy(values.data() + stride * i, &u, sizeof(u));
            VkDescriptorBufferInfo buffer{frame.uniforms.buffer, stride * i, sizeof(u)};
            const auto* cube = sky.texture && !sky.hdr ? sky.texture : skyWhiteCube_.get();
            const auto* pano = sky.texture && sky.hdr ? sky.texture : skyWhitePanorama_.get();
            VkDescriptorImageInfo images[]{
                {cube->sampler(), cube->imageView(), cube->imageLayout()},
                {pano->sampler(), pano->imageView(), pano->imageLayout()}};
            VkWriteDescriptorSet writes[3]{};
            for (auto& write : writes) {
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = frame.sets[i];
                write.descriptorCount = 1;
            }
            writes[0].dstBinding = 0;
            writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[0].pBufferInfo = &buffer;
            for (int j = 0; j < 2; ++j) {
                writes[j + 1].dstBinding = j + 1;
                writes[j + 1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                writes[j + 1].pImageInfo = &images[j];
            }
            vkUpdateDescriptorSets(device_, 3, writes, 0, nullptr);
        }
        upload(frame.uniforms, values.data(), stride * skies.size());
        return frame;
    } catch (...) {
        destroySkyFrame(frame);
        throw;
    }
}
void VulkanSceneSurface::cleanupSky() noexcept {
    skyWhiteCube_.reset();
    skyWhitePanorama_.reset();
    if (skyPipeline_) {
        vkDestroyPipeline(device_, skyPipeline_, nullptr);
    }
    if (skyPipelineLayout_) {
        vkDestroyPipelineLayout(device_, skyPipelineLayout_, nullptr);
    }
    if (skySetLayout_) {
        vkDestroyDescriptorSetLayout(device_, skySetLayout_, nullptr);
    }
    skyPipeline_ = VK_NULL_HANDLE;
    skyPipelineLayout_ = VK_NULL_HANDLE;
    skySetLayout_ = VK_NULL_HANDLE;
}
