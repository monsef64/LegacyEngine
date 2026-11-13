#pragma once
#include "assets/assets.hpp"
#include "lighting/shadow_settings.hpp"
#include "project/json_io.hpp"
#include <SDL3/SDL.h>
struct RenderingSettings {
    bool visible = false, vsync = true;
    ShadowSettings shadows;
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
        shadows = {};
        error.clear();
        try {
            auto path = file(assets);
            if (std::filesystem::exists(path)) {
                auto json = readJsonFile(path, 4096);
                if ((json.at("version") != 1 && json.at("version") != 2 &&
                     json.at("version") != 3) ||
                    !json.at("vsync").is_boolean()) {
                    throw std::runtime_error("Invalid rendering settings");
                }
                vsync = json.at("vsync").get<bool>();
                if (json.at("version") >= 2) {
                    if (!json.at("shadowMode").is_number_integer() ||
                        !json.at("shadowResolution").is_number_integer()) {
                        throw std::runtime_error("Invalid shadow settings");
                    }
                    ShadowSettings loaded{json.at("shadowMode").get<int>(),
                                          json.at("shadowResolution").get<int>()};
                    if (!loaded.valid()) {
                        throw std::runtime_error("Invalid shadow mode or resolution");
                    }
                    if (json.at("version") == 3) {
                        if (!json.at("softShadows").is_boolean()) {
                            throw std::runtime_error("Invalid shadow filtering");
                        }
                        loaded.soft = json.at("softShadows").get<bool>();
                    }
                    shadows = loaded;
                }
            }
        } catch (const std::exception& e) {
            error = e.what();
            return;
        }
        apply();
    }
    void save(const AssetManager& assets) const {
        writeJsonFile(file(assets),
                      Json{{"version", 3},
                           {"softShadows", shadows.soft},
                           {"vsync", vsync},
                           {"shadowMode", shadows.mode},
                           {"shadowResolution", shadows.resolution}});
    }
};
