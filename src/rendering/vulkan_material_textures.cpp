#include "rendering/vulkan_material_textures.hpp"
#include "materials/material.hpp"
#include <stdexcept>

void VulkanMaterialTextures::beginFrame() {
    ++frame_;
    deferred_ = false;
    // Multiple surfaces may render between uses of a preview. Use wall time,
    // not draw count, to avoid canceling a still-visible preview's request.
    const auto now = std::chrono::steady_clock::now();
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (now - it->second.used > std::chrono::seconds(2)) {
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
}
void VulkanMaterialTextures::clear() {
    worker_.cancelQueued();
    pending_.clear();
    failures_.clear();
    entries_.clear();
    bytes_ = 0;
    root_.clear();
    deferred_ = false;
}
bool VulkanMaterialTextures::pending() const {
    return !pending_.empty() || deferred_;
}
const VulkanTexture* VulkanMaterialTextures::get(const AssetManager& assets,
                                                 const std::string& path) {
    if (path.empty()) {
        return nullptr;
    }
    if (!materialTexturePath(path)) {
        throw std::runtime_error("Unsupported material texture");
    }
    if (root_ != assets.root) {
        clear();
        root_ = assets.root;
    }
    const auto now = std::chrono::steady_clock::now();
    auto found = entries_.find(path);
    if (found != entries_.end()) {
        auto& e = found->second;
        if (e.used == frame_ || now - e.checked < std::chrono::seconds(1)) {
            e.used = frame_;
            return e.image.get();
        }
        e.checked = now;
        const auto file = assets.resolve(path);
        if (e.stamp == std::filesystem::last_write_time(file) &&
            e.size == std::filesystem::file_size(file)) {
            e.used = frame_;
            return e.image.get();
        }
        bytes_ -= e.bytes;
        entries_.erase(found);
    }
    auto failed = failures_.find(path);
    if (failed != failures_.end()) {
        if (now < failed->second.retry) {
            throw std::runtime_error(failed->second.message);
        }
        failures_.erase(failed);
    }
    auto work = pending_.find(path);
    if (work == pending_.end()) {
        // At most two decoded images are retained while waiting for upload.
        if (pending_.size() >= 2) {
            deferred_ = true;
            return nullptr;
        }
        auto future =
            worker_.submit([assets, path] { return decodeStreamedTexture(assets, path); });
        if (future) {
            pending_.emplace(path, Pending{std::move(*future), now});
        } else {
            deferred_ = true;
        }
        return nullptr;
    }
    work->second.used = now;
    if (work->second.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        return nullptr;
    }
    StreamedTexture decoded;
    try {
        decoded = work->second.future.get();
    } catch (const std::exception& error) {
        std::string message = error.what();
        pending_.erase(work);
        if (failures_.size() >= 128) {
            failures_.erase(failures_.begin());
        }
        failures_[path] = {message, now + std::chrono::seconds(1)};
        throw std::runtime_error(message);
    }
    pending_.erase(work);
    const std::size_t cost = std::size_t(decoded.width) * decoded.height * 4 * 4 / 3 + 16;
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
        bytes_ -= oldest->second.bytes;
        entries_.erase(oldest);
    }
    Entry entry;
    entry.bytes = cost;
    entry.used = frame_;
    entry.checked = now;
    entry.stamp = decoded.stamp;
    entry.size = decoded.size;
    entry.image = std::make_unique<VulkanTexture>(surface_.physicalDevice(),
                                                  surface_.device(),
                                                  surface_.graphicsQueue(),
                                                  surface_.graphicsFamily(),
                                                  decoded.width,
                                                  decoded.height,
                                                  decoded.pixels,
                                                  VulkanTexture::Address::Repeat,
                                                  true);
    auto result = entries_.emplace(path, std::move(entry)).first;
    bytes_ += cost;
    return result->second.image.get();
}
