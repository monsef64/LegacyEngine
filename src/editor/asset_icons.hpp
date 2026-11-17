#pragma once
#include "assets/texture_preview.hpp"
#include "models/model_asset.hpp"

// Font Awesome Free Solid glyphs, merged into the embedded editor font. The Qt
// asset panel renders these with QIcon/QPainter, so only the lookup stays.
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
