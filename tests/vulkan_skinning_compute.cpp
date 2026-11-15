// Real Vulkan compute/readback regression test. Requires a Vulkan device; returns
// CTest's skip code when no loader/device/compute queue is available.
#include "rendering/vulkan_shader_layout.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <vulkan/vulkan.h>

static void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(operation);
    }
}
struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
};
struct Test {
    VkInstance instance = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkShaderModule shader = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::array<Buffer, 5> buffers{};
    ~Test() {
        if (device) {
            vkDeviceWaitIdle(device);
            if (commandPool) {
                vkDestroyCommandPool(device, commandPool, nullptr);
            }
            if (pipeline) {
                vkDestroyPipeline(device, pipeline, nullptr);
            }
            if (shader) {
                vkDestroyShaderModule(device, shader, nullptr);
            }
            if (pipelineLayout) {
                vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
            }
            if (descriptorPool) {
                vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            }
            if (setLayout) {
                vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
            }
            for (auto& b : buffers) {
                if (b.mapped) {
                    vkUnmapMemory(device, b.memory);
                }
                if (b.buffer) {
                    vkDestroyBuffer(device, b.buffer, nullptr);
                }
                if (b.memory) {
                    vkFreeMemory(device, b.memory, nullptr);
                }
            }
            vkDestroyDevice(device, nullptr);
        }
        if (instance) {
            vkDestroyInstance(instance, nullptr);
        }
    }
};

int main(int argc, char** argv) {
    if (argc != 2) {
        return 1;
    }
    try {
        Test t;
        VkApplicationInfo app{};
        app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.apiVersion = VK_API_VERSION_1_0;
        VkInstanceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo = &app;
        if (vkCreateInstance(&ci, nullptr, &t.instance) != VK_SUCCESS) {
            return 77;
        }
        uint32_t n = 0;
        if (vkEnumeratePhysicalDevices(t.instance, &n, nullptr) != VK_SUCCESS || !n) {
            return 77;
        }
        std::vector<VkPhysicalDevice> devices(n);
        check(vkEnumeratePhysicalDevices(t.instance, &n, devices.data()), "enumerate devices");
        VkPhysicalDevice physical = VK_NULL_HANDLE;
        uint32_t family = 0;
        for (auto candidate : devices) {
            uint32_t count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
            std::vector<VkQueueFamilyProperties> props(count);
            vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, props.data());
            for (uint32_t i = 0; i < count; ++i) {
                if (props[i].queueCount && (props[i].queueFlags & VK_QUEUE_COMPUTE_BIT)) {
                    physical = candidate;
                    family = i;
                    break;
                }
            }
            if (physical) {
                break;
            }
        }
        if (!physical) {
            return 77;
        }
        float priority = 1;
        VkDeviceQueueCreateInfo qi{};
        qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qi.queueFamilyIndex = family;
        qi.queueCount = 1;
        qi.pQueuePriorities = &priority;
        VkDeviceCreateInfo di{};
        di.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        di.queueCreateInfoCount = 1;
        di.pQueueCreateInfos = &qi;
        check(vkCreateDevice(physical, &di, nullptr, &t.device), "create device");
        VkQueue queue;
        vkGetDeviceQueue(t.device, family, 0, &queue);
        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceMemoryProperties(physical, &memory);
        auto createBuffer = [&](unsigned index, VkDeviceSize size, const void* data) {
            Buffer& b = t.buffers[index];
            VkBufferCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            info.size = size;
            info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            check(vkCreateBuffer(t.device, &info, nullptr, &b.buffer), "create buffer");
            VkMemoryRequirements requirements;
            vkGetBufferMemoryRequirements(t.device, b.buffer, &requirements);
            uint32_t type = UINT32_MAX;
            for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
                const auto flags =
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                if ((requirements.memoryTypeBits & (1u << i)) &&
                    (memory.memoryTypes[i].propertyFlags & flags) == flags) {
                    type = i;
                    break;
                }
            }
            if (type == UINT32_MAX) {
                throw std::runtime_error("no coherent host memory for test");
            }
            VkMemoryAllocateInfo ai{};
            ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai.allocationSize = requirements.size;
            ai.memoryTypeIndex = type;
            check(vkAllocateMemory(t.device, &ai, nullptr, &b.memory), "allocate buffer memory");
            check(vkBindBufferMemory(t.device, b.buffer, b.memory, 0), "bind buffer");
            check(vkMapMemory(t.device, b.memory, 0, size, 0, &b.mapped), "map buffer");
            if (data) {
                std::memcpy(b.mapped, data, static_cast<size_t>(size));
            } else {
                std::memset(b.mapped, 0, static_cast<size_t>(size));
            }
        };
        constexpr uint32_t count = 65;
        std::array<Vertex, count> input{};
        std::array<std::array<uint32_t, 2>, count> ranges{};
        for (uint32_t i = 0; i < count; ++i) {
            input[i] = {{float(i), 2, -1}, {1, 1, 0}, .25f, .75f};
            ranges[i] = {0, 2};
        }
        ranges[1] = {0, 0};
        const float influences[]{0, .25f, 1, .75f};
        std::array<Mat4, 4> palette{};
        for (auto& m : palette) {
            m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1;
        }
        palette[2].m[12] = 2;
        palette[3].m[0] = .5f; // Second bone has an inverse-transpose normal scale.
        createBuffer(0, sizeof(input), input.data());
        createBuffer(1, sizeof(ranges), ranges.data());
        createBuffer(2, sizeof(influences), influences);
        createBuffer(3, sizeof(palette), palette.data());
        createBuffer(4, sizeof(input), nullptr);
        std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
        for (uint32_t i = 0; i < bindings.size(); ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo sl{};
        sl.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        sl.bindingCount = static_cast<uint32_t>(bindings.size());
        sl.pBindings = bindings.data();
        check(vkCreateDescriptorSetLayout(t.device, &sl, nullptr, &t.setLayout),
              "create descriptor layout");
        VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5};
        VkDescriptorPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool.maxSets = 1;
        pool.poolSizeCount = 1;
        pool.pPoolSizes = &size;
        check(vkCreateDescriptorPool(t.device, &pool, nullptr, &t.descriptorPool),
              "create descriptor pool");
        VkDescriptorSetAllocateInfo ds{};
        ds.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ds.descriptorPool = t.descriptorPool;
        ds.descriptorSetCount = 1;
        ds.pSetLayouts = &t.setLayout;
        VkDescriptorSet set;
        check(vkAllocateDescriptorSets(t.device, &ds, &set), "allocate descriptor set");
        std::array<VkDescriptorBufferInfo, 5> infos{};
        std::array<VkWriteDescriptorSet, 5> writes{};
        for (uint32_t i = 0; i < 5; ++i) {
            infos[i] = {t.buffers[i].buffer, 0, VK_WHOLE_SIZE};
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &infos[i];
        }
        vkUpdateDescriptorSets(t.device, 5, writes.data(), 0, nullptr);
        VkPushConstantRange pc{VK_SHADER_STAGE_COMPUTE_BIT, 0, 8};
        VkPipelineLayoutCreateInfo pl{};
        pl.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl.setLayoutCount = 1;
        pl.pSetLayouts = &t.setLayout;
        pl.pushConstantRangeCount = 1;
        pl.pPushConstantRanges = &pc;
        check(vkCreatePipelineLayout(t.device, &pl, nullptr, &t.pipelineLayout),
              "create pipeline layout");
        std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
        if (!file) {
            throw std::runtime_error("cannot open SPIR-V");
        }
        auto bytes = file.tellg();
        if (bytes <= 0 || bytes % 4 != 0) {
            throw std::runtime_error("invalid SPIR-V length");
        }
        std::vector<uint32_t> words(static_cast<size_t>(bytes) / 4);
        file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(words.data()), bytes)) {
            throw std::runtime_error("cannot read SPIR-V");
        }
        VkShaderModuleCreateInfo sm{};
        sm.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        sm.codeSize = static_cast<size_t>(bytes);
        sm.pCode = words.data();
        check(vkCreateShaderModule(t.device, &sm, nullptr, &t.shader), "create shader module");
        VkPipelineShaderStageCreateInfo stage{};
        stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = t.shader;
        stage.pName = "main";
        VkComputePipelineCreateInfo pi{};
        pi.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pi.stage = stage;
        pi.layout = t.pipelineLayout;
        check(vkCreateComputePipelines(t.device, VK_NULL_HANDLE, 1, &pi, nullptr, &t.pipeline),
              "create compute pipeline");
        VkCommandPoolCreateInfo cp{};
        cp.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cp.queueFamilyIndex = family;
        check(vkCreateCommandPool(t.device, &cp, nullptr, &t.commandPool), "create command pool");
        VkCommandBufferAllocateInfo ca{};
        ca.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ca.commandPool = t.commandPool;
        ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ca.commandBufferCount = 1;
        VkCommandBuffer cmd;
        check(vkAllocateCommandBuffers(t.device, &ca, &cmd), "allocate command buffer");
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        check(vkBeginCommandBuffer(cmd, &begin), "begin commands");
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, t.pipeline);
        vkCmdBindDescriptorSets(
            cmd, VK_PIPELINE_BIND_POINT_COMPUTE, t.pipelineLayout, 0, 1, &set, 0, nullptr);
        VulkanShaderLayout::SkinningParameters push{count, 2};
        vkCmdPushConstants(
            cmd, t.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(cmd, (count + 63) / 64, 1, 1);
        VkMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT,
                             0,
                             1,
                             &barrier,
                             0,
                             nullptr,
                             0,
                             nullptr);
        check(vkEndCommandBuffer(cmd), "end commands");
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &cmd;
        check(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE), "submit");
        check(vkQueueWaitIdle(queue), "wait");
        const auto* out = static_cast<const Vertex*>(t.buffers[4].mapped);
        const Vec3 expectedNormal = normalize(Vec3{.625f, 1, 0});
        for (uint32_t i = 0; i < count; ++i) {
            const bool empty = i == 1;
            if (std::abs(out[i].position.x - (empty ? 0 : 2 * (float(i) + 1.5f))) > .0001f ||
                std::abs(out[i].position.y - (empty ? 0 : 4)) > .0001f ||
                std::abs(out[i].position.z - (empty ? 0 : -2)) > .0001f ||
                std::abs(out[i].normal.x - (empty ? 0 : expectedNormal.x)) > .0001f ||
                std::abs(out[i].normal.y - (empty ? 1 : expectedNormal.y)) > .0001f ||
                std::abs(out[i].normal.z) > .0001f || out[i].u != .25f || out[i].v != .75f) {
                throw std::runtime_error("incorrect skinning output");
            }
        }
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Vulkan skinning test: %s\n", e.what());
        return 1;
    }
}
