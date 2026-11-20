#pragma once
#include <SDL3/SDL.h>
#include <filesystem>
#include <memory>
#include <string>

// One runtime overlay, owned by the editor's GL lifetime. No editor UI uses RmlUi.
class GameUI {
  public:
    GameUI();
    ~GameUI();
    void sync(bool active, const std::filesystem::path& assetRoot, SDL_Window* window);
    // Coordinates are SDL window logical pixels, dimensions are framebuffer pixels.
    void viewport(float x, float y, float w, float h, int pixelsW, int pixelsH);
    bool event(SDL_Event event, SDL_Window* window, bool focused, bool mouseLocked);
    void render(int width, int height, bool update);
    static GameUI* current();
    bool setText(const std::string& id, const std::string& text);
    bool takeClick(const std::string& id);
    bool loaded() const;
    bool capturesKeyboard() const;
    bool capturesMouse() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
