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
    inline static std::array<std::uint32_t, 32> collisions = [] {
        std::array<std::uint32_t, 32> a;
        a.fill(~0u);
        return a;
    }();
    static bool collide(int a, int b) {
        return a >= 0 && a < 32 && b >= 0 && b < 32 && (collisions[a] & (1u << b)) != 0;
    }
    static void setCollision(int a, int b, bool enabled) {
        if (enabled) {
            collisions[a] |= 1u << b;
            collisions[b] |= 1u << a;
        } else {
            collisions[a] &= ~(1u << b);
            collisions[b] &= ~(1u << a);
        }
    }
    static void resetCollision(int layer) {
        for (int i = 0; i < 32; ++i) {
            setCollision(layer, i, true);
        }
    }
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
        collisions.fill(~0u);
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
                auto matrix = collisions;
                if (json.contains("collisions")) {
                    matrix = json.at("collisions").get<std::array<std::uint32_t, 32>>();
                    for (int i = 0; i < 32; ++i) {
                        for (int j = 0; j < 32; ++j) {
                            if (bool(matrix[i] & (1u << j)) != bool(matrix[j] & (1u << i))) {
                                throw std::runtime_error("Asymmetric collision matrix");
                            }
                        }
                    }
                }
                names = loaded;
                collisions = matrix;
            }
        } catch (const std::exception& e) {
            error = e.what();
        }
    }
    void save(const AssetManager& assets) {
        try {
            writeJsonFile(file(assets),
                          Json{{"version", 1}, {"names", names}, {"collisions", collisions}});
            error.clear();
        } catch (const std::exception& e) {
            error = e.what();
        }
    }
};
