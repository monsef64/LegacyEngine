#pragma once
#include "core/ecs.hpp"
#include <SDL3/SDL.h>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

// Runtime documents belong to GUI components; no conventional auto-load path.
class GameUI {
  public:
    GameUI();
    ~GameUI();
    void
    sync(World& world, bool active, const std::filesystem::path& assetRoot, SDL_Window* window);
    void viewport(float x, float y, float w, float h, int pixelsW, int pixelsH);
    bool event(SDL_Event event, SDL_Window* window, bool focused, bool mouseLocked);
    void render(int width, int height, bool update);
    static GameUI* current();
    static std::string clickEvent(Entity owner, const std::string& id);
    std::function<void(Entity, const std::string&)> clicked;
    bool setText(Entity owner, const std::string& id, const std::string& text);
    bool takeClick(Entity owner, const std::string& id);
    bool loaded(Entity owner) const;
    bool capturesKeyboard() const;
    bool capturesMouse() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
