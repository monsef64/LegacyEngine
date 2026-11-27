#pragma once
#include "core/worker_pool.hpp"
#include "levels/level_system.hpp"
#include "rendering/renderer.hpp"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>

// Isolated UI context: editor Play can preload in the middle of its own ImGui
// frame. Preserve the presented editor image and only add text over Game.
class PreloadText {
    ImGuiContext* previous_ = ImGui::GetCurrentContext();
    ImGuiContext* context_ = nullptr;
    SDL_Window* previousWindow_ = SDL_GL_GetCurrentWindow();
    SDL_GLContext glContext_ = SDL_GL_GetCurrentContext();
    SDL_Window* window_;
    GLuint snapshot_ = 0;
    int width_ = 1, height_ = 1;
    ImVec2 low_, high_;

  public:
    PreloadText(SDL_Window* window, std::optional<SDL_FRect> area) : window_(window) {
        if (!SDL_GL_MakeCurrent(window_, glContext_)) {
            throw std::runtime_error(SDL_GetError());
        }
        SDL_GetWindowSizeInPixels(window_, &width_, &height_);
        width_ = std::max(1, width_);
        height_ = std::max(1, height_);
        int lw = 1, lh = 1;
        SDL_GetWindowSize(window_, &lw, &lh);
        const auto rect = area.value_or(SDL_FRect{0, 0, float(lw), float(lh)});
        low_ = {rect.x * width_ / std::max(1, lw), rect.y * height_ / std::max(1, lh)};
        high_ = {low_.x + rect.w * width_ / std::max(1, lw),
                 low_.y + rect.h * height_ / std::max(1, lh)};
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (area) {
            glGenTextures(1, &snapshot_);
            glBindTexture(GL_TEXTURE_2D, snapshot_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glReadBuffer(GL_FRONT);
            glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, width_, height_, 0);
            glReadBuffer(GL_BACK);
        }
        context_ = ImGui::CreateContext();
        ImGui::SetCurrentContext(context_);
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().LogFilename = nullptr;
        ImGui_ImplOpenGL3_Init("#version 330 core");
        ImGui::SetCurrentContext(previous_);
    }
    ~PreloadText() {
        ImGui::SetCurrentContext(context_);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui::DestroyContext(context_);
        glDeleteTextures(1, &snapshot_);
        ImGui::SetCurrentContext(previous_);
        if (previousWindow_) {
            SDL_GL_MakeCurrent(previousWindow_, glContext_);
        }
    }
    void draw() {
        ImGui::SetCurrentContext(context_);
        ImGui_ImplOpenGL3_NewFrame();
        auto& io = ImGui::GetIO();
        io.DisplaySize = {float(width_), float(height_)};
        io.DisplayFramebufferScale = {1, 1};
        io.DeltaTime = 1.f / 60;
        ImGui::NewFrame();
        auto* draw = ImGui::GetBackgroundDrawList();
        if (snapshot_) {
            draw->AddImage(
                static_cast<ImTextureID>(snapshot_), {0, 0}, io.DisplaySize, {0, 1}, {1, 0});
        } else {
            draw->AddRectFilled({0, 0}, io.DisplaySize, IM_COL32(0, 0, 0, 255));
        }
        draw->PushClipRect(low_, high_, true);
        const auto size = ImGui::CalcTextSize("Loading..");
        const ImVec2 pos{(low_.x + high_.x - size.x) * .5f, (low_.y + high_.y - size.y) * .5f};
        draw->AddText({pos.x + 1, pos.y + 1}, IM_COL32(0, 0, 0, 255), "Loading..");
        draw->AddText(pos, IM_COL32(255, 255, 255, 255), "Loading..");
        draw->PopClipRect();
        ImGui::Render();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width_, height_);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window_);
        ImGui::SetCurrentContext(previous_);
    }
};

// Startup/editor-Play loading screen. During level transitions LevelSystem::poll
// uses the normal frame loop instead. No scripts run against half-warmed assets.
inline LevelSystem::Transition preloadBeforePlay(const AssetManager& assets,
                                                 const ProjectIO::Meshes& meshes,
                                                 std::string scene,
                                                 Renderer& renderer,
                                                 SDL_Window* window,
                                                 std::optional<SDL_FRect> gameArea = std::nullopt) {
    SDL_Log("Play preload: preparing scene assets");
    auto job = WorkerPool::shared().submit([assets, meshes, scene = std::move(scene)]() mutable {
        return LevelSystem::prepare(assets, meshes, "", std::move(scene));
    });
    if (!job) {
        throw std::runtime_error("Preload worker queue is full");
    }
    std::optional<LevelSystem::Transition> prepared;
    SDL_Log("Play preload: initializing loading overlay");
    PreloadText text(window, gameArea);
    const auto started = SDL_GetTicks();
    for (;;) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                SDL_PushEvent(&event);
                throw std::runtime_error("Loading cancelled by window close");
            }
        }
        if (!prepared && job->wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            prepared = job->get();
            SDL_Log("Play preload: scene prepared (%zu dependencies); uploading GPU assets",
                    prepared->assets.size());
        }
        if (prepared && renderer.preloadAssets(prepared->assets)) {
            SDL_Log("Play preload: assets ready; closing loading overlay");
            return std::move(*prepared);
        }
        if (SDL_GetTicks() - started > 120000) {
            throw std::runtime_error(
                "Asset preloading timed out (resource/cache budget may be exceeded)");
        }
        text.draw();
        SDL_Delay(1);
    }
}
