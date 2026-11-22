#pragma once
#include "assets/assets.hpp"
#include "rendering/skybox.hpp"
#include <future>
#include <vector>
struct SkyboxDecoded {
    struct Image {
        int width = 0, height = 0;
        std::vector<unsigned char> bytes;
        std::vector<float> floats;
    };
    std::vector<Image> images;
    std::vector<std::string> paths;
    std::string stamp;
    Vec3 ambient{1, 1, 1};
};
class SkyboxRenderer {
    unsigned program_ = 0, vao_ = 0, texture_ = 0;
    std::string signature_, requestKey_;
    std::vector<std::string> dependencies_;
    std::future<SkyboxDecoded> pending_;
    bool ready_ = false;
    int inverseLocation_ = -1, tintLocation_ = -1, opacityLocation_ = -1;
    int textureLocation_ = -1, hdrLocation_ = -1;
    std::chrono::steady_clock::time_point checked_{};
    bool loaded_ = false;
    Vec3 ambient_{1, 1, 1};

  public:
    std::string error;
    bool ready() const {
        return ready_;
    }
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
