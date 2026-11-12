#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>

inline bool cameraModifiersHeld(SDL_Keymod modifiers) {
    return (modifiers & (SDL_KMOD_CTRL | SDL_KMOD_ALT | SDL_KMOD_GUI)) != 0;
}

inline float sceneCameraSpeed(SDL_Keymod modifiers,float boost) {
    return modifiers&SDL_KMOD_SHIFT?5.0f*boost:5.0f;
}
inline float adjustSceneCameraBoost(float boost,float wheel) {
    return std::clamp(boost*std::pow(1.2f,wheel),1.0f,100.0f);
}
