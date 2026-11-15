// Opt-in SDK experiment only; not the production upscaler. Reuses the pinned
// port's Vulkan/FSR test helpers without vendoring that third-party source.
#define main legacy_port_selftest_main
#include LEGACY_FSR3_SELFTEST_SOURCE
#undef main

#include "rendering/vulkan_scene_renderer.hpp"
#include <fstream>
#include <stdexcept>

namespace {
std::vector<uint32_t> shader(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("cannot open engine shader");
    }
    const auto size = file.tellg();
    if (size <= 0 || size % 4) {
        throw std::runtime_error("invalid shader size");
    }
    std::vector<uint32_t> data(size_t(size) / 4);
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        throw std::runtime_error("shader read failed");
    }
    return data;
}
VkImageCreateInfo description(VkFormat format, uint32_t w, uint32_t h, VkImageUsageFlags usage) {
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = format;
    ci.extent = {w, h, 1};
    ci.mipLevels = ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    return ci;
}
FfxResource
resource(VkImage image, VkImageCreateInfo info, const wchar_t* name, bool write = false) {
    return ffxGetResourceVK(
        reinterpret_cast<void*>(image),
        ffxGetImageResourceDescriptionVK(
            image, info, write ? FFX_RESOURCE_USAGE_UAV : FFX_RESOURCE_USAGE_READ_ONLY),
        name,
        write ? FFX_RESOURCE_STATE_UNORDERED_ACCESS : FFX_RESOURCE_STATE_COMPUTE_READ);
}
void destroy(Gpu& g, Image& image) {
    vkDestroyImage(g.device, image.image, nullptr);
    vkFreeMemory(g.device, image.memory, nullptr);
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 5) {
        return 1;
    }
    try {
        Gpu g = createGpu();
        constexpr uint32_t rw = 64, rh = 64, dw = 128, dh = 128;
        {
            VulkanSceneSurface surface(
                g.physical, g.device, g.queue, g.queueFamily, shader(argv[1]), shader(argv[2]));
            surface.enableMotionVectors(shader(argv[3]), shader(argv[4]));
            VulkanSceneRenderer renderer(surface);
            MeshData triangle;
            triangle.vertices = {{{-.9f, -.9f, 0}}, {{.9f, -.9f, 0}}, {{0, .9f, 0}}};
            const auto mesh = surface.createMesh(triangle);
            World world;
            const auto entity = world.create();
            world.add(entity, Transform{});
            world.add(entity, MeshRenderer{mesh, {1, .1f, .1f}});
            TemporalHistory history;
            TemporalHistory::Configuration config{{rw, rh}, {dw, dh}, 1, 1, 1};
            Mat4 identity{};
            identity.m[0] = identity.m[5] = identity.m[10] = identity.m[15] = 1;

            const size_t scratchSize =
                ffxGetScratchMemorySizeVK(g.physical, FFX_FSR3UPSCALER_CONTEXT_COUNT);
            std::unique_ptr<uint8_t, decltype(&free)> scratch(
                static_cast<uint8_t*>(aligned_alloc(64, (scratchSize + 63) / 64 * 64)), &free);
            if (!scratch) {
                throw std::runtime_error("FSR scratch allocation failed");
            }
            std::memset(scratch.get(), 0, scratchSize);
            VkDeviceContext vkdc{g.device, g.physical, g.host.GetDeviceProcAddr};
            FfxFsr3UpscalerContextDescription cd{};
            FFX_CHECK(ffxGetInterfaceVK(&cd.backendInterface,
                                        ffxGetDeviceVK(&vkdc),
                                        scratch.get(),
                                        scratchSize,
                                        FFX_FSR3UPSCALER_CONTEXT_COUNT));
            cd.flags = FFX_FSR3UPSCALER_ENABLE_AUTO_EXPOSURE;
            cd.maxRenderSize = {rw, rh};
            cd.maxUpscaleSize = {dw, dh};
            auto context = std::make_unique<FfxFsr3UpscalerContext>();
            FFX_CHECK(ffxFsr3UpscalerContextCreate(context.get(), &cd));
            const auto rwUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            Image output = makeImage(g,
                                     VK_FORMAT_R16G16B16A16_SFLOAT,
                                     dw,
                                     dh,
                                     VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                         VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
            Image dilatedDepth = makeImage(g, VK_FORMAT_R32_SFLOAT, rw, rh, rwUsage);
            Image dilatedMotion = makeImage(g, VK_FORMAT_R16G16_SFLOAT, rw, rh, rwUsage);
            Image previousDepth = makeImage(g, VK_FORMAT_R32_UINT, rw, rh, rwUsage);
            Buffer readback =
                makeHostBuffer(g, VkDeviceSize(dw) * dh * 8, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
            VkCommandBuffer cb;
            VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            alloc.commandPool = g.pool;
            alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc.commandBufferCount = 1;
            CHECK(vkAllocateCommandBuffers(g.device, &alloc, &cb));
            VkImageLayout outLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            for (int frame = 0; frame < 4; ++frame) {
                world.get<Transform>(entity).position.x = float(frame) * .04f;
                auto drawn = renderer.drawTemporalTo(
                    surface, world, identity, config, history, {}, false, frame == 2);
                const bool reset = frame == 0 || frame == 2;
                if (drawn.frame.reset != reset || drawn.missingObjectHistory != reset) {
                    throw std::runtime_error("unexpected temporal reset/history state");
                }
                float jx = 0, jy = 0;
                const auto phases = ffxFsr3UpscalerGetJitterPhaseCount(rw, dw);
                FFX_CHECK(ffxFsr3UpscalerGetJitterOffset(&jx, &jy, int(drawn.frame.index), phases));
                if (std::abs(drawn.frame.jitter.x - jx) > 1e-6f ||
                    std::abs(drawn.frame.jitter.y - jy) > 1e-6f) {
                    throw std::runtime_error("engine and SDK jitter differ");
                }
                if (!surface.colorImage() || !surface.motionImage() ||
                    !surface.motionDepthImage()) {
                    throw std::runtime_error("engine temporal resources missing");
                }
                VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
                begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
                CHECK(vkBeginCommandBuffer(cb, &begin));
                // The surface's color pass synchronizes for fragment consumers;
                // make its writes visible to this compute consumer too.
                barrier(cb,
                        surface.colorImage(),
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                barrier(cb,
                        surface.motionDepthImage(),
                        VK_IMAGE_ASPECT_DEPTH_BIT,
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                barrier(cb,
                        output.image,
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        outLayout,
                        VK_IMAGE_LAYOUT_GENERAL);
                if (!frame) {
                    for (Image* i : {&dilatedDepth, &dilatedMotion, &previousDepth}) {
                        barrier(cb,
                                i->image,
                                VK_IMAGE_ASPECT_COLOR_BIT,
                                VK_IMAGE_LAYOUT_UNDEFINED,
                                VK_IMAGE_LAYOUT_GENERAL);
                    }
                }
                const auto colorInfo =
                    description(VK_FORMAT_R8G8B8A8_UNORM,
                                rw,
                                rh,
                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
                const auto depthInfo = description(VK_FORMAT_D32_SFLOAT,
                                                   rw,
                                                   rh,
                                                   VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                                       VK_IMAGE_USAGE_SAMPLED_BIT);
                const auto motionInfo =
                    description(VK_FORMAT_R32G32_SFLOAT,
                                rw,
                                rh,
                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
                FfxFsr3UpscalerDispatchDescription dd{};
                dd.commandList = ffxGetCommandListVK(cb);
                dd.color = resource(surface.colorImage(), colorInfo, L"engine color");
                dd.depth = resource(surface.motionDepthImage(), depthInfo, L"engine depth");
                dd.motionVectors = resource(surface.motionImage(), motionInfo, L"engine motion");
                dd.output = resource(output.image, output.info, L"output", true);
                dd.dilatedDepth =
                    resource(dilatedDepth.image, dilatedDepth.info, L"dilated depth", true);
                dd.dilatedMotionVectors =
                    resource(dilatedMotion.image, dilatedMotion.info, L"dilated motion", true);
                dd.reconstructedPrevNearestDepth =
                    resource(previousDepth.image, previousDepth.info, L"previous depth", true);
                dd.jitterOffset = {drawn.frame.jitter.x, drawn.frame.jitter.y};
                dd.motionVectorScale = {float(rw), float(rh)};
                dd.renderSize = {rw, rh};
                dd.upscaleSize = {dw, dh};
                dd.frameTimeDelta = 16.7f;
                dd.preExposure = 1;
                dd.reset = drawn.frame.reset || drawn.missingObjectHistory;
                dd.cameraNear = .1f;
                dd.cameraFar = 1000;
                dd.cameraFovAngleVertical = 1;
                dd.viewSpaceToMetersFactor = 1;
                FFX_CHECK(ffxFsr3UpscalerContextDispatch(context.get(), &dd));
                barrier(cb,
                        surface.motionDepthImage(),
                        VK_IMAGE_ASPECT_DEPTH_BIT,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
                barrier(cb,
                        output.image,
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        VK_IMAGE_LAYOUT_GENERAL,
                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
                VkBufferImageCopy copy{};
                copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                copy.imageExtent = {dw, dh, 1};
                vkCmdCopyImageToBuffer(cb,
                                       output.image,
                                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                       readback.buffer,
                                       1,
                                       &copy);
                CHECK(vkEndCommandBuffer(cb));
                VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
                submit.commandBufferCount = 1;
                submit.pCommandBuffers = &cb;
                CHECK(vkQueueSubmit(g.queue, 1, &submit, VK_NULL_HANDLE));
                CHECK(vkQueueWaitIdle(g.queue));
                CHECK(vkResetCommandBuffer(cb, 0));
                outLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            }
            const auto* pixels = static_cast<const uint16_t*>(readback.mapped);
            const size_t center = (64 * dw + 64) * 4;
            float r = fromHalf(pixels[center]), green = fromHalf(pixels[center + 1]);
            printf("engine FSR output center: %f %f\n", r, green);
            for (size_t i = 0; i < size_t(dw) * dh * 4; ++i) {
                if (!std::isfinite(fromHalf(pixels[i]))) {
                    throw std::runtime_error("non-finite FSR output");
                }
            }
            if (r < .2f || r <= green * 2 || r <= fromHalf(pixels[0]) * 2) {
                throw std::runtime_error("FSR output does not contain the rendered red triangle");
            }
            FFX_CHECK(ffxFsr3UpscalerContextDestroy(context.get()));
            vkFreeCommandBuffers(g.device, g.pool, 1, &cb);
            for (Image* image : {&output, &dilatedDepth, &dilatedMotion, &previousDepth}) {
                destroy(g, *image);
            }
            vkUnmapMemory(g.device, readback.memory);
            vkDestroyBuffer(g.device, readback.buffer, nullptr);
            vkFreeMemory(g.device, readback.memory, nullptr);
        }
        vkDestroyCommandPool(g.device, g.pool, nullptr);
        vkDestroyDevice(g.device, nullptr);
        if (g.messenger) {
            reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(
                g.instance, "vkDestroyDebugUtilsMessengerEXT"))(g.instance, g.messenger, nullptr);
        }
        vkDestroyInstance(g.instance, nullptr);
        if (g_validationErrors) {
            throw std::runtime_error("Vulkan validation errors");
        }
        puts("PASS: experimental engine-to-FSR dispatch");
        return 0;
    } catch (const std::exception& e) {
        fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
