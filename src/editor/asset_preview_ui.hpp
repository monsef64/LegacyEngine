#pragma once
#include "assets/assets.hpp"
#include <algorithm>
#include <functional>
#include <imgui.h>
#include <vector>

struct AssetPreviewData {
    unsigned texture = 0;
    int width = 384, height = 256;
    bool flip = false;
    std::string text, error;
    std::vector<float> waveform;
    float seconds = 0;
};
// Import completion queues work only; GPU generation runs on focused editor frames.
inline std::function<void(const AssetManager&)> assetPreviewImported;
// Thumbnail requests return only an image descriptor, never transient detail-slot data.
inline std::function<AssetPreviewData(const AssetManager&, const std::string&)> assetThumbnail;
// The running editor installs a renderer; header-only UI remains usable without GL.
inline std::function<const AssetPreviewData&(const AssetManager&, const std::string&, int)>
    assetPreview;
inline void
drawAssetPreview(const AssetManager& assets, const std::string& path, float width, int slot = 0) {
    if (!assetPreview) {
        return;
    }
    const auto& preview = assetPreview(assets, path, slot);
    width = std::max(40.f, width);
    if (preview.texture) {
        const float ratio = float(preview.height) / std::max(1, preview.width);
        const float imageWidth = width * std::min(1.f, 1.5f / std::max(.001f, ratio));
        ImGui::Image(static_cast<ImTextureID>(preview.texture),
                     ImVec2(imageWidth, imageWidth * ratio),
                     preview.flip ? ImVec2(0, 1) : ImVec2(0, 0),
                     preview.flip ? ImVec2(1, 0) : ImVec2(1, 1));
    }
    if (!preview.waveform.empty()) {
        ImGui::Text("Duration: %.2f seconds", preview.seconds);
        ImGui::PlotHistogram("##Waveform",
                             preview.waveform.data(),
                             int(preview.waveform.size()),
                             0,
                             "Waveform",
                             0,
                             1,
                             ImVec2(width, 140));
    }
    if (!preview.text.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        ImGui::TextUnformatted(preview.text.c_str());
        ImGui::PopTextWrapPos();
    }
    if (!preview.error.empty()) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + width);
        const bool filesystem = preview.error.find("filesystem error") != std::string::npos ||
                                preview.error.find("filesystem_error") != std::string::npos;
        ImGui::TextDisabled("%s",
                            filesystem ? "Preview unavailable: asset is missing or inaccessible."
                                       : preview.error.c_str());
        ImGui::PopTextWrapPos();
    }
}
