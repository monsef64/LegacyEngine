#pragma once
#include "assets/assets.hpp"
#include "rendering/skybox.hpp"
class SkyboxRenderer {
    unsigned program_ = 0, vao_ = 0, texture_ = 0;
    std::string signature_;
    std::chrono::steady_clock::time_point checked_{};
    bool loaded_ = false;

  public:
    std::string error;
    bool init();
    bool prepare(const Skybox& sky, const AssetManager& assets);
    void clear();
    void shutdown();
    void draw(const Skybox& sky, const AssetManager& assets, const Mat4& vp, float opacity = 1);
};
