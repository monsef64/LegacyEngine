#pragma once
#include "editor/script_explorer.hpp"
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
    if (ImGui::Button(value.empty() ? "None - select asset" : value.c_str(), ImVec2(-1, 0))) {
        ImGui::OpenPopup("Assets");
    }
    if (ImGui::BeginPopup("Assets")) {
        if (ImGui::Selectable("None", value.empty())) {
            value.clear();
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::Separator();
        ScriptTree tree;
        if (assets) {
            for (const auto& path : assets->files()) {
                if (supported(path)) {
                    tree.add(path);
                }
            }
        }
        if (auto selected = tree.draw(value)) {
            value = *selected;
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}
