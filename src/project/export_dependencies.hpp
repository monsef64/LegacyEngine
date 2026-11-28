#pragma once
#include "assets/assets.hpp"
#include "project/json_io.hpp"
#include <cctype>
#include <deque>
#include <map>
#include <set>

namespace GameExport {
// A dependency closure rooted in scenes, registered levels and spawnable prefabs,
// not a copy of the entire Assets tree.
// Literal paths in scripts are followed. Constructed non-prefab runtime paths
// must be declared in Settings/ExportAssets.json: {"include":["Folder/file", "Folder/"]}.
inline std::set<std::string> exportDependencies(const AssetManager& assets,
                                                const std::string& scene,
                                                const std::string& startLevel,
                                                bool allLevels = true,
                                                std::set<std::string>* packedMembers = nullptr) {
    std::map<std::string, std::filesystem::path> inventory;
    std::set<std::string> linkedDirectories;
    for (auto it = std::filesystem::recursive_directory_iterator(assets.root);
         it != std::filesystem::recursive_directory_iterator();
         ++it) {
        if (it->is_directory() &&
            (it->path().filename() == ".cache" || it->path().filename() == ".git")) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_symlink() && it->is_directory()) {
            linkedDirectories.insert(it->path().lexically_relative(assets.root).generic_string() +
                                     "/");
        }
        if (it->is_regular_file() || it->is_symlink()) {
            auto key = it->path().lexically_relative(assets.root).generic_string();
            inventory.emplace(key, it->path());
            if (inventory.size() > 100000) {
                throw std::runtime_error("Asset inventory exceeds 100000 files");
            }
        }
    }
    std::set<std::string> used;
    std::deque<std::string> pending;
    auto include = [&](const std::string& key) {
        for (const auto& prefix : linkedDirectories) {
            if (key.rfind(prefix, 0) == 0) {
                throw std::runtime_error(
                    "Referenced export asset uses a symbolic-link directory: " + key);
            }
        }
        auto found = inventory.find(key);
        if (found == inventory.end()) {
            return;
        }
        // resolve checks the canonical destination, including symbolic-link escapes.
        auto checked = assets.resolve(key);
        if (std::filesystem::is_symlink(found->second)) {
            throw std::runtime_error("Referenced export asset is a symbolic link: " + key);
        }
        if (!std::filesystem::is_regular_file(checked)) {
            throw std::runtime_error("Referenced export asset is not a file: " + key);
        }
        if (used.insert(key).second) {
            if (used.size() > 20000) {
                throw std::runtime_error("Export exceeds 20000 assets");
            }
            pending.push_back(key);
        }
    };
    auto reference = [&](std::string value, const std::string& owner) {
        if (value.empty() || value.size() > 4096 || value.find('\0') != value.npos ||
            value.find("://") != value.npos) {
            return;
        }
        // Packed members travel with their indivisible model container.
        const auto hash = value.find('#');
        if (hash != value.npos && packedMembers && materialPathValid(value)) {
            packedMembers->insert(value);
        }
        if (hash != value.npos) {
            value.resize(hash);
        }
        if (value.empty()) {
            return;
        }
        std::filesystem::path path(value);
        if (path.is_absolute() || value.find(':') != value.npos) {
            return;
        }
        include(path.lexically_normal().generic_string());
        if (value.rfind("Assets/", 0) == 0) {
            include(std::filesystem::path(value.substr(7)).lexically_normal().generic_string());
        }
        if (!owner.empty()) {
            include((std::filesystem::path(owner).parent_path() / path)
                        .lexically_normal()
                        .generic_string());
        }
    };
    auto textReferences = [&](const std::string& text, const std::string& owner) {
        // Entire JSON strings may be paths with spaces (rather than source code).
        reference(text, owner);
        std::string token;
        char quote = 0;
        bool escape = false;
        auto flush = [&] {
            if (!token.empty()) {
                reference(token, owner);
            }
            token.clear();
        };
        for (char c : text) {
            if (quote) {
                if (escape) {
                    token += c;
                    escape = false;
                } else if (c == '\\') {
                    escape = true;
                } else if (c == quote) {
                    flush();
                    quote = 0;
                } else if (token.size() < 4097) {
                    token += c;
                }
            } else if (c == '\'' || c == '"') {
                flush();
                quote = c;
            } else if (std::isspace(static_cast<unsigned char>(c)) || c == '(' || c == ')' ||
                       c == '<' || c == '>' || c == '=' || c == ',' || c == ';' || c == '[' ||
                       c == ']' || c == '{' || c == '}') {
                flush();
            } else if (token.size() < 4097) {
                token += c;
            }
        }
        flush();
    };
    auto documentReferences =
        [&](auto&& self, const Json& document, const std::string& owner, int depth) -> void {
        if (depth > 128) {
            throw std::runtime_error("Export dependency document is too deeply nested");
        }
        if (document.is_string()) {
            const auto& value = document.get_ref<const std::string&>();
            // Embedded images are hex, never asset references.
            if (value.size() <= 16 * 1024 * 1024) {
                textReferences(value, owner);
            }
        } else if (document.is_object()) {
            for (auto it = document.begin(); it != document.end(); ++it) {
                if (it.key() == "hex" || it.key() == "vertices" || it.key() == "weights" ||
                    it.key() == "heights" || it.key() == "indices") {
                    continue;
                }
                self(self, it.value(), owner, depth + 1);
            }
        } else if (document.is_array()) {
            for (const auto& child : document) {
                self(self, child, owner, depth + 1);
            }
        }
    };
    documentReferences(documentReferences, parseJson(scene), "", 0);
    if (allLevels) {
        include("Levels/levels.json");
        // Prefabs are runtime-addressable by name, just like registered levels.
        // A script can construct "Prefabs/" .. name .. ".prefab" without a
        // literal full path in the scene. Ship every project prefab as a root,
        // then let the normal queue follow its meshes/materials/scripts/etc.
        // Scene-only preload (allLevels=false) intentionally stays selective.
        for (const auto& entry : inventory) {
            if (AssetManager::extension(entry.first) == ".prefab") {
                include(entry.first);
            }
        }
    }
    const auto registry = assets.resolve("Levels/levels.json");
    if (allLevels && std::filesystem::exists(registry)) {
        auto doc = readJsonFile(registry, 128 * 1024);
        for (const auto& name : doc.at("levels")) {
            const auto level = name.get<std::string>();
            if (level.find_first_of("/\\:") != level.npos || level == "..") {
                throw std::runtime_error("Invalid registered level name");
            }
            const auto key = "Levels/" + level + ".level";
            if (!inventory.count(key)) {
                throw std::runtime_error("Registered export level is missing: " + level);
            }
            include(key);
        }
    }
    if (!startLevel.empty()) {
        include("Levels/" + startLevel + ".level");
    }
    const auto project =
        assets.root.filename() == "Assets" ? assets.root.parent_path() : assets.root;
    const auto explicitFile = project / "Settings" / "ExportAssets.json";
    if (std::filesystem::exists(explicitFile)) {
        auto extras = readJsonFile(explicitFile, 1024 * 1024).at("include");
        if (!extras.is_array() || extras.size() > 20000) {
            throw std::runtime_error("Invalid ExportAssets include list");
        }
        for (const auto& item : extras) {
            const auto key = item.get<std::string>();
            if (key.empty()) {
                throw std::runtime_error("Empty ExportAssets include path");
            }
            const auto resolved = assets.resolve(key);
            if (!std::filesystem::exists(resolved)) {
                throw std::runtime_error("ExportAssets include is missing: " + key);
            }
            auto relative =
                resolved.lexically_relative(std::filesystem::weakly_canonical(assets.root))
                    .generic_string();
            if (std::filesystem::is_directory(resolved)) {
                if (relative == ".") {
                    relative.clear();
                } else {
                    relative += '/';
                }
                for (const auto& entry : inventory) {
                    if (entry.first.rfind(relative, 0) == 0) {
                        include(entry.first);
                    }
                }
            } else {
                include(relative);
            }
        }
    }
    while (!pending.empty()) {
        const auto key = pending.front();
        pending.pop_front();
        const auto file = assets.resolve(key);
        const auto ext = AssetManager::extension(key);
        const bool json = ext == ".json" || ext == ".mat" || ext == ".model" || ext == ".mesh" ||
                          ext == ".lemesh" || ext == ".terrain" || ext == ".prefab" ||
                          ext == ".level" || ext == ".legacy" || ext == ".leanim" ||
                          ext == ".cubemap" || ext == ".controller" || ext == ".gltf";
        const bool text = ext == ".ls" || ext == ".lua" || ext == ".obj" || ext == ".mtl";
        if (json) {
            // Engine JSON assets may be compressed model/terrain containers.
            if (ext == ".level" || ext == ".prefab" || ext == ".legacy") {
                if (std::filesystem::file_size(file) > 16 * 1024 * 1024) {
                    throw std::runtime_error("Export level/prefab exceeds 16 MiB");
                }
                std::ifstream in(file, std::ios::binary);
                if (!in) {
                    throw std::runtime_error("Cannot read export dependency: " + key);
                }
                std::string bytes{std::istreambuf_iterator<char>(in), {}};
                const auto first = bytes.find_first_not_of(" \r\n\t");
                if (first != bytes.npos && bytes[first] == '{') {
                    documentReferences(documentReferences, parseJson(bytes), key, 0);
                } else {
                    textReferences(bytes, key); // Backward-compatible legacy scene format.
                }
            } else {
                documentReferences(
                    documentReferences, readJsonFile(file, 256 * 1024 * 1024), key, 0);
            }
        } else if (text) {
            if (std::filesystem::file_size(file) > 16 * 1024 * 1024) {
                throw std::runtime_error("Export text asset exceeds 16 MiB");
            }
            std::ifstream in(file, std::ios::binary);
            if (!in) {
                throw std::runtime_error("Cannot read export dependency: " + key);
            }
            const std::string bytes{std::istreambuf_iterator<char>(in), {}};
            textReferences(bytes, key);
        }
    }
    return used;
}
} // namespace GameExport
