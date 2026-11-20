#pragma once
#include "assets/assets.hpp"
#include <imgui.h>
#include <map>
#include <optional>

struct ScriptTreeAction {
    enum Kind { None, Rename, Duplicate, Delete, Move } kind = None;
    std::string path, folder;
};
inline void scriptFolderDrop(const std::string& folder, ScriptTreeAction* action) {
    if (action && ImGui::BeginDragDropTarget()) {
        if (const auto* payload = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
            if (payload->Data && payload->DataSize > 1 &&
                static_cast<const char*>(payload->Data)[payload->DataSize - 1] == '\0') {
                const std::string path(static_cast<const char*>(payload->Data),
                                       payload->DataSize - 1);
                if (AssetManager::script(path)) {
                    *action = {ScriptTreeAction::Move, path, folder};
                }
            }
        }
        ImGui::EndDragDropTarget();
    }
}
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
                                    std::optional<std::string>* createScriptIn = nullptr,
                                    ScriptTreeAction* action = nullptr) const {
        std::optional<std::string> result;
        for (const auto& [name, folder] : folders) {
            const bool open = ImGui::TreeNode(name.c_str());
            scriptFolderDrop(prefix + name, action);
            if (createScriptIn && ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("New LegacyScript (.lua)")) {
                    *createScriptIn = prefix + name;
                }
                ImGui::EndPopup();
            }
            if (open) {
                if (auto chosen =
                        folder.draw(selected, prefix + name + "/", createScriptIn, action)) {
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
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "%s\nF2: Rename | Right-click: Rename, Duplicate, Delete\nDrag onto a folder or Assets to move",
                    path.c_str());
            }
            if (action && ImGui::BeginPopupContextItem()) {
                if (ImGui::MenuItem("Rename", "F2")) {
                    *action = {ScriptTreeAction::Rename, path, {}};
                }
                if (ImGui::MenuItem("Duplicate")) {
                    *action = {ScriptTreeAction::Duplicate, path, {}};
                }
                if (ImGui::MenuItem("Delete")) {
                    *action = {ScriptTreeAction::Delete, path, {}};
                }
                ImGui::EndPopup();
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
    // Include empty folders, so they can be drag/drop destinations too.
    if (std::filesystem::is_directory(assets.root)) {
        unsigned count = 0;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(assets.root)) {
            if (++count > 10000) {
                break;
            }
            if (entry.is_directory() && !entry.is_symlink()) {
                auto relative = entry.path().lexically_relative(assets.root);
                assets.resolve(relative.generic_string());
                auto* folder = &tree;
                for (const auto& part : relative) {
                    folder = &folder->folders[part.string()];
                }
            }
        }
    }
    return tree;
}
