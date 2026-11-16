#pragma once
#include "../../third_party/stb/stb_image.h"
#include "assets/assets.hpp"
#include <memory>

namespace Lighting {
// CPU equivalent of the 512px linear-filtered runtime cookie atlas, for offline GI.
struct CookieImage {
    static constexpr int size = cookieResolution;
    std::vector<unsigned char> pixels;
    Vec3 sample(float u, float v) const {
        if (u < 0 || v < 0 || u > 1 || v > 1 || pixels.empty()) {
            return {};
        }
        const float x = std::clamp(u * size - .5f, 0.f, float(size - 1)),
                    y = std::clamp(v * size - .5f, 0.f, float(size - 1));
        const int ix = int(x), iy = int(y), nx = std::min(ix + 1, size - 1),
                  ny = std::min(iy + 1, size - 1);
        const float fx = x - ix, fy = y - iy;
        float rgba[4]{};
        for (int c = 0; c < 4; ++c) {
            auto at = [&](int a, int b) { return pixels[(b * size + a) * 4 + c] / 255.f; };
            rgba[c] = (at(ix, iy) * (1 - fx) + at(nx, iy) * fx) * (1 - fy) +
                      (at(ix, ny) * (1 - fx) + at(nx, ny) * fx) * fy;
        }
        return Vec3{rgba[0], rgba[1], rgba[2]} * rgba[3];
    }
    static std::shared_ptr<CookieImage> load(const AssetManager& assets, const std::string& path) {
        const auto file = assets.resolve(path);
        const auto bytes = std::filesystem::file_size(file);
        if (bytes == 0 || bytes > 64 * 1024 * 1024) {
            throw std::runtime_error("Cookie image exceeds 64 MiB");
        }
        std::vector<unsigned char> encoded(bytes);
        std::ifstream in(file, std::ios::binary);
        if (!in.read(reinterpret_cast<char*>(encoded.data()), std::streamsize(bytes))) {
            throw std::runtime_error("Cannot read cookie image");
        }
        int w = 0, h = 0, channels = 0;
        if (!stbi_info_from_memory(encoded.data(), int(bytes), &w, &h, &channels) || w < 1 ||
            h < 1 || w > 4096 || h > 4096) {
            throw std::runtime_error("Cookie image requires at most 4096 x 4096 pixels");
        }
        std::unique_ptr<unsigned char, decltype(&stbi_image_free)> decoded(
            stbi_load_from_memory(encoded.data(), int(bytes), &w, &h, &channels, 4),
            stbi_image_free);
        if (!decoded) {
            throw std::runtime_error("Cannot decode cookie image");
        }
        auto image = std::make_shared<CookieImage>();
        image->pixels.resize(size * size * 4);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const float sx = std::clamp((x + .5f) * w / size - .5f, 0.f, float(w - 1));
                const float sy = std::clamp((size - y - .5f) * h / size - .5f, 0.f, float(h - 1));
                const int ix = int(sx), iy = int(sy), nx = std::min(ix + 1, w - 1),
                          ny = std::min(iy + 1, h - 1);
                const float fx = sx - ix, fy = sy - iy;
                for (int c = 0; c < 4; ++c) {
                    auto at = [&](int a, int b) { return decoded.get()[(b * w + a) * 4 + c]; };
                    const float value = (at(ix, iy) * (1 - fx) + at(nx, iy) * fx) * (1 - fy) +
                                        (at(ix, ny) * (1 - fx) + at(nx, ny) * fx) * fy;
                    image->pixels[(y * size + x) * 4 + c] =
                        static_cast<unsigned char>(std::clamp(value, 0.f, 255.f) + .5f);
                }
            }
        }
        return image;
    }
};
} // namespace Lighting
