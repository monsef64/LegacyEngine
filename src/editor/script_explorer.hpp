#pragma once
#include "assets/assets.hpp"
#include "ui/mygui_ui.hpp"
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
            const bool open = ui::treeNode(name.c_str());
            if (createScriptIn && ui::beginPopupContextItem()) {
                if (ui::menuItem("New LegacyScript (.ls)")) {
                    *createScriptIn = prefix + name;
                }
                ui::endPopup();
            }
            if (open) {
                if (auto chosen = folder.draw(selected, prefix + name + "/", createScriptIn)) {
                    result = chosen;
                }
                ui::treePop();
            }
        }
        for (const auto& name : files) {
            const auto path = prefix + name;
            if (ui::selectable(name.c_str(), selected == path)) {
                result = path;
            }
            if (ui::beginDragDropSource()) {
                ui::setDragDropPayload("ASSET_PATH", path.c_str(), path.size() + 1);
                ui::textUnformatted(name.c_str());
                ui::endDragDropSource();
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
