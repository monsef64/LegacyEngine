#pragma once
#include "assets/texture_preview.hpp"
#include "models/model_asset.hpp"
#include "ui/mygui_ui.hpp"

// Font Awesome Free Solid glyphs, merged into the embedded editor font.
inline const char* assetIcon(const std::string& path, bool folder = false) {
    if (folder) return "\xef\x81\xbc"; // folder-open
    const auto ext = AssetManager::extension(path);
    if (ext == ".ls" || ext == ".lua") return "\xef\x87\x89"; // file-code
    if (ext == ".terrain") return "\xef\x9b\xbc";
    if (ext == ".prefab" || ext == ".lemesh" || ext == ".obj" || ext == ".gltf" || ext == ".glb")
        return "\xef\x86\xb2"; // cube
    if (ext == ".cubemap") return "\xef\x9e\xa2";
    if (ext == ".mat") return "\xef\x81\x82"; // material sphere
    if (textureAsset(path)) return "\xef\x87\x85"; // file-image
    if (ext == ".wav" || ext == ".mp3" || ext == ".ogg") return "\xef\x80\xa8"; // volume-high
    if (ext == ".txt" || ext == ".json" || ext == ".md") return "\xef\x85\x9c"; // file-lines
    return "\xef\x85\x9b"; // file
}

inline void fileBadge(const std::string& path,
                      ui::Vec2 pos,
                      bool folder = false,
                      unsigned thumbnail = 0,
                      float size = 32) {
    auto* draw = ui::windowDrawList();
    if (thumbnail) {
        draw->addImage(static_cast<ui::TextureID>(thumbnail),
                       pos,
                       ui::Vec2(pos.x + size, pos.y + size),
                       modelAsset(path) ? ui::Vec2(0, 1) : ui::Vec2(0, 0),
                       modelAsset(path) ? ui::Vec2(1, 0) : ui::Vec2(1, 1));
        return;
    }
    draw->addRectFilled(pos,
                        ui::Vec2(pos.x + size, pos.y + size),
                        UI_COL32(35, 45, 60, 255),
                        4);
    const char* icon = assetIcon(path, folder);
    const float fontSize = ui::fontSize() * size / 32;
    const float width = ui::calcTextSize(icon).x * size / 32;
    draw->addText(ui::defaultFont(),
                  fontSize,
                  ui::Vec2(pos.x + (size - width) * .5f, pos.y + (size - fontSize) * .5f),
                  UI_COL32(160, 185, 215, 255),
                  icon);
}

inline void fileIcon(const std::string& path, bool folder = false, unsigned thumbnail = 0) {
    const auto pos = ui::cursorScreenPos();
    ui::dummy(ui::Vec2(36, 32));
    fileBadge(path, pos, folder, thumbnail);
    ui::sameLine();
}
