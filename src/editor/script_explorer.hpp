#pragma once
#include "assets/assets.hpp"
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
