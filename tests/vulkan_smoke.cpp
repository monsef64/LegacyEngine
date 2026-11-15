// Graphical smoke test for the Vulkan presentation context.
#include "rendering/vulkan_context.hpp"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstring>
#include <exception>

int main(int argc, char** argv) {
    bool automated = false;
    if (argc == 2 && std::strcmp(argv[1], "--smoke-test") == 0) {
        automated = true;
    } else if (argc != 1) {
        std::fprintf(stderr, "Usage: %s [--smoke-test]\n", argv[0]);
        return 1;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window =
        SDL_CreateWindow("LegacyEngine Vulkan presentation test",
                         800,
                         600,
                         SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    int result = 0;
    try {
        VulkanContext context(window);
        bool running = true;
        unsigned frames = 0;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT ||
                    event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                    running = false;
                }
                if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                    event.type == SDL_EVENT_WINDOW_RESTORED ||
                    event.type == SDL_EVENT_WINDOW_MINIMIZED) {
                    context.requestResize();
                }
            }
            if (!running) {
                break;
            }
            if (context.presentClear(.07f, .08f, .10f) == VulkanContext::FrameResult::Presented) {
                ++frames;
                if (automated && frames == 30) {
                    SDL_SetWindowSize(window, 640, 480);
                }
                if (automated && frames == 60) {
                    context.setVsync(false);
                }
                if (automated && frames == 90) {
                    context.setVsync(true);
                }
                if (automated && frames >= 120) {
                    running = false;
                }
            } else {
                SDL_Delay(10);
            }
        }
        if (automated && frames < 120) {
            result = 1;
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Vulkan presentation test: %s\n", error.what());
        result = 1;
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
