// Full SDL/ImGui/Vulkan submission test using an offscreen scene texture.
#include "assets/vulkan_browser_thumbnails.hpp"
#include "assets/vulkan_thumbnail_cache.hpp"
#include "editor/asset_preview_ui.hpp"
#include "materials/material_io.hpp"
#include "rendering/vulkan_imgui.hpp"
#include <SDL3/SDL.h>
#include <cstdio>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

static std::vector<uint32_t> load(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Cannot open shader");
    }
    auto size = file.tellg();
    if (size <= 0 || size % 4) {
        throw std::runtime_error("Invalid SPIR-V");
    }
    std::vector<uint32_t> data(static_cast<size_t>(size) / 4);
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        throw std::runtime_error("Cannot read shader");
    }
    return data;
}
static void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
// Exercise the engine's graphics-independent thumbnail decoder and GPU cache.
struct Fixture {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
                                 ("legacy-vulkan-thumbnail-" + std::to_string(SDL_GetTicksNS()));
    AssetManager assets;
    Fixture() {
        assets.root = path / "Assets";
        std::filesystem::create_directories(assets.root);
        std::ofstream out(assets.root / "red.tga", std::ios::binary);
        unsigned char header[18]{};
        header[2] = 2;
        header[12] = 2;
        header[14] = 2;
        header[16] = 24;
        out.write(reinterpret_cast<const char*>(header), sizeof(header));
        for (int i = 0; i < 4; ++i) {
            out.put(0);
            out.put(0);
            out.put(char(255));
        }
        if (!out) {
            throw std::runtime_error("Cannot create texture fixture");
        }
        out.close();
        MaterialProperties material;
        material.color = {1, .5f, .25f};
        material.texture = "red.tga";
        saveMaterial(assets, "test.mat", material);
    }
    ~Fixture() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

// Verify uploaded pixels and the generated last mip on the GPU, then restore
// the layout so the same image can be sampled by the ImGui backend.
static void checkTexturePixels(VulkanContext& context, const VulkanTexture& texture) {
    auto device = context.device();
    struct Temp {
        VkDevice device;
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkCommandPool pool = VK_NULL_HANDLE;
        ~Temp() {
            if (pool) {
                vkDestroyCommandPool(device, pool, nullptr);
            }
            if (buffer) {
                vkDestroyBuffer(device, buffer, nullptr);
            }
            if (memory) {
                vkFreeMemory(device, memory, nullptr);
            }
        }
    } temp{device};
    auto vkcheck = [](VkResult result) {
        if (result != VK_SUCCESS) {
            throw std::runtime_error("texture readback Vulkan error");
        }
    };
    constexpr VkDeviceSize bytes = 4 * 2 * 4 + 4;
    VkBufferCreateInfo buffer{};
    buffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer.size = bytes;
    buffer.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    vkcheck(vkCreateBuffer(device, &buffer, nullptr, &temp.buffer));
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device, temp.buffer, &req);
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(context.physicalDevice(), &props);
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        const auto flags =
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if ((req.memoryTypeBits & (1u << i)) &&
            (props.memoryTypes[i].propertyFlags & flags) == flags) {
            type = i;
            break;
        }
    }
    require(type != UINT32_MAX, "No coherent readback memory");
    VkMemoryAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = type;
    vkcheck(vkAllocateMemory(device, &alloc, nullptr, &temp.memory));
    vkcheck(vkBindBufferMemory(device, temp.buffer, temp.memory, 0));
    VkCommandPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.queueFamilyIndex = context.graphicsFamily();
    vkcheck(vkCreateCommandPool(device, &pool, nullptr, &temp.pool));
    VkCommandBufferAllocateInfo ca{};
    ca.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ca.commandPool = temp.pool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkcheck(vkAllocateCommandBuffers(device, &ca, &cmd));
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkcheck(vkBeginCommandBuffer(cmd, &begin));
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = texture.image();
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = texture.mipLevels();
    barrier.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0,
                         nullptr,
                         0,
                         nullptr,
                         1,
                         &barrier);
    VkBufferImageCopy copies[2]{};
    copies[0].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copies[0].imageSubresource.layerCount = 1;
    copies[0].imageExtent = {4, 2, 1};
    copies[1].bufferOffset = 32;
    copies[1].imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copies[1].imageSubresource.mipLevel = 2;
    copies[1].imageSubresource.layerCount = 1;
    copies[1].imageExtent = {1, 1, 1};
    vkCmdCopyImageToBuffer(
        cmd, texture.image(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, temp.buffer, 2, copies);
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
    vkcheck(vkEndCommandBuffer(cmd));
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    vkcheck(vkQueueSubmit(context.graphicsQueue(), 1, &submit, VK_NULL_HANDLE));
    vkcheck(vkQueueWaitIdle(context.graphicsQueue()));
    void* mapped = nullptr;
    vkcheck(vkMapMemory(device, temp.memory, 0, bytes, 0, &mapped));
    auto data = static_cast<const uint8_t*>(mapped);
    const bool valid = data[0] == 255 && data[1] == 0 && data[2] == 0 && data[3] == 255 &&
                       data[8] == 0 && data[9] == 255 && data[10] == 0 && data[11] == 255 &&
                       data[16] == 0 && data[17] == 0 && data[18] == 255 && data[19] == 255 &&
                       data[32] >= 126 && data[32] <= 129 && data[33] >= 126 && data[33] <= 129 &&
                       data[34] >= 126 && data[34] <= 129 && data[35] == 255;
    vkUnmapMemory(device, temp.memory);
    require(valid, "Vulkan image upload or mipmap readback failed");
}

int main(int argc, char** argv) {
    if (argc != 3) {
        return 1;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return 77;
    }
    SDL_Window* window =
        SDL_CreateWindow("Vulkan ImGui integration",
                         800,
                         600,
                         SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE);
    if (!window) {
        SDL_Quit();
        return 77;
    }
    int result = 0;
    try {
        VulkanContext context(window);
        auto gameWindow = std::unique_ptr<SDL_Window, void (*)(SDL_Window*)>(
            SDL_CreateWindow("Vulkan game window",
                             320,
                             240,
                             SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE),
            SDL_DestroyWindow);
        require(bool(gameWindow), "Could not create secondary Vulkan window");
        VulkanContext gameContext(gameWindow.get(), context);
        VulkanSceneSurface scene(context.physicalDevice(),
                                 context.device(),
                                 context.graphicsQueue(),
                                 context.graphicsFamily(),
                                 load(argv[1]),
                                 load(argv[2]));
        MeshData triangle;
        triangle.vertices = {{{-.8f, -.8f, 0}}, {{.8f, -.8f, 0}}, {{0, .8f, 0}}};
        MeshHandle mesh = scene.createMesh(triangle);
        std::vector<uint8_t> pixels{255, 0,   0,   255, 255, 0,   0,   255, 0,   255, 0,
                                    255, 0,   255, 0,   255, 0,   0,   255, 255, 0,   0,
                                    255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
        VulkanTexture image(context.physicalDevice(),
                            context.device(),
                            context.graphicsQueue(),
                            context.graphicsFamily(),
                            4,
                            2,
                            pixels,
                            VulkanTexture::Address::Repeat,
                            true);
        require(image.imageView() && image.sampler() && image.mipLevels() == 3,
                "texture upload failed");
        checkTexturePixels(context, image);
        Fixture fixture;
        const auto decoded = loadTextureThumbnail(fixture.assets, "red.tga");
        const auto center = (TextureThumbnail::size * TextureThumbnail::size / 2) * 4;
        require(decoded.pixels[center] == 255 && decoded.pixels[center + 3] == 255,
                "thumbnail decode failed");
        VulkanTexture thumbnail(context.physicalDevice(),
                                context.device(),
                                context.graphicsQueue(),
                                context.graphicsFamily(),
                                TextureThumbnail::size,
                                TextureThumbnail::size,
                                std::vector<uint8_t>(decoded.pixels.begin(), decoded.pixels.end()));
        bool rejected = false;
        try {
            VulkanTexture bad(context.physicalDevice(),
                              context.device(),
                              context.graphicsQueue(),
                              context.graphicsFamily(),
                              4,
                              2,
                              std::vector<uint8_t>(3));
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "invalid texture data was accepted");
        Mat4 identity{};
        identity.m[0] = identity.m[5] = identity.m[10] = identity.m[15] = 1;
        auto imgui = std::unique_ptr<ImGuiContext, void (*)(ImGuiContext*)>(ImGui::CreateContext(),
                                                                            ImGui::DestroyContext);
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        VulkanImGui gui(window, context);
        VulkanThumbnailCache thumbnails(context, gui);
        AssetThumbnailSource browserSource;
        VulkanBrowserThumbnails browserThumbnails(browserSource, context, gui);
        static_assert(sizeof(AssetPreviewData{}.texture) >= sizeof(ImTextureID),
                      "Preview texture IDs must not be truncated");
        unsigned presented = 0, gamePresented = 0;
        ImVec2 buttonPosition{}, textPosition{};
        unsigned clicks = 0;
        char text[64]{};
        for (unsigned frame = 0; frame < 80; ++frame) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                gui.processEvent(event);
                if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                    if (event.window.windowID == SDL_GetWindowID(window)) {
                        context.requestResize();
                    } else if (event.window.windowID == SDL_GetWindowID(gameWindow.get())) {
                        gameContext.requestResize();
                    }
                }
            }
            if (frame == 15) {
                context.setVsync(true);
            }
            if (frame == 50) {
                context.setVsync(false);
            }
            const bool buttonClick = (frame >= 18 && frame <= 20) || (frame >= 45 && frame <= 47) ||
                                     (frame >= 55 && frame <= 57);
            const bool textClick = frame >= 62 && frame <= 64;
            if (buttonClick || textClick) {
                const unsigned phase = textClick     ? frame - 62
                                       : frame >= 55 ? frame - 55
                                       : frame >= 45 ? frame - 45
                                                     : frame - 18;
                const auto pos = textClick ? textPosition : buttonPosition;
                SDL_Event mouse{};
                if (phase == 0) {
                    mouse.type = SDL_EVENT_MOUSE_MOTION;
                    mouse.motion.windowID = SDL_GetWindowID(window);
                    mouse.motion.x = pos.x;
                    mouse.motion.y = pos.y;
                } else {
                    mouse.type =
                        phase == 1 ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
                    mouse.button.windowID = SDL_GetWindowID(window);
                    mouse.button.button = SDL_BUTTON_LEFT;
                    mouse.button.x = pos.x;
                    mouse.button.y = pos.y;
                }
                gui.processEvent(mouse);
            }
            if (frame == 65) {
                SDL_Event typed{};
                typed.type = SDL_EVENT_TEXT_INPUT;
                typed.text.windowID = SDL_GetWindowID(window);
                typed.text.text = "hello";
                gui.processEvent(typed);
            }
            if (frame == 40) {
                SDL_SetWindowSize(window, 640, 480);
                context.requestResize();
            }
            if (frame == 20) {
                SDL_SetWindowSize(gameWindow.get(), 400, 300);
                gameContext.requestResize();
            }
            const unsigned side = frame < 30 ? 64 : 96;
            // Match the editor's ordering: allocate the viewport before Image,
            // then render the scene after UI construction but before submission.
            scene.prepare(side, side);
            const auto preparedGeneration = scene.imageGeneration();
            require(scene.width() == side && scene.height() == side, "viewport preparation failed");
            if (frame == 25) {
                thumbnails.invalidate("red.tga");
            }
            thumbnails.beginFrame(fixture.assets);
            browserThumbnails.prepare(fixture.assets);
            require(gui.beginFrame(), "Could not begin frame");
            browserSource.beginFrame(fixture.assets);
            ImGui::SetNextWindowPos(ImVec2(10, 10));
            ImGui::SetNextWindowSize(ImVec2(300, 300));
            ImGui::Begin("Scene");
            ImGui::Text("Vulkan ImGui and scene texture");
            if (ImGui::Button("Input regression")) {
                ++clicks;
            }
            const auto buttonMin = ImGui::GetItemRectMin();
            const auto buttonMax = ImGui::GetItemRectMax();
            buttonPosition =
                ImVec2((buttonMin.x + buttonMax.x) * .5f, (buttonMin.y + buttonMax.y) * .5f);
            ImGui::InputText("Input text", text, sizeof(text));
            const auto textMin = ImGui::GetItemRectMin();
            const auto textMax = ImGui::GetItemRectMax();
            textPosition = ImVec2(textMin.x + 15, (textMin.y + textMax.y) * .5f);
            const auto sceneTexture = gui.texture(scene);
            if (frame == 0) {
                bool refused = false;
                try {
                    gui.presentSecondary(gameContext, sceneTexture);
                } catch (const std::runtime_error&) {
                    refused = true;
                }
                require(refused, "Secondary presentation was accepted during a UI frame");
            }
            ImGui::Image(sceneTexture, ImVec2(200, 200));
            ImGui::Image(gui.texture(image), ImVec2(32, 16));
            ImGui::Image(gui.texture(thumbnail), ImVec2(32, 32));
            const auto cachedThumbnail = thumbnails.texture(fixture.assets, "red.tga");
            require(cachedThumbnail != ImTextureID_Invalid, "Vulkan thumbnail cache failed");
            if (frame == 0) {
                bool refused = false;
                try {
                    thumbnails.invalidate("red.tga");
                } catch (const std::runtime_error&) {
                    refused = true;
                }
                require(refused, "Thumbnail invalidation during a frame was accepted");
            }
            require(thumbnails.texture(fixture.assets, "bad.txt") == ImTextureID_Invalid,
                    "Non-image thumbnail accepted");
            ImGui::Image(cachedThumbnail, ImVec2(32, 32));
            const auto materialThumbnailId = browserSource.texture(fixture.assets, "test.mat");
            require(materialThumbnailId != ImTextureID_Invalid,
                    "Vulkan browser material thumbnail failed");
            ImGui::Image(materialThumbnailId, ImVec2(32, 32));
            if (frame == 2) {
                browserSource.clear();
                require(browserSource.texture(fixture.assets, "test.mat") == ImTextureID_Invalid,
                        "cleared browser cache served a stale descriptor");
                bool refused = false;
                try {
                    browserThumbnails.prepare(fixture.assets);
                } catch (const std::runtime_error&) {
                    refused = true;
                }
                require(refused, "browser cache prepared during UI construction");
            }
            AssetManager otherRoot = fixture.assets;
            otherRoot.root /= "other";
            require(browserSource.texture(otherRoot, "test.mat") == ImTextureID_Invalid,
                    "browser cache served a descriptor from another root");
            ImGui::End();
            scene.render(side, side, {{mesh, identity, {.1f, .5f, 1, 1}}});
            require(scene.imageGeneration() == preparedGeneration,
                    "scene image was replaced after ImGui registration");
            if (frame == 10) {
                context.setVsync(false); // Must skip rather than use a stale pipeline.
            }
            if (frame == 0) {
                gui.renderFrame();
                require(gui.frameOpen(), "Finalized frame must remain protected until submission");
                bool refused = false;
                try {
                    gui.forget(scene);
                } catch (const std::runtime_error&) {
                    refused = true;
                }
                require(refused, "Finalized frame allowed descriptor destruction");
            }
            auto status = gui.endFrame();
            if (status == VulkanContext::FrameResult::Presented) {
                ++presented;
                if (frame == 0) {
                    bool refused = false;
                    try {
                        gui.presentSecondary(gameContext, ImTextureID_Invalid);
                    } catch (const std::runtime_error&) {
                        refused = true;
                    }
                    require(refused, "Unregistered secondary image was accepted");
                }
                if (gui.presentSecondary(gameContext, sceneTexture, "Game window") ==
                    VulkanContext::FrameResult::Presented) {
                    ++gamePresented;
                }
            }
            if (frame == 10) {
                require(status == VulkanContext::FrameResult::Skipped,
                        "Mid-frame change was not skipped");
                require(gui.presentSecondary(gameContext, sceneTexture, "Game window") ==
                            VulkanContext::FrameResult::Presented,
                        "Secondary presentation failed with pending main recreation");
            }
            require(ImGui::GetDrawData()->TotalVtxCount > 0, "ImGui produced no draw commands");
        }
        require(clicks == 3, "UI clicks were lost after swapchain recreation");
        require(std::string(text) == "hello", "UI text input was lost after swapchain recreation");
        require(presented >= 75 && context.swapchainGeneration() >= 3,
                "Presentation or swapchain recreation failed");
        require(gamePresented >= 75 && gameContext.swapchainGeneration() >= 2,
                "Secondary presentation or swapchain recreation failed");
        if (SDL_MinimizeWindow(window) && SDL_SyncWindow(window) &&
            (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) {
            require(gui.beginFrame(true), "Secondary frame did not start while editor minimized");
            const auto texture = gui.texture(scene);
            gui.renderFrame();
            require(gui.endFrame() == VulkanContext::FrameResult::Skipped,
                    "Minimized editor unexpectedly presented");
            require(gui.presentSecondary(gameContext, texture) ==
                        VulkanContext::FrameResult::Presented,
                    "Secondary window did not present while editor minimized");
            SDL_RestoreWindow(window);
        }
        thumbnails.clear();
        gui.forget(scene);
        gui.forget(image);
        gui.forget(thumbnail);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Vulkan ImGui integration: %s\n", error.what());
        result = 1;
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
