#pragma once
#include "assets/assets.hpp"
#include <functional>
#include <imgui.h>

// Editor widgets deal in ImGui texture IDs, not API-specific texture names.
// Providers must outlive installed callbacks and detach before destruction.
struct AssetThumbnailSource {
    std::function<void(const AssetManager&)> beginFrame;
    std::function<ImTextureID(const AssetManager&, const std::string&)> texture;
    std::function<void()> clear;
};
