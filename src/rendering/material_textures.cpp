#include "rendering/material_textures.hpp"
#include "materials/material.hpp"
#include <SDL3/SDL.h>
#include <glad/gl.h>
#include <memory>
#include <stb_image.h>
void MaterialTextureCache::clear() {
    if (SDL_GL_GetCurrentContext()) {
        for (auto& p : entries_) {
            if (p.second.texture) {
                glDeleteTextures(1, &p.second.texture);
            }
        }
    }
    entries_.clear();
    failures_.clear();
    bytes_ = 0;
}
unsigned
MaterialTextureCache::get(const AssetManager& assets, const std::string& path, std::string& error) {
    if (path.empty()) {
        return 0;
    }
    auto failure = failures_.find(path);
    if (failure != failures_.end() &&
        std::chrono::steady_clock::now() - failure->second.first < std::chrono::seconds(1)) {
        error = failure->second.second;
        return 0;
    }
    try {
        if (!materialTexturePath(path)) {
            throw std::runtime_error("Unsupported material texture");
        }
        auto found = entries_.find(path);
        const auto now = std::chrono::steady_clock::now();
        if (found != entries_.end()) {
            auto& e = found->second;
            e.used = frame_;
            if (now - e.checked < std::chrono::seconds(1)) {
                return e.texture;
            }
            e.checked = now;
            auto file = assets.resolve(path);
            if (e.stamp == std::filesystem::last_write_time(file) &&
                e.size == std::filesystem::file_size(file)) {
                return e.texture;
            }
            if (e.texture) {
                glDeleteTextures(1, &e.texture);
            }
            bytes_ -= e.bytes;
            entries_.erase(found);
        }
        const auto file = assets.resolve(path);
        const auto size = std::filesystem::file_size(file);
        if (size > 32 * 1024 * 1024) {
            throw std::runtime_error("Material texture exceeds 32 MiB");
        }
        std::ifstream in(file, std::ios::binary);
        std::vector<unsigned char> encoded(static_cast<std::size_t>(size));
        in.read(reinterpret_cast<char*>(encoded.data()), size);
        if (!in || in.peek() != std::char_traits<char>::eof()) {
            throw std::runtime_error("Cannot read material texture");
        }
        int w = 0, h = 0, c = 0;
        if (!stbi_info_from_memory(encoded.data(), int(size), &w, &h, &c) || w <= 0 || h <= 0 ||
            w > 4096 || h > 4096) {
            throw std::runtime_error("Material texture requires at most 4096 x 4096 pixels");
        }
        const std::size_t cost = std::size_t(w) * h * 4 * 4 / 3 + 16;
        while (bytes_ + cost > 128 * 1024 * 1024 || entries_.size() >= 128) {
            auto oldest = entries_.end();
            for (auto it = entries_.begin(); it != entries_.end(); ++it) {
                if (it->second.used != frame_ &&
                    (oldest == entries_.end() || it->second.used < oldest->second.used)) {
                    oldest = it;
                }
            }
            if (oldest == entries_.end()) {
                throw std::runtime_error("Material texture cache exceeds 128 MiB/frame budget");
            }
            glDeleteTextures(1, &oldest->second.texture);
            bytes_ -= oldest->second.bytes;
            entries_.erase(oldest);
        }
        std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
            stbi_load_from_memory(encoded.data(), int(size), &w, &h, &c, 4), stbi_image_free);
        if (!pixels) {
            throw std::runtime_error("Cannot decode material texture");
        }
        // File images are top-down, mesh UVs are bottom-up. Do not change stb global state.
        for (int y = 0; y < h / 2; ++y) {
            for (int x = 0; x < w * 4; ++x) {
                std::swap(pixels.get()[std::size_t(y) * w * 4 + x],
                          pixels.get()[std::size_t(h - y - 1) * w * 4 + x]);
            }
        }
        Entry entry;
        entry.bytes = cost;
        entry.used = frame_;
        entry.checked = now;
        entry.stamp = std::filesystem::last_write_time(file);
        entry.size = size;
        glGenTextures(1, &entry.texture);
        glBindTexture(GL_TEXTURE_2D, entry.texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.get());
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        bytes_ += cost;
        entries_[path] = entry;
        failures_.erase(path);
        return entry.texture;
    } catch (const std::exception& e) {
        error = path + ": " + e.what();
        if (failures_.size() > 256) {
            failures_.clear();
        }
        failures_[path] = {std::chrono::steady_clock::now(), error};
        return 0;
    }
}
