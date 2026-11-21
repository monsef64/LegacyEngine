#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <filesystem>
struct lua_State;
struct ImGuiContext;

// Independent ImGui context drawn into the Game surface, never the editor context.
class ScriptGUI {
  public:
    ~ScriptGUI();
    void shutdown();
    void viewport(float x, float y, float w, float h, int pw, int ph);
    bool event(const SDL_Event& event, SDL_Window* window, bool focused, bool locked);
    void render(SDL_Window* window,
                int width,
                int height,
                bool playing,
                const std::filesystem::path& assets);
    bool capturesKeyboard() const;
    bool capturesMouse() const;
    static void install(lua_State* state);
    static bool inFrame();
    static void beginScope(std::uintptr_t owner, int listener);
    static void endScope();

  private:
    ImGuiContext* context_ = nullptr;
    SDL_Window* window_ = nullptr;
    float x_ = 0, y_ = 0, w_ = 1, h_ = 1, mx_ = -1e10f, my_ = -1e10f;
    int pw_ = 1, ph_ = 1;
    bool focused_ = false, keyboard_ = false, mouse_ = false;
    static int api(lua_State* state);
};
