#pragma once
#include "assets/assets.hpp"
#include "project/json_io.hpp"
#include <SDL3/SDL.h>
struct RenderingSettings {
    bool visible = false, vsync = true;
    std::string error;
    std::filesystem::path root;
    static std::filesystem::path file(const AssetManager& assets) {
        return (assets.root.filename() == "Assets" ? assets.root.parent_path() : assets.root) /
               "Settings" / "Rendering.json";
    }
    void apply() {
        if (!SDL_GL_SetSwapInterval(vsync ? 1 : 0)) {
            error = std::string("Cannot change VSync: ") + SDL_GetError();
            SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "%s", error.c_str());
        } else {
            error.clear();
            SDL_Log("VSync %s", vsync ? "enabled" : "disabled");
        }
    }
    void load(const AssetManager& assets) {
        if (root == assets.root) {
            return;
        }
        root = assets.root;
        vsync = true;
        error.clear();
        try {
            auto path = file(assets);
            if (std::filesystem::exists(path)) {
                auto json = readJsonFile(path, 4096);
                if (json.at("version") != 1 || !json.at("vsync").is_boolean()) {
                    throw std::runtime_error("Invalid rendering settings");
                }
                vsync = json.at("vsync").get<bool>();
            }
        } catch (const std::exception& e) {
            error = e.what();
            return;
        }
        apply();
    }
    void save(const AssetManager& assets) const {
        writeJsonFile(file(assets), Json{{"version", 1}, {"vsync", vsync}});
    }
};
