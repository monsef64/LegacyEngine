#pragma once
#include <SDL3/SDL.h>

inline bool cameraModifiersHeld(SDL_Keymod modifiers) {
    return (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_SHIFT | SDL_KMOD_ALT | SDL_KMOD_GUI)) != 0;
}
