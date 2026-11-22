#include "assets/texture_preview.hpp"
#include "core/worker_pool.hpp"
#include "materials/material_io.hpp"
#include <SDL3/SDL.h>
#include <glad/gl.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_MAX_DIMENSIONS 16384
#include <fstream>
#include <memory>
#include <sstream>
#include <stb_image.h>

bool textureAsset(const std::string& path) {
    const auto e = AssetManager::extension(path);
    return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".bmp" || e == ".tga" || e == ".hdr";
}
std::filesystem::path thumbnailCachePath(const AssetManager& assets, const std::string& path) {
    assets.resolve(path);
    std::uint64_t hash = 14695981039346656037ull;
    for (unsigned char c : std::filesystem::path(path).generic_string()) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    std::ostringstream name;
    name << std::hex << hash << ".rgba";
    const auto project =
        assets.root.filename() == "Assets" ? assets.root.parent_path() : assets.root;
    auto cache = project / ".cache", folder = cache / "thumbnails";
    if (std::filesystem::is_symlink(cache) || std::filesystem::is_symlink(folder)) {
        throw std::runtime_error("Thumbnail cache may not be a symlink");
    }
    auto file = folder / name.str();
    if (std::filesystem::is_symlink(file)) {
        throw std::runtime_error("Thumbnail entry may not be a symlink");
    }
    return file;
}
TextureThumbnail loadTextureThumbnail(const AssetManager& assets,
                                      const std::string& path,
                                      bool* cacheHit,
                                      bool allowDecode) {
    if (cacheHit) {
        *cacheHit = false;
    }
    if (!textureAsset(path)) {
        throw std::runtime_error("Unsupported thumbnail format");
    }
    const auto source = assets.resolve(path);
    const auto bytes = std::filesystem::file_size(assetContainerPath(source));
    if (!embeddedAsset(path) && bytes > 32 * 1024 * 1024) {
        throw std::runtime_error("Texture exceeds thumbnail input limit");
    }
    const auto stamp =
        std::filesystem::last_write_time(assetContainerPath(source)).time_since_epoch().count();
    auto cache = thumbnailCachePath(assets, path);
    TextureThumbnail result;
    {
        std::ifstream in(cache, std::ios::binary);
        std::string header;
        if (in &&
            std::filesystem::file_size(cache) <
                TextureThumbnail::size * TextureThumbnail::size * 4 + 512 &&
            std::getline(in, header)) {
            std::istringstream metadata(header);
            std::string magic;
            std::uintmax_t savedBytes = 0;
            std::int64_t savedStamp = 0;
            metadata >> magic >> savedBytes >> savedStamp;
            if (metadata && magic == "LegacyThumbnail2" && savedBytes == bytes &&
                savedStamp == stamp) {
                in.read(reinterpret_cast<char*>(result.pixels.data()), result.pixels.size());
                if (in.gcount() == static_cast<std::streamsize>(result.pixels.size()) &&
                    in.peek() == std::char_traits<char>::eof()) {
                    if (cacheHit) {
                        *cacheHit = true;
                    }
                    return result;
                }
            }
        }
    }
    if (!allowDecode) {
        throw std::runtime_error("Thumbnail is not cached");
    }
    int width = 0, height = 0, channels = 0;
    // Bounded encoded read also handles Unicode filenames on Windows.
    auto encoded = readAssetBytes(source, 32 * 1024 * 1024);
    if ((!embeddedAsset(path) && encoded.size() != bytes) ||
        !stbi_info_from_memory(encoded.data(), int(encoded.size()), &width, &height, &channels) ||
        width <= 0 || height <= 0 || std::uint64_t(width) * height > 16 * 1024 * 1024) {
        throw std::runtime_error("Invalid or oversized texture");
    }
    std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(encoded.data(), int(encoded.size()), &width, &height, &channels, 4),
        stbi_image_free);
    if (!pixels) {
        throw std::runtime_error("Cannot decode texture preview");
    }
    const float factor =
        std::min(float(TextureThumbnail::size) / width, float(TextureThumbnail::size) / height);
    const int outW = std::max(1, int(width * factor)), outH = std::max(1, int(height * factor));
    const int left = (TextureThumbnail::size - outW) / 2, top = (TextureThumbnail::size - outH) / 2;
    for (int y = 0; y < outH; ++y) {
        for (int x = 0; x < outW; ++x) {
            // Area average avoids aliasing high-frequency texture patterns.
            const int x0 = x * width / outW, x1 = std::max(x0 + 1, (x + 1) * width / outW),
                      y0 = y * height / outH, y1 = std::max(y0 + 1, (y + 1) * height / outH);
            std::uint64_t sum[4]{};
            for (int sy = y0; sy < y1; ++sy) {
                for (int sx = x0; sx < x1; ++sx) {
                    for (int c = 0; c < 4; ++c) {
                        sum[c] += pixels.get()[(std::size_t(sy) * width + sx) * 4 + c];
                    }
                }
            }
            const auto count = (x1 - x0) * (y1 - y0);
            for (int c = 0; c < 4; ++c) {
                result.pixels[((y + top) * TextureThumbnail::size + x + left) * 4 + c] =
                    static_cast<unsigned char>(sum[c] / count);
            }
        }
    }
    // Cache writes are best effort: a read-only project can still display previews.
    try {
        std::filesystem::create_directories(cache.parent_path());
        auto temp = cache;
        temp +=
            ".tmp-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        try {
            std::ofstream out(temp, std::ios::binary);
            out << "LegacyThumbnail2 " << bytes << ' ' << stamp << '\n';
            out.write(reinterpret_cast<const char*>(result.pixels.data()), result.pixels.size());
            out.close();
            if (!out) {
                throw std::runtime_error("Cannot write thumbnail cache");
            }
            std::error_code ec;
            std::filesystem::rename(temp, cache, ec);
            if (ec) {
                std::filesystem::remove(cache, ec);
                ec.clear();
                std::filesystem::rename(temp, cache, ec);
                if (ec) {
                    std::filesystem::remove(temp, ec);
                }
            }
        } catch (...) {
            std::error_code ec;
            std::filesystem::remove(temp, ec);
        }
    } catch (...) {
    }
    return result;
}
namespace {
DecodedThumbnail decodeThumbnail(const AssetManager& assets, const std::string& path) {
    DecodedThumbnail result;
    auto& image = result.image;
    if (materialAsset(path)) {
        const auto material = loadMaterial(assets, path);
        result.dependency = material.texture;
        if (!result.dependency.empty()) {
            const auto dependency = assetContainerPath(assets.resolve(result.dependency));
            result.dependencyExists = std::filesystem::is_regular_file(dependency);
            if (result.dependencyExists) {
                result.dependencyModified = std::filesystem::last_write_time(dependency);
                result.dependencyBytes = std::filesystem::file_size(dependency);
            }
        }
        std::filesystem::path cache;
        std::string signature;
        try {
            cache = thumbnailCachePath(assets, path);
            cache += ".material-v1";
            if (std::filesystem::is_symlink(cache)) {
                cache.clear();
            }
            const auto source = assetContainerPath(assets.resolve(path));
            signature = "LEMaterialThumbnail1 " +
                        std::to_string(std::filesystem::file_size(source)) + " " +
                        std::to_string(
                            std::filesystem::last_write_time(source).time_since_epoch().count()) +
                        " " + std::to_string(result.dependencyExists) + " " +
                        std::to_string(result.dependencyBytes) + " " +
                        std::to_string(result.dependencyModified.time_since_epoch().count());
            if (!cache.empty()) {
                std::ifstream input(cache, std::ios::binary);
                std::string header;
                if (std::getline(input, header) && header == signature) {
                    input.read(reinterpret_cast<char*>(image.pixels.data()), image.pixels.size());
                    if (input && input.peek() == std::char_traits<char>::eof()) {
                        return result;
                    }
                    image = {};
                }
            }
        } catch (...) {
            cache.clear();
        }
        TextureThumbnail albedo;
        bool textured = false;
        if (!material.texture.empty() && result.dependencyExists) {
            albedo = loadTextureThumbnail(assets, material.texture);
            textured = true;
        }
        for (int y = 0; y < TextureThumbnail::size; ++y) {
            for (int x = 0; x < TextureThumbnail::size; ++x) {
                float nx = (x + .5f) / TextureThumbnail::size * 2 - 1,
                      ny = 1 - (y + .5f) / TextureThumbnail::size * 2;
                float r = nx * nx + ny * ny;
                auto i = (y * TextureThumbnail::size + x) * 4;
                float background = ((x / 8 + y / 8) % 2) ? .16f : .12f;
                Vec3 color{background, background, background};
                if (r < .9f) {
                    float nz = std::sqrt(1 - r / .9f);
                    float light = .18f + .82f * std::max(0.f, (-nx + ny + nz) * .577f);
                    float spec =
                        material.specular * std::pow(std::max(0.f, (-nx + ny + nz) * .577f), 24.f);
                    Vec3 tex{1, 1, 1};
                    if (textured) {
                        tex = {albedo.pixels[i] / 255.f,
                               albedo.pixels[i + 1] / 255.f,
                               albedo.pixels[i + 2] / 255.f};
                    }
                    color = {material.color.x * tex.x * light + spec,
                             material.color.y * tex.y * light + spec,
                             material.color.z * tex.z * light + spec};
                }
                image.pixels[i] = static_cast<unsigned char>(std::clamp(color.x, 0.f, 1.f) * 255);
                image.pixels[i + 1] =
                    static_cast<unsigned char>(std::clamp(color.y, 0.f, 1.f) * 255);
                image.pixels[i + 2] =
                    static_cast<unsigned char>(std::clamp(color.z, 0.f, 1.f) * 255);
                image.pixels[i + 3] = 255;
            }
        }
        if (!cache.empty()) {
            auto temp = cache;
            temp += ".tmp-" +
                    std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
            try {
                std::filesystem::create_directories(cache.parent_path());
                std::ofstream output(temp, std::ios::binary);
                output << signature << '\n';
                output.write(reinterpret_cast<const char*>(image.pixels.data()),
                             image.pixels.size());
                output.close();
                std::error_code ec;
                if (output) {
                    std::filesystem::rename(temp, cache, ec);
                    if (ec) {
                        std::filesystem::remove(cache, ec);
                        ec.clear();
                        std::filesystem::rename(temp, cache, ec);
                    }
                }
            } catch (...) { /* Cache writes are optional. */
            }
            std::error_code ec;
            std::filesystem::remove(temp, ec);
        }
    } else {
        image = loadTextureThumbnail(assets, path);
    }
    return result;
}
void uploadThumbnail(unsigned& texture, const TextureThumbnail& image) {
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA8,
                 TextureThumbnail::size,
                 TextureThumbnail::size,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 image.pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
} // namespace
void TexturePreviewCache::clear() {
    if (SDL_GL_GetCurrentContext()) {
        for (auto& pair : entries_) {
            if (pair.second.texture) {
                glDeleteTextures(1, &pair.second.texture);
            }
        }
    }
    entries_.clear();
    root_.clear();
}
void TexturePreviewCache::finish(Entry& e) {
    if (!e.pending.valid()) {
        return;
    }
    if (e.pending.wait_for(std::chrono::seconds(0)) != std::future_status::ready || budget_ <= 0) {
        return;
    }
    --budget_;
    try {
        const auto result = e.pending.get();
        e.dependency = result.dependency;
        e.dependencyExists = result.dependencyExists;
        e.dependencyModified = result.dependencyModified;
        e.dependencyBytes = result.dependencyBytes;
        uploadThumbnail(e.texture, result.image);
    } catch (...) {
        e.failed = true;
    }
    e.checked = {}; // Revalidate source metadata after the asynchronous job.
}
void TexturePreviewCache::beginFrame(const AssetManager& assets) {
    if (root_ != assets.root) {
        clear();
        root_ = assets.root;
    }
    ++frame_;
    budget_ = 4;
    // Drain completed offscreen requests too, so scrolling cannot stall admission.
    for (auto& pair : entries_) {
        finish(pair.second);
    }
}
unsigned TexturePreviewCache::texture(const AssetManager& assets, const std::string& path) {
    if ((!textureAsset(path) && !materialAsset(path)) || !SDL_GL_GetCurrentContext()) {
        return 0;
    }
    auto found = entries_.find(path);
    const auto now = std::chrono::steady_clock::now();
    if (found != entries_.end()) {
        auto& e = found->second;
        e.touched = frame_;
        finish(e);
        if (e.pending.valid()) {
            return e.texture;
        }
        if (now - e.checked < std::chrono::seconds(1)) {
            return e.texture;
        }
        e.checked = now;
        try {
            const auto file = assetContainerPath(assets.resolve(path));
            bool dependencyCurrent = true;
            if (!e.dependency.empty()) {
                const auto dependency = assetContainerPath(assets.resolve(e.dependency));
                const bool exists = std::filesystem::is_regular_file(dependency);
                dependencyCurrent =
                    exists == e.dependencyExists &&
                    (!exists ||
                     (e.dependencyModified == std::filesystem::last_write_time(dependency) &&
                      e.dependencyBytes == std::filesystem::file_size(dependency)));
            }
            if (dependencyCurrent && e.modified == std::filesystem::last_write_time(file) &&
                e.bytes == std::filesystem::file_size(file)) {
                return e.texture;
            }
        } catch (...) {
            // Never return a previously cached, now-deleted texture next frame.
            if (e.texture) {
                glDeleteTextures(1, &e.texture);
            }
            e.texture = 0;
            e.failed = true;
            return 0;
        }
        if (e.texture) {
            glDeleteTextures(1, &e.texture);
        }
        entries_.erase(found);
    }
    if (budget_ <= 0) {
        return 0;
    }
    // At most one CPU decode per cache in flight; visible assets request lazily.
    for (const auto& pair : entries_) {
        if (pair.second.pending.valid()) {
            return 0;
        }
    }
    --budget_;
    if (entries_.size() >= 256) {
        auto oldest =
            std::min_element(entries_.begin(), entries_.end(), [](const auto& a, const auto& b) {
                return a.second.touched < b.second.touched;
            });
        if (oldest->second.texture) {
            glDeleteTextures(1, &oldest->second.texture);
        }
        entries_.erase(oldest);
    }
    auto& entry = entries_[path];
    entry.touched = frame_;
    entry.checked = now;
    try {
        const auto file = assetContainerPath(assets.resolve(path));
        entry.modified = std::filesystem::last_write_time(file);
        entry.bytes = std::filesystem::file_size(file);
        auto job =
            WorkerPool::shared().submit([assets, path] { return decodeThumbnail(assets, path); });
        if (job) {
            entry.pending = std::move(*job);
        } else {
            entries_.erase(path); // Saturated queues retry on a later frame.
        }
        return 0;
    } catch (...) {
        entry.failed = true;
    }
    return entry.texture;
}
