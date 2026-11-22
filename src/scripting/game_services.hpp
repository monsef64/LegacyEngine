#pragma once
#include <SDL3/SDL.h>
#include <filesystem>
#include <string>
struct lua_State;
namespace GameServices {
void install(lua_State* L);
void configure(SDL_Window* window,
               bool editor,
               const std::string& company,
               const std::string& game,
               const std::filesystem::path& data,
               bool embeddedGame = false);
void update(SDL_Window* window, bool active);
void requestQuit();
bool quitReady();
bool pendingQuit();
void cancelQuit();
void finishFrame(unsigned framebuffer, int width, int height);
void paceFrame(Uint64 started);
} // namespace GameServices
