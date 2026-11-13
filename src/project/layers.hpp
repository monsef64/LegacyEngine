#pragma once
#include "assets/assets.hpp"
#include "core/components.hpp"
#include "project/json_io.hpp"
#include <array>
struct LayerSettings {
    inline static std::array<std::string, 32> names = [] {
        std::array<std::string, 32> result;
        result[0] = "Default";
        return result;
    }();
    std::filesystem::path root;
    bool visible = false;
    std::string error;
    static int effective(const World& world, Entity entity) {
        const auto* layer = world.tryGet<ObjectLayer>(entity);
        return layer && layer->value >= 0 && layer->value < 32 && !names[layer->value].empty()
                   ? layer->value
                   : 0;
    }
    static int find(const std::string& name) {
        if (name.empty()) {
            return -1;
        }
        for (int i = 0; i < 32; ++i) {
            if (names[i] == name) {
                return i;
            }
        }
        return -1;
    }
    static std::filesystem::path file(const AssetManager& assets) {
        return (assets.root.filename() == "Assets" ? assets.root.parent_path() : assets.root) /
               "Settings/Layers.json";
    }
    void load(const AssetManager& assets) {
        if (root == assets.root) {
            return;
        }
        root = assets.root;
        names = {};
        names[0] = "Default";
        error.clear();
        try {
            if (std::filesystem::exists(file(assets))) {
                auto json = readJsonFile(file(assets), 16384);
                if (json.at("version") != 1 || json.at("names").size() != 32) {
                    throw std::runtime_error("Invalid layer settings");
                }
                auto loaded = json.at("names").get<std::array<std::string, 32>>();
                if (loaded[0] != "Default") {
                    throw std::runtime_error("Default layer cannot be removed");
                }
                for (int i = 0; i < 32; ++i) {
                    if (loaded[i].size() > 64) {
                        throw std::runtime_error("Layer name too long");
                    }
                    for (int j = 0; j < i; ++j) {
                        if (!loaded[i].empty() && loaded[i] == loaded[j]) {
                            throw std::runtime_error("Duplicate layer");
                        }
                    }
                }
                names = loaded;
            }
        } catch (const std::exception& e) {
            error = e.what();
        }
    }
    void save(const AssetManager& assets) {
        try {
            writeJsonFile(file(assets), Json{{"version", 1}, {"names", names}});
            error.clear();
        } catch (const std::exception& e) {
            error = e.what();
        }
    }
};
