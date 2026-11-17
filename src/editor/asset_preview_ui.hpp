#pragma once
#include "assets/assets.hpp"
#include <algorithm>
#include <functional>
#include <vector>

struct AssetPreviewData {
    unsigned texture = 0;
    int width = 384, height = 256;
    bool flip = false;
    std::string text, error;
    std::vector<float> waveform;
    float seconds = 0;
};
// Thumbnail requests return only an image descriptor, never transient detail-slot data.
inline std::function<AssetPreviewData(const AssetManager&, const std::string&)> assetThumbnail;
// The running editor installs a renderer; header-only UI remains usable without GL.
inline std::function<const AssetPreviewData&(const AssetManager&, const std::string&, int)>
    assetPreview;
