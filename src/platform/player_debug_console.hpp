#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <deque>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

// Export-only diagnostics, not a gameplay GUI or Lua command execution API.
class PlayerDebugConsole {
    struct Line {
        SDL_LogPriority priority;
        std::string text;
    };
    SDL_LogOutputFunction previousLog_ = nullptr;
    void* previousUser_ = nullptr;
    ImGuiContext* context_ = nullptr;
    std::mutex mutex_;
    std::deque<Line> lines_;
    bool visible_ = false, scroll_ = true;
    void setVisible(bool visible) {
        visible_ = visible;
        auto* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(context_);
        ImGui::GetIO().ClearEventsQueue();
        ImGui::GetIO().ClearInputKeys();
        ImGui::GetIO().ClearInputMouse();
        ImGui::SetCurrentContext(previous);
        if (!visible) {
            SDL_CaptureMouse(false);
        }
    }
    static void SDLCALL log(void* user,
                            int category,
                            SDL_LogPriority priority,
                            const char* message) {
        auto& self = *static_cast<PlayerDebugConsole*>(user);
        if (self.previousLog_) {
            self.previousLog_(self.previousUser_, category, priority, message);
        }
        // SDL can log on worker threads. Never throw across the C callback.
        try {
            std::lock_guard<std::mutex> lock(self.mutex_);
            if (self.lines_.size() >= 512) {
                self.lines_.pop_front();
            }
            self.lines_.push_back(
                {priority,
                 std::string(message ? message : "",
                             message ? std::min<std::size_t>(std::strlen(message), 8192) : 0)});
        } catch (...) {
        }
    }

  public:
    PlayerDebugConsole(SDL_Window* window, bool enabled) {
        if (!enabled) {
            return;
        }
        auto* previous = ImGui::GetCurrentContext();
        context_ = ImGui::CreateContext();
        ImGui::SetCurrentContext(context_);
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().LogFilename = nullptr;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        ImGui::StyleColorsDark();
        const bool platform = ImGui_ImplSDL3_InitForOpenGL(window, SDL_GL_GetCurrentContext());
        const bool renderer = platform && ImGui_ImplOpenGL3_Init("#version 330 core");
        if (!renderer) {
            if (platform) {
                ImGui_ImplSDL3_Shutdown();
            }
            ImGui::DestroyContext(context_);
            context_ = nullptr;
            ImGui::SetCurrentContext(previous);
            throw std::runtime_error("Cannot initialize exported debug console");
        }
        ImGui::SetCurrentContext(previous);
        SDL_GetLogOutputFunction(&previousLog_, &previousUser_);
        SDL_SetLogOutputFunction(log, this);
        SDL_Log("Debug Mode enabled: press ` to toggle the log console");
    }
    ~PlayerDebugConsole() {
        if (!context_) {
            return;
        }
        SDL_SetLogOutputFunction(previousLog_, previousUser_);
        auto* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(context_);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext(context_);
        ImGui::SetCurrentContext(previous == context_ ? nullptr : previous);
    }
    PlayerDebugConsole(const PlayerDebugConsole&) = delete;
    PlayerDebugConsole& operator=(const PlayerDebugConsole&) = delete;
    bool visible() const {
        return visible_;
    }
    void resetInput() {
        if (context_) {
            setVisible(visible_);
        }
    }
    bool event(const SDL_Event& event) {
        if (!context_) {
            return false;
        }
        // Keep backend button/window bookkeeping current while hidden, without
        // accumulating an unbounded ImGui input queue between console frames.
        auto* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(context_);
        ImGui_ImplSDL3_ProcessEvent(&event);
        if (!visible_) {
            ImGui::GetIO().ClearEventsQueue();
        }
        ImGui::SetCurrentContext(previous);
        if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
            event.key.scancode == SDL_SCANCODE_GRAVE) {
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                setVisible(!visible_);
            }
            return true;
        }
        if (!visible_) {
            return false;
        }
        // Preserve the player's fullscreen shortcut while the log window is open.
        if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
            event.key.key == SDLK_F11) {
            return false;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
            setVisible(false);
            return true;
        }
        return event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP ||
               event.type == SDL_EVENT_TEXT_INPUT || event.type == SDL_EVENT_TEXT_EDITING ||
               event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_WHEEL ||
               event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP;
    }
    // Caller has bound the game's framebuffer. SDL supplies logical/pixel scaling.
    void draw() {
        if (!visible_) {
            return;
        }
        auto* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(context_);
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        const auto size = ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(size.x, std::max(1.f, size.y * .6f)), ImGuiCond_Always);
        if (ImGui::Begin("Debug Console (` / Escape to close)",
                         &visible_,
                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoCollapse)) {
            if (ImGui::Button("Clear")) {
                std::lock_guard<std::mutex> lock(mutex_);
                lines_.clear();
            }
            ImGui::SameLine();
            ImGui::Checkbox("Auto-scroll", &scroll_);
            ImGui::SameLine();
            ImGui::TextDisabled("Read-only logs; no script execution");
            std::vector<Line> lines;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                lines.assign(lines_.begin(), lines_.end());
            }
            ImGui::BeginChild("Logs", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
            const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1;
            for (const auto& line : lines) {
                const bool warning = line.priority >= SDL_LOG_PRIORITY_WARN;
                if (warning) {
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          line.priority >= SDL_LOG_PRIORITY_ERROR
                                              ? ImVec4(1, .4f, .4f, 1)
                                              : ImVec4(1, .8f, .3f, 1));
                }
                ImGui::TextUnformatted(line.text.c_str());
                if (warning) {
                    ImGui::PopStyleColor();
                }
            }
            if (scroll_ && atBottom) {
                ImGui::SetScrollHereY(1.f);
            }
            ImGui::EndChild();
        }
        ImGui::End();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        ImGui::SetCurrentContext(previous);
        if (!visible_) {
            setVisible(false);
        }
    }
};
