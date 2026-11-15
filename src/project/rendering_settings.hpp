#pragma once
#include "assets/assets.hpp"
#include "lighting/shadow_settings.hpp"
#include "project/json_io.hpp"
#include <functional>
struct RenderingSettings {
    // The editor installs its active project settings for script access.
    static inline RenderingSettings* active = nullptr;
    ~RenderingSettings() {
        if (active == this) {
            active = nullptr;
        }
    }
    bool visible = false, vsync = true;
    // 0=off, 1=native AA, 2=quality, 3=balanced, 4=performance, 5=ultra performance.
    int fsr3GameQuality = 0;
    ShadowSettings shadows;
    std::string error;
    std::filesystem::path root;
    // The editor installs its presentation backend; settings and project
    // serialization do not depend on the presentation context.
    std::function<void(bool)> applyVsync;
    std::function<std::string()> presentationStatus;
    static std::filesystem::path file(const AssetManager& assets) {
        return (assets.root.filename() == "Assets" ? assets.root.parent_path() : assets.root) /
               "Settings" / "Rendering.json";
    }
    void apply() {
        try {
            if (applyVsync) {
                applyVsync(vsync);
            }
            error.clear();
        } catch (const std::exception& e) {
            error = std::string("Cannot change VSync: ") + e.what();
        }
    }
    void load(const AssetManager& assets) {
        if (root == assets.root) {
            return;
        }
        root = assets.root;
        vsync = true;
        fsr3GameQuality = 0;
        shadows = {};
        error.clear();
        try {
            auto path = file(assets);
            if (std::filesystem::exists(path)) {
                auto json = readJsonFile(path, 4096);
                if ((json.at("version") != 1 && json.at("version") != 2 &&
                     json.at("version") != 3 && json.at("version") != 4) ||
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
                    if (json.at("version") >= 3) {
                        if (!json.at("softShadows").is_boolean()) {
                            throw std::runtime_error("Invalid shadow filtering");
                        }
                        loaded.soft = json.at("softShadows").get<bool>();
                    }
                    shadows = loaded;
                }
                if (json.at("version") >= 4) {
                    if (!json.at("fsr3GameQuality").is_number_integer()) {
                        throw std::runtime_error("Invalid Game FSR setting");
                    }
                    const auto quality = json.at("fsr3GameQuality").get<int>();
                    if (quality < 0 || quality > 5) {
                        throw std::runtime_error("Invalid Game FSR quality");
                    }
                    fsr3GameQuality = quality;
                }
            }
        } catch (const std::exception& e) {
            error = e.what();
            return;
        }
        apply();
    }
    bool setGameFsrQuality(int quality) {
        if (quality < 0 || quality > 5) {
            error = "Game FSR quality must be between 0 and 5";
            return false;
        }
        const int previous = fsr3GameQuality;
        fsr3GameQuality = quality;
        try {
            if (!root.empty()) {
                AssetManager assets;
                assets.root = root;
                save(assets);
            }
            error.clear();
            return true;
        } catch (const std::exception& e) {
            fsr3GameQuality = previous;
            error = e.what();
            return false;
        }
    }
    void save(const AssetManager& assets) const {
        writeJsonFile(file(assets),
                      Json{{"version", 4},
                           {"fsr3GameQuality", fsr3GameQuality},
                           {"softShadows", shadows.soft},
                           {"vsync", vsync},
                           {"shadowMode", shadows.mode},
                           {"shadowResolution", shadows.resolution}});
    }
};
