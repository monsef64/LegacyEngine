#pragma once
#include "assets/assets.hpp"
#include <imgui.h>
#include <map>
#include <optional>

struct ScriptTree {
    std::map<std::string, ScriptTree> folders;
    std::vector<std::string> files;
    void add(const std::filesystem::path& path) {
        auto it = path.begin();
        if (it == path.end()) {
            return;
        }
        const auto first = it->string();
        ++it;
        if (it == path.end()) {
            files.push_back(first);
            return;
        }
        std::filesystem::path rest;
        for (; it != path.end(); ++it) {
            rest /= *it;
        }
        folders[first].add(rest);
    }
    std::optional<std::string> draw(const std::string& selected,
                                    const std::string& prefix = "",
                                    std::optional<std::string>* createScriptIn = nullptr) const {
        std::optional<std::string> result;
        for (const auto& [name, folder] : folders) {
            const bool open = ImGui::TreeNode(name.c_str());
            if (createScriptIn && ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("New LegacyScript (.ls)")) {
                    *createScriptIn = prefix + name;
                }
                ImGui::EndPopup();
            }
            if (open) {
                if (auto chosen = folder.draw(selected, prefix + name + "/", createScriptIn)) {
                    result = chosen;
                }
                ImGui::TreePop();
            }
        }
        for (const auto& name : files) {
            const auto path = prefix + name;
            if (ImGui::Selectable(name.c_str(), selected == path)) {
                result = path;
            }
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("ASSET_PATH", path.c_str(), path.size() + 1);
                ImGui::TextUnformatted(name.c_str());
                ImGui::EndDragDropSource();
            }
        }
        return result;
    }
};
inline ScriptTree buildScriptTree(const AssetManager& assets) {
    ScriptTree tree;
    for (const auto& file : assets.files()) {
        if (AssetManager::script(file)) {
            tree.add(file);
        }
    }
    return tree;
}
