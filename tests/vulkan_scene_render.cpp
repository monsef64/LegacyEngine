// Vulkan offscreen rendering/readback integration test. Needs SDL Vulkan and a
// real Vulkan implementation; works with SwiftShader's headless surface too.
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

static std::vector<uint32_t> load(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Cannot open shader");
    }
    auto size = file.tellg();
    if (size <= 0 || size % 4 != 0) {
        throw std::runtime_error("Invalid SPIR-V");
    }
    std::vector<uint32_t> words(static_cast<size_t>(size) / 4);
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(words.data()), size)) {
        throw std::runtime_error("Cannot read shader");
    }
    return words;
}
static void require(bool value, const char* message) {
    if (!value) {
        throw std::runtime_error(message);
    }
}
int main(int argc, char** argv) {
    if (argc != 3) {
        return 1;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return 77;
    }
    SDL_Window* window =
        SDL_CreateWindow("Vulkan scene test", 64, 64, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (!window) {
        SDL_Quit();
        return 77;
    }
    int result = 0;
    try {
        VulkanContext context(window);
        VulkanSceneSurface surface(context.physicalDevice(),
                                   context.device(),
                                   context.graphicsQueue(),
                                   context.graphicsFamily(),
                                   load(argv[1]),
                                   load(argv[2]));
        auto triangle = [](float z) {
            MeshData mesh;
            mesh.vertices = {{{-.8f, -.8f, z}}, {{.8f, -.8f, z}}, {{0, .8f, z}}};
            return mesh;
        };
        VulkanSceneSurface gameSurface(context.physicalDevice(),
                                       context.device(),
                                       context.graphicsQueue(),
                                       context.graphicsFamily(),
                                       load(argv[1]),
                                       load(argv[2]),
                                       &surface);
        const auto near = surface.createMesh(triangle(0));
        const auto far = surface.createMesh(triangle(.8f));
        const auto closer = surface.createMesh(triangle(-.5f));
        Mat4 identity{};
        identity.m[0] = identity.m[5] = identity.m[10] = identity.m[15] = 1;
        using Draw = VulkanSceneSurface::Draw;
        const Draw blue{near, identity, {0, 0, 1, 1}};
        const Draw red{far, identity, {1, 0, 0, 1}};
        const Draw green{closer, identity, {0, 1, 0, .5f}, false, true};
        const auto between = surface.createMesh(triangle(-.25f));
        const Draw translucentRed{between, identity, {1, 0, 0, .5f}, false, true};
        surface.render(64, 64, {blue, red}, {0, 0, 0, 1});
        require(surface.imageView() && surface.sampler() && surface.imageGeneration() == 1,
                "missing image");
        auto pixels = surface.readPixels();
        auto pixel = [&](unsigned x, unsigned y, unsigned channel) {
            return pixels[(y * 64 + x) * 4 + channel];
        };
        require(pixel(32, 32, 0) < 4 && pixel(32, 32, 1) < 4 && pixel(32, 32, 2) > 250,
                "triangle winding or depth test failed");
        require(pixel(0, 0, 0) == 0 && pixel(0, 0, 1) == 0 && pixel(0, 0, 2) == 0, "clear failed");
        // Top-left viewport rectangles affect geometry while the entire image is cleared.
        VulkanSceneSurface::RenderRegion region{8, 12, 32, 24};
        surface.render(64, 64, {blue}, {0, 0, 0, 1}, nullptr, nullptr, {}, &region);
        pixels = surface.readPixels();
        require(pixel(24, 24, 2) > 250 && pixel(48, 24, 2) == 0 && pixel(24, 48, 2) == 0,
                "camera viewport rectangle failed");
        bool badRegion = false;
        VulkanSceneSurface::RenderRegion invalidRegion{63, 0, 2, 8};
        try {
            surface.render(64, 64, {blue}, {}, nullptr, nullptr, {}, &invalidRegion);
        } catch (const std::runtime_error&) {
            badRegion = true;
        }
        require(badRegion, "out-of-bounds viewport accepted");
        surface.render(64, 64, {blue, red}, {0, 0, 0, 1});
        // Scene and game surfaces can use the same mesh handles without
        // replacing each other's color images.
        gameSurface.render(32, 32, {{near, identity, {0, 1, 0, 1}}}, {0, 0, 0, 1});
        auto gamePixels = gameSurface.readPixels();
        require(gamePixels[(16 * 32 + 16) * 4 + 1] > 250, "shared mesh render failed");
        pixels = surface.readPixels();
        require(pixel(32, 32, 2) > 250, "second viewport replaced scene image");
        auto temporary = gameSurface.createMesh(triangle(0));
        Vec3 low{}, high{};
        require(gameSurface.meshBounds(temporary, low, high) && low.x == -.8f && high.y == .8f,
                "shared mesh bounds incorrect");
        MeshData replacement = triangle(.2f);
        replacement.vertices[0].position.x = -2;
        surface.updateMesh(temporary, replacement);
        require(gameSurface.meshBounds(temporary, low, high) && low.x == -2 && low.z == .2f,
                "updated mesh bounds incorrect");
        surface.destroyMesh(temporary);
        require(!gameSurface.meshBounds(temporary, low, high), "destroyed mesh has bounds");
        MeshData emptyMesh;
        auto emptyHandle = surface.createMesh(emptyMesh);
        require(!surface.meshBounds(emptyHandle, low, high), "empty mesh has bounds");
        surface.destroyMesh(emptyHandle);
        bool staleRejected = false;
        try {
            gameSurface.render(32, 32, {{temporary, identity, {1, 1, 1, 1}}});
        } catch (const std::runtime_error&) {
            staleRejected = true;
        }
        require(staleRejected, "destroyed shared mesh handle accepted");
        const auto next = surface.createMesh(triangle(0));
        require(next.id != temporary.id, "stale handle was reused");
        surface.destroyMesh(next);
        surface.render(64, 64, {blue, red, green}, {0, 0, 0, 1});
        require(surface.imageGeneration() == 1, "unnecessary image recreation");
        pixels = surface.readPixels();
        require(pixel(32, 32, 1) >= 125 && pixel(32, 32, 1) <= 130 && pixel(32, 32, 2) >= 125 &&
                    pixel(32, 32, 2) <= 130,
                "alpha blending failed");
        surface.render(64, 64, {blue, green, translucentRed}, {0, 0, 0, 1});
        pixels = surface.readPixels();
        require(pixel(32, 32, 0) >= 125 && pixel(32, 32, 0) <= 130 && pixel(32, 32, 1) >= 62 &&
                    pixel(32, 32, 1) <= 66 && pixel(32, 32, 2) >= 62 && pixel(32, 32, 2) <= 66,
                "transparent depth write failed");
        Mat4 mirror = identity;
        mirror.m[0] = -1;
        surface.render(64, 64, {{near, mirror, {1, 0, 0, 1}, true}}, {0, 0, 0, 1});
        pixels = surface.readPixels();
        require(pixel(32, 32, 0) > 250 && pixel(32, 32, 2) < 4, "mirrored culling failed");
        MeshData wire;
        wire.topology = MeshData::Topology::Lines;
        wire.vertices = {{{-.8f, 0, -.5f}}, {{.8f, 0, -.5f}}};
        const auto line = surface.createMesh(wire);
        surface.render(64, 64, {blue, {line, identity, {1, 1, 1, 1}}}, {0, 0, 0, 1});
        pixels = surface.readPixels();
        bool white = false;
        for (unsigned y = 30; y <= 33; ++y) {
            white |= pixel(32, y, 0) > 250 && pixel(32, y, 1) > 250;
        }
        require(white, "line rendering failed");
        World world;
        auto object = world.create();
        world.add(object, Transform{});
        world.add(object, MeshRenderer{near, {0, 0, 1}});
        auto hidden = world.create();
        world.add(hidden, Transform{{0, 0, -.5f}});
        world.add(hidden, MeshRenderer{near, {1, 0, 0}});
        world.add(hidden, ObjectActivity{false});
        VulkanSceneRenderer renderer(surface);
        renderer.draw(world, identity, 64, 64);
        pixels = surface.readPixels();
        require(pixel(32, 32, 2) > 250 && pixel(32, 32, 0) < 4, "ECS rendering/activity failed");
        world.get<MeshRenderer>(object).material = "not-yet-supported.mat";
        bool rejected = false;
        try {
            renderer.draw(world, identity, 64, 64);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "unsupported material should not be rendered silently");
        world.get<MeshRenderer>(object).material.clear();
        struct Fixture {
            std::filesystem::path path =
                std::filesystem::temp_directory_path() /
                ("legacy-vulkan-model-" + std::to_string(SDL_GetTicksNS()));
            ~Fixture() {
                std::error_code ignored;
                std::filesystem::remove_all(path, ignored);
            }
        } fixture;
        std::filesystem::create_directories(fixture.path);
        auto writeModel = [&](float left, float right) {
            Json model = {{"format", "LegacyEngineMesh"},
                          {"version", 1},
                          {"vertices",
                           {{left, -.8f, 0, 0, 0, 1, 0, 0},
                            {right, -.8f, 0, 0, 0, 1, 1, 0},
                            {(left + right) / 2, .8f, 0, 0, 0, 1, .5f, 1}}}};
            writeJsonFile(fixture.path / "triangle.lemesh", model);
        };
        writeModel(-.8f, .8f);
        renderer.setAssetRoot(fixture.path);
        AssetManager modelAssets;
        modelAssets.root = fixture.path;
        require(renderer.modelBounds(modelAssets, "triangle.lemesh", low, high) && low.x == -.8f &&
                    high.x == .8f,
                "static model preview bounds incorrect");
        // A mesh not already loaded by modelBounds is decoded on a worker.
        std::filesystem::copy_file(fixture.path / "triangle.lemesh",
                                   fixture.path / "streamed.lemesh");
        world.get<MeshRenderer>(object).model = "streamed.lemesh";
        renderer.draw(world, identity, 64, 64);
        require(renderer.pendingStreaming(), "model streaming was not scheduled");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (renderer.pendingStreaming() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            renderer.draw(world, identity, 64, 64);
        }
        require(!renderer.pendingStreaming(), "model streaming timed out");
        pixels = surface.readPixels();
        require(pixel(32, 32, 2) > 250, "streamed model did not render");
        // Invalidate a queued job before switching project roots. A completion
        // from the old root must never populate the new project's cache.
        std::filesystem::copy_file(fixture.path / "triangle.lemesh",
                                   fixture.path / "switch.lemesh");
        world.get<MeshRenderer>(object).model = "switch.lemesh";
        renderer.draw(world, identity, 64, 64);
        require(renderer.pendingStreaming(), "root-switch test did not queue a model");
        const auto otherRoot = fixture.path / "other";
        std::filesystem::create_directories(otherRoot);
        writeJsonFile(otherRoot / "switch.lemesh", Json::parse(R"({
            "format":"LegacyEngineMesh", "version":1,
            "vertices":[[0.82,-0.8,0,0,0,1,0,0],[0.98,-0.8,0,0,0,1,1,0],[0.9,0.8,0,0,0,1,0.5,1]]
        })"));
        renderer.setAssetRoot(otherRoot);
        renderer.draw(world, identity, 64, 64);
        const auto switchDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (renderer.pendingStreaming() && std::chrono::steady_clock::now() < switchDeadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            renderer.draw(world, identity, 64, 64);
        }
        require(!renderer.pendingStreaming(), "root-switch streaming timed out");
        pixels = surface.readPixels();
        require(pixel(32, 32, 2) < 100, "old project mesh survived a root switch");
        renderer.setAssetRoot(fixture.path);
        require(renderer.modelBounds(modelAssets, "triangle.lemesh", low, high),
                "model bounds not restored after root switch");
        world.get<MeshRenderer>(object).model = "triangle.lemesh";
        renderer.draw(world, identity, 64, 64);
        pixels = surface.readPixels();
        require(pixel(32, 32, 2) > 250, "Vulkan static model asset not rendered");
        renderer.drawTo(gameSurface, world, identity, 32, 32);
        gamePixels = gameSurface.readPixels();
        require(gamePixels[(16 * 32 + 16) * 4 + 2] > 250,
                "Vulkan model cache not shared with game view");
        const auto rig = Json::parse(R"({
            "format":"LegacyEngineMesh", "version":2, "parts":[{"mesh":"part.lemesh"}],
            "nodes":[{"parent":-1,"matrix":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],
                      "position":[0,0,0],"scale":[1,1,1],"rotation":[0,0,0,1]}],
            "animations":[{"channels":[{"node":0,"positions":[[0,0,0,0],[1,2,0,0]],
                                        "rotations":[],"scales":[]}]}]})");
        const auto part = Json::parse(R"({
            "format":"LegacyEngineMesh","version":1,"settings":"rig.lemesh","node":0,
            "bones":[{"node":0,"offset":[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]}],
            "weights":[[[0,1]],[[0,1]],[[0,1]]],
            "vertices":[[-0.6,-0.6,0,0,0,1,0,0],[0.6,-0.6,0,0,0,1,1,0],[0,0.6,0,0,0,1,0.5,1]]})");
        writeJsonFile(fixture.path / "rig.lemesh", rig);
        writeJsonFile(fixture.path / "part.lemesh", part);
        require(renderer.modelBounds(modelAssets, "rig.lemesh", low, high) && low.x == -.6f &&
                    high.x == .6f,
                "multipart model preview bounds incorrect");
        world.get<MeshRenderer>(object).model = "part.lemesh";
        Animation animation;
        animation.poseActive = true;
        animation.model = "rig.lemesh";
        animation.modelAnimation = 0;
        world.add(object, animation);
        renderer.draw(world, identity, 64, 64);
        pixels = surface.readPixels();
        require(pixel(32, 32, 2) > 250, "Vulkan animated bind frame failed");
        world.get<Animation>(object).time = 1;
        renderer.draw(world, identity, 64, 64);
        pixels = surface.readPixels();
        require(pixel(32, 32, 2) < 100, "Vulkan animated pose was not updated");
        renderer.drawTo(gameSurface, world, identity, 32, 32);
        gamePixels = gameSurface.readPixels();
        require(gamePixels[(16 * 32 + 16) * 4 + 2] < 100, "animated pose not shared with game");
        world.get<Animation>(object).blendWeight = 0;
        world.get<Animation>(object).blendSourceAnimation = 0;
        world.get<Animation>(object).blendSourceTime = 0;
        renderer.draw(world, identity, 64, 64);
        pixels = surface.readPixels();
        require(pixel(32, 32, 2) > 250, "Vulkan animation crossfade failed");
        world.get<Animation>(object).poseActive = false;
        world.get<MeshRenderer>(object).model = "triangle.lemesh";
        writeModel(.82f, .98f);
        ++modelAssetRevision;
        require(renderer.modelBounds(modelAssets, "triangle.lemesh", low, high) && low.x == .82f,
                "model bounds not invalidated after revision");
        renderer.draw(world, identity, 64, 64);
        pixels = surface.readPixels();
        require(pixel(32, 32, 2) < 100, "model revision did not reload GPU geometry");
        renderer.drawTo(gameSurface, world, identity, 32, 32);
        gamePixels = gameSurface.readPixels();
        require(gamePixels[(16 * 32 + 16) * 4 + 2] < 100,
                "reloaded model was not reflected in game view");
        renderer.clearModelCache();
        rejected = false;
        try {
            surface.render(64, 64, {{MeshHandle{9999}, identity, {1, 1, 1, 1}}});
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "invalid mesh handle should be rejected");
        surface.render(32, 16, {blue}, {0, 0, 0, 1});
        require(surface.imageGeneration() == 2 && surface.readPixels().size() == 32 * 16 * 4,
                "resize failed");
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Vulkan scene render test: %s\n", error.what());
        result = 1;
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
