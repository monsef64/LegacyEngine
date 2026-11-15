// Skybox fullscreen rendering, cube and HDR sampling on a Vulkan device.
#include "assets/cubemap.hpp"
#include "rendering/vulkan_context.hpp"
#include "rendering/vulkan_scene_renderer.hpp"
#include "rendering/vulkan_scene_surface.hpp"
#include <SDL3/SDL.h>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <vector>
static void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
static std::vector<uint32_t> load(const char* name) {
    std::ifstream f(name, std::ios::binary | std::ios::ate);
    if (!f) {
        throw std::runtime_error("shader open failed");
    }
    auto n = f.tellg();
    if (n <= 0 || n % 4) {
        throw std::runtime_error("invalid shader");
    }
    std::vector<uint32_t> data(size_t(n) / 4);
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(data.data()), n)) {
        throw std::runtime_error("shader read failed");
    }
    return data;
}
int main(int argc, char** argv) {
    if (argc != 5) {
        return 1;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return 77;
    }
    auto* window = SDL_CreateWindow("Vulkan skybox", 64, 64, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (!window) {
        SDL_Quit();
        return 77;
    }
    int result = 0;
    try {
        VulkanContext ctx(window);
        VulkanSceneSurface surface(ctx.physicalDevice(),
                                   ctx.device(),
                                   ctx.graphicsQueue(),
                                   ctx.graphicsFamily(),
                                   load(argv[1]),
                                   load(argv[2]));
        bool rejected = false;
        try {
            surface.enableSkybox({}, {});
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "invalid skybox shaders accepted");
        surface.enableSkybox(load(argv[3]), load(argv[4]));
        Mat4 vp = multiply(perspective(60 * kPi / 180, 1, .1f, 10),
                           lookAt({0, 0, 2}, {0, 0, 0}, {0, 1, 0}));
        VulkanSceneSurface::SkyDraw sky{vp, {1, 0, 0, 1}, nullptr, false};
        auto pixel = [&](int x, int y) {
            auto p = surface.readPixels();
            auto n = (y * 64 + x) * 4;
            return std::vector<uint8_t>(p.begin() + n, p.begin() + n + 4);
        };
        auto render = [&](const std::vector<VulkanSceneSurface::SkyDraw>& skies) {
            surface.render(64, 64, {}, {0, 0, 0, 1}, nullptr, nullptr, skies);
            return pixel(32, 32);
        };
        auto p = render({sky});
        require(p[0] > 250 && p[1] < 3 && p[2] < 3, "solid sky tint incorrect");
        VulkanSceneSurface::SkyDraw blue{vp, {0, 0, 1, .5f}, nullptr, false};
        p = render({sky, blue});
        require(p[0] >= 126 && p[0] <= 129 && p[2] >= 126 && p[2] <= 129, "skybox fade incorrect");
        std::vector<uint8_t> faces{255, 0,   0, 255, 0,   255, 0,   255, 0, 0,   255, 255,
                                   255, 255, 0, 255, 255, 0,   255, 255, 0, 255, 255, 255};
        VulkanTexture cube(ctx.physicalDevice(),
                           ctx.device(),
                           ctx.graphicsQueue(),
                           ctx.graphicsFamily(),
                           1,
                           1,
                           faces,
                           VulkanTexture::Address::Clamp,
                           false,
                           false,
                           true);
        sky.tintOpacity = {1, 1, 1, 1};
        sky.texture = &cube;
        p = render({sky});
        require(p[0] < 3 && p[1] > 250 && p[2] > 250, "cubemap negative-Z face incorrect");
        sky.viewProjection = multiply(perspective(60 * kPi / 180, 1, .1f, 10),
                                      lookAt({0, 0, 0}, {1, 0, 0}, {0, 1, 0}));
        p = render({sky});
        require(p[0] > 250 && p[1] < 3 && p[2] < 3, "cubemap positive-X face incorrect");
        VulkanTexture hdr(ctx.physicalDevice(),
                          ctx.device(),
                          ctx.graphicsQueue(),
                          ctx.graphicsFamily(),
                          1,
                          1,
                          std::vector<float>{4, 1, .25f, 1},
                          VulkanTexture::Address::Panorama);
        sky.viewProjection = vp;
        sky.texture = &hdr;
        sky.hdr = true;
        p = render({sky});
        require(p[0] >= 229 && p[0] <= 232 && p[1] >= 185 && p[1] <= 188 && p[2] >= 121 &&
                    p[2] <= 125,
                "HDR panorama tone map incorrect");
        MeshData triangle;
        triangle.vertices = {{{-.5f, -.5f, 0}}, {{.5f, -.5f, 0}}, {{0, .5f, 0}}};
        auto mesh = surface.createMesh(triangle);
        surface.render(64, 64, {{mesh, vp, {0, 1, 0, 1}}}, {0, 0, 0, 1}, nullptr, nullptr, {sky});
        p = pixel(32, 32);
        require(p[0] < 3 && p[1] > 250 && p[2] < 3, "skybox overlapped scene geometry");
        p = pixel(0, 0);
        require(p[0] >= 229 && p[0] <= 232, "skybox did not cover background");
        sky.hdr = false;
        rejected = false;
        try {
            render({sky});
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "2D texture accepted as cubemap");
        VulkanSceneRenderer renderer(surface);
        World world;
        auto entity = world.create();
        Skybox first;
        first.color = {1, 0, 0};
        world.add(entity, first);
        FrameProfiler profiler;
        activeProfiler = &profiler;
        profiler.begin(true);
        renderer.drawTo(surface, world, vp, 64, 64);
        profiler.end();
        activeProfiler = nullptr;
        require(profiler.stats(int(ProfilePart::Skybox)).last > 0,
                "Vulkan skybox profiling not recorded");
        p = pixel(32, 32);
        require(p[0] > 250 && p[2] < 3, "ECS skybox failed");
        Skybox next;
        next.color = {0, 0, 1};
        world.add(entity, SkyboxTransition{first, next, {}, .5f, 1});
        renderer.drawTo(surface, world, vp, 64, 64);
        p = pixel(32, 32);
        require(p[0] >= 126 && p[0] <= 129 && p[2] >= 126 && p[2] <= 129,
                "ECS skybox transition failed");
        world.remove<SkyboxTransition>(entity);
        auto destination = world.create();
        world.add(destination, next);
        require(beginSkyboxFade(world, entity, destination, 1) == nullptr, "cannot begin sky fade");
        renderer.updateSkyboxFades(world, .5f);
        renderer.drawTo(surface, world, vp, 64, 64);
        p = pixel(32, 32);
        require(p[0] >= 126 && p[0] <= 129 && p[2] >= 126 && p[2] <= 129, "sky fade update failed");
        renderer.updateSkyboxFades(world, .5f);
        require(!world.get<Skybox>(entity).enabled && world.get<Skybox>(destination).enabled,
                "sky fade completion failed");
        world.get<Skybox>(entity).cubemap = "missing.cubemap";
        require(beginSkyboxFade(world, destination, entity, 1) == nullptr,
                "cannot begin invalid sky fade");
        renderer.updateSkyboxFades(world, .25f);
        require(!world.has<SkyboxTransition>(destination) && !renderer.skyboxError.empty(),
                "invalid sky destination did not cancel fade");
        world.get<Skybox>(destination).enabled = false;
        world.get<Skybox>(entity).enabled = true;
        world.get<Skybox>(entity).cubemap.clear();
        // Asset-backed cubemap and HDR decoding use project-relative assets.
        auto root = std::filesystem::temp_directory_path() /
                    ("legacy-vulkan-sky-" + std::to_string(SDL_GetTicksNS()));
        std::filesystem::create_directories(root);
        struct Cleanup {
            std::filesystem::path p;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove_all(p, ec);
            }
        } cleanup{root};
        AssetManager assets;
        assets.root = root;
        CubemapAsset asset;
        for (int i = 0; i < 6; ++i) {
            asset.faces[i] = "face" + std::to_string(i) + ".bmp";
            std::ofstream f(root / asset.faces[i], std::ios::binary);
            unsigned char bmp[58] = {'B', 'M', 58, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0, 40,
                                     0,   0,   0,  1, 0, 0, 0, 1, 0, 0, 0,  1, 0, 24};
            bmp[55] = i == 4 ? 64 : 0;
            bmp[56] = i == 4 ? 255 : 0;
            f.write(reinterpret_cast<const char*>(bmp), sizeof(bmp));
        }
        saveCubemap(assets, "sky.cubemap", asset);
        renderer.setAssetRoot(root);
        auto& component = world.get<Skybox>(entity);
        component.color = {1, 1, 1};
        component.cubemap = "sky.cubemap";
        renderer.drawTo(surface, world, vp, 64, 64);
        p = pixel(32, 32);
        require(renderer.skyboxError.empty() && p[0] > 250 && p[1] >= 62 && p[1] <= 65,
                "ECS cubemap asset failed");
        {
            std::ofstream f(root / "sky.hdr", std::ios::binary);
            f << "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 1\n";
            const unsigned char rgbe[4] = {128, 64, 32, 131};
            f.write(reinterpret_cast<const char*>(rgbe), 4);
        }
        component.type = 1;
        component.hdr = "sky.hdr";
        renderer.drawTo(surface, world, vp, 64, 64);
        p = pixel(32, 32);
        require(renderer.skyboxError.empty() && p[0] >= 229 && p[0] <= 232 && p[1] >= 211 &&
                    p[1] <= 214,
                "ECS HDR asset failed");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Vulkan sky render test: %s\n", e.what());
        result = 1;
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
