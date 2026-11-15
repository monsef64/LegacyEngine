#pragma once
#include "editor/script_explorer.hpp"
#include "editor/search_box.hpp"
#include <functional>
// Filter first so empty/unsupported folders never appear in the picker.
inline bool assetPicker(const char* label,
                        std::string& value,
                        const AssetManager* assets,
                        const std::function<bool(const std::string&)>& supported) {
    ImGui::PushID(label);
    bool changed = false;
    if (label[0] != '#') {
        ImGui::TextUnformatted(label);
    }
    static ImGuiID active = 0;
    static ImGuiTextFilter search;
    const auto id = ImGui::GetID("Asset selection");
    if (ImGui::Button(value.empty() ? "None - select asset" : value.c_str(), ImVec2(-1, 0))) {
        active = id;
        search.Clear();
    }
    if (active == id) {
        ImGui::SetNextWindowSize(ImVec2(560, 420), ImGuiCond_FirstUseEver);
        bool open = true;
        if (ImGui::Begin("Select asset###MatchingAssetSelector", &open)) {
            searchBox("Search", search);
            ImGui::BeginChild("Matching assets");
            if (ImGui::Selectable("None", value.empty())) {
                value.clear();
                changed = true;
                open = false;
            }
            if (assets) {
                for (const auto& path : assets->files()) {
                    if (supported(path) && search.PassFilter(path.c_str()) &&
                        ImGui::Selectable(path.c_str(), value == path)) {
                        value = path;
                        changed = true;
                        open = false;
                    }
                }
            }
            ImGui::EndChild();
        }
        ImGui::End();
        if (!open) {
            active = 0;
        }
    }
    ImGui::PopID();
    return changed;
}
