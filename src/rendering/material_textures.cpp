#include "rendering/material_textures.hpp"
#include "core/worker_pool.hpp"
#include "materials/material.hpp"
#include "project/json_io.hpp"
#include "rendering/texture_quality.hpp"
#include <SDL3/SDL.h>
#include <glad/gl.h>
#include <memory>
#include <stb_image.h>
void MaterialTextureCache::clear() {
    // packaged-task futures do not wait on abandoned CPU jobs. Jobs capture only
    // path/root/value snapshots, so a project switch cannot publish stale data.
    pending_.clear();
    waiting_ = false;
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
unsigned MaterialTextureCache::get(const AssetManager& assets,
                                   const std::string& path,
                                   std::string& error,
                                   bool fullResolution,
                                   bool waitForReady) {
    if (quality_ != TextureQuality::current()) {
        clear();
        quality_ = TextureQuality::current();
    }
    if (path.empty()) {
        return 0;
    }
    const std::string cacheKey = path + (fullResolution ? "|full" : "|scaled");
    auto found = entries_.find(cacheKey);
    auto resident = [&] { return found == entries_.end() ? 0u : found->second.texture; };
    const auto now = std::chrono::steady_clock::now();
    if (found != entries_.end()) {
        found->second.used = frame_;
    }
    const auto failure = failures_.find(cacheKey);
    if (failure != failures_.end() && now - failure->second.first < std::chrono::seconds(1)) {
        error = failure->second.second;
        return resident();
    }
    try {
        auto pending = pending_.find(cacheKey);
        if (pending != pending_.end()) {
            if (waitForReady && found == entries_.end()) {
                pending->second.wait();
            }
            if (pending->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready ||
                (!waitForReady && uploads_ >= 1)) {
                waiting_ = true;
                return resident();
            }
            // Erase before get: a failed future is consumed once, never polled again.
            auto future = std::move(pending->second);
            pending_.erase(pending);
            auto image = future.get();
            if (image.unchanged && found != entries_.end()) {
                found->second.checked = now;
                return resident();
            }
            if (image.pixels.empty()) {
                return resident();
            }
            const auto cost = std::size_t(image.width) * image.height * 4 * 4 / 3 + 16;
            if (found != entries_.end()) {
                if (found->second.texture) {
                    glDeleteTextures(1, &found->second.texture);
                }
                bytes_ -= found->second.bytes;
                entries_.erase(found);
                found = entries_.end();
            }
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
            Entry entry;
            entry.generation = ++generation_;
            entry.bytes = cost;
            entry.used = frame_;
            entry.checked = now;
            entry.stamp = image.stamp;
            entry.size = image.size;
            glGenTextures(1, &entry.texture);
            glBindTexture(GL_TEXTURE_2D, entry.texture);
            glTexImage2D(GL_TEXTURE_2D,
                         0,
                         GL_RGBA8,
                         image.width,
                         image.height,
                         0,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         image.pixels.data());
            glGenerateMipmap(GL_TEXTURE_2D);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            bytes_ += cost;
            entries_[cacheKey] = entry;
            ++uploads_;
            failures_.erase(cacheKey);
            return entry.texture;
        }
        if (found != entries_.end() && now - found->second.checked < std::chrono::seconds(1)) {
            return resident();
        }
        if (!waitForReady && pending_.size() >= 2) {
            waiting_ = true;
            return resident();
        }
        const bool hasOld = found != entries_.end();
        const auto stamp = hasOld ? found->second.stamp : std::filesystem::file_time_type{};
        const auto oldSize = hasOld ? found->second.size : 0;
        const int mipReduction = fullResolution ? 0 : quality_;
        auto decode = [assets, path, hasOld, stamp, oldSize, mipReduction] {
            if (!materialTexturePath(path)) {
                throw std::runtime_error("Unsupported material texture");
            }
            const auto file = assets.resolve(path);
            Decoded result;
            result.stamp = std::filesystem::last_write_time(assetContainerPath(file));
            result.size = std::filesystem::file_size(assetContainerPath(file));
            if (hasOld && result.stamp == stamp && result.size == oldSize) {
                result.unchanged = true;
                return result;
            }
            if (!result.size || (!embeddedAsset(path) && result.size > 32 * 1024 * 1024)) {
                throw std::runtime_error("Material texture exceeds 32 MiB or is empty");
            }
            auto encoded = readAssetBytes(file, 32 * 1024 * 1024);
            int channels = 0;
            if (!stbi_info_from_memory(encoded.data(),
                                       int(encoded.size()),
                                       &result.width,
                                       &result.height,
                                       &channels) ||
                result.width <= 0 || result.height <= 0 || result.width > 4096 ||
                result.height > 4096) {
                throw std::runtime_error("Material texture requires at most 4096 x 4096 pixels");
            }
            std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
                stbi_load_from_memory(encoded.data(),
                                      int(encoded.size()),
                                      &result.width,
                                      &result.height,
                                      &channels,
                                      4),
                stbi_image_free);
            if (!pixels) {
                throw std::runtime_error("Cannot decode material texture");
            }
            const auto row = std::size_t(result.width) * 4;
            for (int y = 0; y < result.height / 2; ++y) {
                std::swap_ranges(pixels.get() + y * row,
                                 pixels.get() + (y + 1) * row,
                                 pixels.get() + (result.height - y - 1) * row);
            }
            result.pixels.assign(pixels.get(), pixels.get() + row * result.height);
            // Box-filter on the worker, reducing both upload cost and residency.
            for (int mip = 0; mip < mipReduction && (result.width > 1 || result.height > 1);
                 ++mip) {
                const int w = std::max(1, result.width / 2), h = std::max(1, result.height / 2);
                std::vector<unsigned char> reduced(std::size_t(w) * h * 4);
                for (int y = 0; y < h; ++y) {
                    for (int x = 0; x < w; ++x) {
                        for (int c = 0; c < 4; ++c) {
                            unsigned sum = 0, samples = 0;
                            // Include odd trailing rows/columns in the final destination texel.
                            const int endX = (x + 1) * result.width / w,
                                      endY = (y + 1) * result.height / h;
                            for (int sy = y * result.height / h; sy < endY; ++sy) {
                                for (int sx = x * result.width / w; sx < endX; ++sx) {
                                    sum +=
                                        result
                                            .pixels[(std::size_t(sy) * result.width + sx) * 4 + c];
                                    ++samples;
                                }
                            }
                            reduced[(std::size_t(y) * w + x) * 4 + c] =
                                static_cast<unsigned char>((sum + samples / 2) / samples);
                        }
                    }
                }
                result.pixels = std::move(reduced);
                result.width = w;
                result.height = h;
            }
            if (std::filesystem::last_write_time(assetContainerPath(file)) != result.stamp ||
                std::filesystem::file_size(assetContainerPath(file)) != result.size) {
                throw std::runtime_error("Texture changed while streaming; retrying");
            }
            return result;
        };
        auto request = WorkerPool::shared().submit(decode);
        if (!request && waitForReady && found == entries_.end()) {
            // Imports may fill the bounded pool. Complete this required resource
            // locally rather than displaying a placeholder or growing the queue.
            std::packaged_task<Decoded()> task(std::move(decode));
            request = task.get_future();
            task();
        }
        waiting_ = true;
        if (request) {
            pending_.emplace(cacheKey, std::move(*request));
            // First-use resources are ready before drawing, never revealed as
            // untextured placeholders. Resident hot-reloads remain asynchronous.
            if (waitForReady && found == entries_.end()) {
                return get(assets, path, error, fullResolution, true);
            }
        }
        return resident();
    } catch (const std::exception& e) {
        // Remove deleted/corrupt resources once, not alternating stale IDs/fallback.
        found = entries_.find(cacheKey);
        if (found != entries_.end()) {
            if (found->second.texture) {
                glDeleteTextures(1, &found->second.texture);
            }
            bytes_ -= found->second.bytes;
            entries_.erase(found);
        }
        error = path + ": " + e.what();
        if (failures_.size() > 256) {
            failures_.clear();
        }
        failures_[cacheKey] = {now, error};
        return 0;
    }
}
