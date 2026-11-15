#include "rendering/vulkan_fsr3_game.hpp"
#include "rendering/vulkan_context.hpp"
#include "rendering/vulkan_scene_renderer.hpp"
#include <SDL3/SDL.h>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <vector>

static std::vector<uint32_t> load(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        throw std::runtime_error("shader open failed");
    }
    auto n = f.tellg();
    if (n <= 0 || n % 4) {
        throw std::runtime_error("invalid shader");
    }
    std::vector<uint32_t> out(static_cast<size_t>(n) / 4);
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(out.data()), n)) {
        throw std::runtime_error("shader read failed");
    }
    return out;
}
int main(int argc, char** argv) {
    if (argc != 5) {
        return 1;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return 77;
    }
    auto* window = SDL_CreateWindow("FSR Game test", 64, 64, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (!window) {
        SDL_Quit();
        return 77;
    }
    int status = 0;
    try {
        VulkanContext vk(window);
        VulkanSceneSurface input(vk.physicalDevice(),
                                 vk.device(),
                                 vk.graphicsQueue(),
                                 vk.graphicsFamily(),
                                 load(argv[1]),
                                 load(argv[2]));
        VulkanSceneSurface output(vk.physicalDevice(),
                                  vk.device(),
                                  vk.graphicsQueue(),
                                  vk.graphicsFamily(),
                                  load(argv[1]),
                                  load(argv[2]),
                                  &input);
        input.enableMotionVectors(load(argv[3]), load(argv[4]));
        output.enableTransferDestination();
        output.prepare(128, 128);
        VulkanSceneRenderer renderer(input);
        MeshData mesh;
        mesh.vertices = {{{-.9f, -.9f, 0}}, {{.9f, -.9f, 0}}, {{0, .9f, 0}}};
        auto handle = input.createMesh(mesh);
        World world;
        auto entity = world.create();
        world.add(entity, Transform{});
        world.add(entity, MeshRenderer{handle, {1, .1f, .1f}});
        Mat4 vp{};
        vp.m[0] = vp.m[5] = vp.m[10] = vp.m[15] = 1;
        TemporalHistory history;
        TemporalHistory::Configuration config{{64, 64}, {128, 128}, 1, 1, 1};
        VulkanFsr3Game::Device device{vk.instance(),
                                      vk.physicalDevice(),
                                      vk.device(),
                                      vk.graphicsQueue(),
                                      vk.graphicsFamily(),
                                      vk.apiVersion()};
        VulkanFsr3Game fsr(device, config.render, config.display);
        for (int f = 0; f < 4; ++f) {
            world.get<Transform>(entity).position.x = f * .04f;
            renderer.drawTemporalTo(
                input, world, vp, config, history, {}, false, f == 2, [&](const auto& frame) {
                    fsr.dispatch({input.colorImage(),
                                  input.motionDepthImage(),
                                  input.motionImage(),
                                  output.colorImage(),
                                  frame.frame.jitter,
                                  frame.frame.reset || frame.missingObjectHistory,
                                  16.7f,
                                  .1f,
                                  1000.f,
                                  1.f});
                });
        }
        const auto pixels = output.readPixels();
        size_t center = (size_t(64) * 128 + 64) * 4;
        if (pixels[center] < 100 || pixels[center] <= pixels[center + 1] * 2 ||
            pixels[center] <= pixels[0] * 2) {
            throw std::runtime_error("Game FSR output does not contain the triangle");
        }
        printf(
            "Game FSR output: %u %u %u\n", pixels[center], pixels[center + 1], pixels[center + 2]);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        status = 1;
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return status;
}
