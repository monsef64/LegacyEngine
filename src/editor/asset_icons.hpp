#pragma once
#include "assets/texture_preview.hpp"
#include "models/model_asset.hpp"
#include <imgui.h>

// Font Awesome Free Solid glyphs, merged into the embedded editor font.
inline const char* assetIcon(const std::string& path, bool folder = false) {
    if (folder) {
        return "\xef\x81\xbc"; // folder-open
    }
    auto ext = AssetManager::extension(path);
    if (ext == ".ls" || ext == ".lua") {
        return "\xef\x87\x89"; // file-code
    }
    if (ext == ".terrain") {
        return "\xef\x9b\xbc";
    }
    if (ext == ".prefab") {
        return "\xef\x86\xb2";
    }
    if (ext == ".cubemap") {
        return "\xef\x9e\xa2";
    }
    if (ext == ".mat") {
        return "\xef\x81\x82"; // material sphere
    }
    if (textureAsset(path)) {
        return "\xef\x87\x85"; // file-image
    }
    if (ext == ".mesh" || ext == ".model" || ext == ".lemesh" || ext == ".obj" || ext == ".gltf" ||
        ext == ".glb") {
        return "\xef\x86\xb2"; // cube
    }
    if (ext == ".wav" || ext == ".mp3" || ext == ".ogg") {
        return "\xef\x80\xa8"; // volume-high
    }
    if (ext == ".txt" || ext == ".json" || ext == ".md") {
        return "\xef\x85\x9c"; // file-lines
    }
    return "\xef\x85\x9b"; // file
}
inline void fileBadge(const std::string& path,
                      ImVec2 pos,
                      bool folder = false,
                      unsigned thumbnail = 0,
                      float size = 32,
                      bool flip = false) {
    auto* draw = ImGui::GetWindowDrawList();
    if (thumbnail) {
        draw->AddImage(static_cast<ImTextureID>(thumbnail),
                       ImVec2(pos.x, pos.y),
                       ImVec2(pos.x + size, pos.y + size),
                       (flip || modelAsset(path)) ? ImVec2(0, 1) : ImVec2(0, 0),
                       (flip || modelAsset(path)) ? ImVec2(1, 0) : ImVec2(1, 1));
        return;
    }
    draw->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), IM_COL32(35, 45, 60, 255), 4);
    const char* icon = assetIcon(path, folder);
    const float fontSize = ImGui::GetFontSize() * size / 32;
    const float width = ImGui::CalcTextSize(icon).x * size / 32;
    draw->AddText(ImGui::GetFont(),
                  fontSize,
                  ImVec2(pos.x + (size - width) * .5f, pos.y + (size - fontSize) * .5f),
                  IM_COL32(160, 185, 215, 255),
                  icon);
}
inline void fileIcon(const std::string& path, bool folder = false, unsigned thumbnail = 0) {
    auto pos = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(36, 32));
    fileBadge(path, pos, folder, thumbnail);
    ImGui::SameLine();
}
