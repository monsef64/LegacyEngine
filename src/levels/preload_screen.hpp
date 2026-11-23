#pragma once
#include "core/worker_pool.hpp"
#include "levels/level_system.hpp"
#include "rendering/renderer.hpp"
#include <SDL3/SDL.h>

// Startup/editor-Play loading screen. During level transitions LevelSystem::poll
// uses the normal frame loop instead. No scripts run against half-warmed assets.
inline LevelSystem::Transition preloadBeforePlay(const AssetManager& assets,
                                                 const ProjectIO::Meshes& meshes,
                                                 std::string scene,
                                                 Renderer& renderer,
                                                 SDL_Window* window) {
    auto job = WorkerPool::shared().submit([assets, meshes, scene = std::move(scene)]() mutable {
        return LevelSystem::prepare(assets, meshes, "", std::move(scene));
    });
    if (!job) {
        throw std::runtime_error("Preload worker queue is full");
    }
    std::optional<LevelSystem::Transition> prepared;
    const auto started = SDL_GetTicks();
    for (;;) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                 event.window.windowID == SDL_GetWindowID(window))) {
                SDL_PushEvent(&event);
                throw std::runtime_error("Loading cancelled by window close");
            }
        }
        if (!prepared && job->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            prepared = job->get();
        }
        if (prepared && renderer.preloadAssets(prepared->assets)) {
            return std::move(*prepared);
        }
        if (SDL_GetTicks() - started > 120000) {
            throw std::runtime_error(
                "Asset preloading timed out (resource/cache budget may be exceeded)");
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        int width = 1, height = 1;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        glViewport(0, 0, width, height);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(.035f, .045f, .06f, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        // Small animated progress indicator without requiring fonts/UI assets.
        glEnable(GL_SCISSOR_TEST);
        glScissor(width / 4,
                  height / 2,
                  std::max(1, int((width / 2) * float(SDL_GetTicks() % 1200) / 1200)),
                  4);
        glClearColor(.2f, .65f, .9f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_SCISSOR_TEST);
        SDL_GL_SwapWindow(window);
        SDL_Delay(1);
    }
}
