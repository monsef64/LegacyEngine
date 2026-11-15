// Vulkan lit-material integration/readback test; works with SDL offscreen and SwiftShader.
#include "rendering/vulkan_context.hpp"
#include "rendering/vulkan_scene_renderer.hpp"
#include "rendering/vulkan_scene_surface.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <vector>

static void require(bool ok, const char* what) {
    if (!ok) {
        throw std::runtime_error(what);
    }
}
static std::vector<uint32_t> load(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Cannot open SPIR-V");
    }
    const auto size = file.tellg();
    if (size <= 0 || size % 4) {
        throw std::runtime_error("Invalid SPIR-V");
    }
    std::vector<uint32_t> words(static_cast<size_t>(size) / 4);
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(words.data()), size)) {
        throw std::runtime_error("Cannot read SPIR-V");
    }
    return words;
}
int main(int argc, char** argv) {
    if (argc != 5) {
        return 1;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return 77;
    }
    SDL_Window* window =
        SDL_CreateWindow("Vulkan lit test", 64, 64, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
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
            surface.enableLighting({}, {});
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "Invalid lighting shaders accepted");
        surface.enableLighting(load(argv[3]), load(argv[4]));
        rejected = false;
        try {
            surface.enableLighting(load(argv[3]), load(argv[4]));
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "Lighting initialized twice");
        MeshData triangle;
        triangle.vertices = {{{-.9f, -.9f, 0}, {0, 0, 1}, 0, 0},
                             {{.9f, -.9f, 0}, {0, 0, 1}, 1, 0},
                             {{0, .9f, 0}, {0, 0, 1}, .5f, 1}};
        const auto mesh = surface.createMesh(triangle);
        Mat4 identity{};
        identity.m[0] = identity.m[5] = identity.m[10] = identity.m[15] = 1;
        VulkanSceneSurface::Draw draw{mesh, identity, {1, 1, 1, 1}};
        draw.lit = true;
        draw.model = draw.normalMatrix = identity;
        VulkanShaderLayout::Lighting light{};
        light.cameraPositionAndSpecular = {0, 0, 2, 0};
        auto pixel = [&]() {
            auto p = surface.readPixels();
            return std::vector<uint8_t>(p.begin() + (32 * 64 + 32) * 4,
                                        p.begin() + (32 * 64 + 32) * 4 + 4);
        };
        auto render = [&]() {
            surface.render(64, 64, {draw}, {0, 0, 0, 1}, &light);
            return pixel();
        };
        auto p = render();
        require(p[0] >= 29 && p[0] <= 32 && p[1] == p[0], "ambient lighting incorrect");
        light.lightCount = 1;
        light.lightPosition[0] = {0, 0, 0, 0}; // directional
        light.lightDirection[0] = {0, 0, -1, 0};
        light.lightColor[0] = {.5f, .25f, .0f, 20};
        p = render();
        require(p[0] >= 155 && p[0] <= 160 && p[1] >= 92 && p[1] <= 97 && p[2] >= 29 && p[2] <= 32,
                "directional lighting incorrect");
        draw.specular = .5f;
        p = render();
        require(p[0] > 210, "specular highlight incorrect");
        draw.specular = 0;
        draw.normalMatrix.m[10] = -1;
        p = render();
        require(p[0] >= 29 && p[0] <= 32, "normal matrix transform incorrect");
        draw.normalMatrix = identity;
        light.lightDirection[0] = {0, 0, 1, 0};
        p = render();
        require(p[0] >= 29 && p[0] <= 32, "back-facing light incorrect");
        light.lightPosition[0] = {0, 0, 2, 1}; // point
        light.lightColor[0] = {.5f, .5f, .5f, 10};
        p = render();
        require(p[0] > 90 && p[0] < 160, "point lighting incorrect");
        light.lightPosition[0].w = 2; // spot
        light.lightDirection[0] = {0, 0, -1, .8f};
        light.lightShadow[0].z = .9f;
        auto spot = render();
        require(spot[0] > 90, "spot lighting incorrect");
        light.lightDirection[0].z = 1;
        p = render();
        require(p[0] >= 29 && p[0] <= 32, "spot cone clipping incorrect");
        light.lightPosition[0].w = 0;
        light.lightDirection[0] = {0, 0, -1, 0};
        light.lightColor[0] = {.88f, .88f, .88f, 10};
        VulkanTexture red(ctx.physicalDevice(),
                          ctx.device(),
                          ctx.graphicsQueue(),
                          ctx.graphicsFamily(),
                          1,
                          1,
                          std::vector<uint8_t>{255, 0, 0, 128},
                          VulkanTexture::Address::Repeat);
        draw.materialTexture = &red;
        p = render();
        require(p[0] > 248 && p[1] < 3 && p[2] < 3, "material texture incorrect");
        VulkanTexture reverseNormal(ctx.physicalDevice(),
                                    ctx.device(),
                                    ctx.graphicsQueue(),
                                    ctx.graphicsFamily(),
                                    1,
                                    1,
                                    std::vector<uint8_t>{128, 128, 0, 255},
                                    VulkanTexture::Address::Repeat);
        draw.normalTexture = &reverseNormal;
        p = render();
        require(p[0] >= 29 && p[0] <= 32, "normal map incorrect");
        draw.normalTexture = nullptr;
        VulkanSceneSurface::Draw background{mesh, identity, {0, 0, 1, 1}};
        background.viewProjectionModel.m[14] = .5f;
        draw.transparent = true;
        surface.render(64, 64, {background, draw}, {0, 0, 0, 1}, &light);
        p = pixel();
        require(p[0] >= 125 && p[0] <= 130 && p[2] >= 125 && p[2] <= 130,
                "lit transparency incorrect");
        struct Fixture {
            std::filesystem::path path = std::filesystem::temp_directory_path() /
                                         ("legacy-vulkan-lit-" + std::to_string(SDL_GetTicksNS()));
            ~Fixture() {
                std::error_code ignored;
                std::filesystem::remove_all(path, ignored);
            }
        } fixture;
        std::filesystem::create_directories(fixture.path);
        {
            std::ofstream out(fixture.path / "green.tga", std::ios::binary);
            unsigned char header[18]{};
            header[2] = 2;
            header[12] = 2;
            header[14] = 2;
            header[16] = 24;
            out.write(reinterpret_cast<const char*>(header), sizeof(header));
            for (int i = 0; i < 4; ++i) {
                out.put(0);
                out.put(char(255));
                out.put(0);
            }
            require(bool(out), "cannot write image fixture");
        }
        AssetManager assets;
        assets.root = fixture.path;
        MaterialProperties material;
        material.texture = "green.tga";
        saveMaterial(assets, "green.mat", material);
        World world;
        auto entity = world.create();
        world.add(entity, Transform{});
        MeshRenderer component{mesh};
        component.material = "green.mat";
        world.add(entity, component);
        auto lightEntity = world.create();
        world.add(lightEntity, Transform{});
        DirectionalLight sun;
        sun.intensity = .88f;
        sun.shadows = false;
        world.add(lightEntity, sun);
        VulkanSceneRenderer renderer(surface);
        renderer.setAssetRoot(fixture.path);
        FrameProfiler profiler;
        activeProfiler = &profiler;
        profiler.begin(true);
        renderer.drawLitTo(surface, world, identity, 64, 64, {0, 0, 2});
        profiler.end();
        activeProfiler = nullptr;
        require(profiler.stats(int(ProfilePart::Lights)).last > 0 &&
                    profiler.stats(int(ProfilePart::Surfaces)).last > 0,
                "Vulkan light/surface profiling not recorded");
        require(renderer.pendingStreaming(), "material decode was not scheduled in background");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (renderer.pendingStreaming() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            renderer.drawLitTo(surface, world, identity, 64, 64, {0, 0, 2});
        }
        require(!renderer.pendingStreaming(), "material streaming timed out");
        p = pixel();
        require(p[1] > 248 && p[0] < 3 && p[2] < 3, "ECS material or light not rendered");
        world.get<MeshRenderer>(entity).materialOverrides = MaterialColor;
        world.get<MeshRenderer>(entity).materialValues.color = {1, .5f, 1};
        renderer.drawLitTo(surface, world, identity, 64, 64, {0, 0, 2});
        p = pixel();
        require(p[1] >= 125 && p[1] <= 130, "material override not applied");

        // Clearing a cache while a decode is in flight must not publish pixels
        // from the old project into a different root with the same asset name.
        const auto otherRoot = fixture.path / "other";
        std::filesystem::create_directories(otherRoot);
        const unsigned char smallTga[]{0, 0, 2, 0, 0, 0,  0,    0, 0,   0, 0,
                                       0, 1, 0, 1, 0, 24, 0x20, 0, 255, 0};
        {
            std::ofstream out(otherRoot / "green.tga", std::ios::binary);
            out.write(reinterpret_cast<const char*>(smallTga), sizeof(smallTga));
        }
        AssetManager otherAssets;
        otherAssets.root = otherRoot;
        VulkanMaterialTextures cache(surface);
        cache.beginFrame();
        require(!cache.get(assets, "green.tga") && cache.pending(), "old texture did not schedule");
        require(!cache.get(otherAssets, "green.tga"), "new root was not scheduled");
        const auto switchDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        const VulkanTexture* streamed = nullptr;
        while (!streamed && std::chrono::steady_clock::now() < switchDeadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            cache.beginFrame();
            streamed = cache.get(otherAssets, "green.tga");
        }
        require(streamed && streamed->width() == 1 && streamed->height() == 1,
                "old project texture survived a root switch");
        std::ofstream(otherRoot / "broken.tga") << "bad data";
        require(!cache.get(otherAssets, "broken.tga"), "invalid texture was not scheduled");
        bool invalidRejected = false;
        const auto errorDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!invalidRejected && std::chrono::steady_clock::now() < errorDeadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            cache.beginFrame();
            try {
                cache.get(otherAssets, "broken.tga");
            } catch (const std::runtime_error&) {
                invalidRejected = true;
            }
        }
        require(invalidRejected, "invalid asynchronous texture did not report an error");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Vulkan lit test: %s\n", e.what());
        result = 1;
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
