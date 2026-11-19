#pragma once
#include "assets/assets.hpp"
#include "rendering/skybox.hpp"
class SkyboxRenderer {
    unsigned program_ = 0, vao_ = 0, texture_ = 0;
    std::string signature_;
    std::chrono::steady_clock::time_point checked_{};
    bool loaded_ = false;
    Vec3 ambient_{1, 1, 1};

  public:
    std::string error;
    Vec3 ambient() const {
        return ambient_;
    }
    unsigned texture() const {
        return loaded_ ? texture_ : 0;
    }
    bool init();
    bool prepare(const Skybox& sky, const AssetManager& assets);
    void clear();
    void shutdown();
    void draw(const Skybox& sky, const AssetManager& assets, const Mat4& vp, float opacity = 1);
};
