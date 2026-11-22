#pragma once
#include "animation/animation_clip.hpp"
#include "assets/cubemap.hpp"
#include "assets/texture_preview.hpp"
#include "audio/audio_clip.hpp"
#include "core/worker_pool.hpp"
#include "editor/asset_preview_ui.hpp"
#include "editor/rotation_editor.hpp"
#include "models/model_asset.hpp"
#include "prefabs/prefab.hpp"
#include "rendering/renderer.hpp"
#include "rendering/scene_surface.hpp"
#include "scene/camera.hpp"
#include "terrain/terrain_io.hpp"
#include <array>
#include <deque>
#include <fstream>

// Independent Inspector, tooltip and picker-detail slots prevent image aliasing.
class AssetPreviewRenderer {
    struct Slot {
        SceneSurface surface;
        std::future<AssetPreviewData> audioJob;
        unsigned cachedTexture = 0;
        bool saved = false;
        ~Slot() {
            if (cachedTexture && SDL_GL_GetCurrentContext()) {
                glDeleteTextures(1, &cachedTexture);
            }
        }
        int lastUsed = 0;
        bool streaming = false, waitingBounds = false;
        std::chrono::steady_clock::time_point nextBounds{};
        AssetPreviewData data;
        std::unique_ptr<World> world;
        OrbitCamera camera;
        std::string key;
        unsigned revision = 0;
        std::filesystem::file_time_type modified{};
        std::chrono::steady_clock::time_point checked{};
    };
    Renderer& renderer_;
    ProjectIO::Meshes meshes_;
    std::array<Slot, 3> slots_;
    int thumbnailFrame_ = -1;
    std::map<std::string, std::unique_ptr<Slot>> icons_;
    MaterialTextureCache textures_;
    TexturePreviewCache thumbnails_;
    TexturePreviewCache pickerThumbnails_;
    std::filesystem::path root_;
    int iconFrame_ = -1, iconBudget_ = 1, iconDrawBudget_ = 1;
    std::chrono::steady_clock::time_point nextIcon_{};

    struct Readback {
        unsigned buffer = 0;
        GLsync fence = nullptr;
        std::size_t bytes = 0;
        std::filesystem::path file;
        Json meta;
    };
    std::deque<Readback> readbacks_;
    struct Write {
        std::filesystem::path file;
        Json meta;
        std::vector<unsigned char> pixels;
    };
    std::deque<std::shared_ptr<Write>> writes_;
    void writePreview(std::filesystem::path file, Json meta, std::vector<unsigned char> pixels) {
        writes_.push_back(
            std::make_shared<Write>(Write{std::move(file), std::move(meta), std::move(pixels)}));
    }
    void drainWrites() {
        if (writes_.empty()) {
            return;
        }
        auto data = writes_.front();
        auto write = WorkerPool::shared().submit([data] {
            const auto& file = data->file;
            auto temporary = file;
            temporary += ".tmp-" + std::to_string(
                                       std::chrono::steady_clock::now().time_since_epoch().count());
            try {
                std::filesystem::create_directories(file.parent_path());
                std::ofstream output(temporary, std::ios::binary);
                output << data->meta.dump() << '\n';
                output.write(reinterpret_cast<const char*>(data->pixels.data()),
                             data->pixels.size());
                output.close();
                std::error_code ec;
                if (output) {
                    std::filesystem::rename(temporary, file, ec);
                    if (ec) {
                        std::filesystem::remove(file, ec);
                        ec.clear();
                        std::filesystem::rename(temporary, file, ec);
                    }
                }
                std::filesystem::remove(temporary, ec);
            } catch (...) {
                std::error_code ec;
                std::filesystem::remove(temporary, ec);
            }
        });
        if (write) {
            writes_.pop_front(); // Retry worker admission rather than dropping a cache.
        }
    }
    void drainReadbacks() {
        if (readbacks_.empty() || writes_.size() >= 8) {
            return;
        }
        auto& item = readbacks_.front();
        const auto status = glClientWaitSync(item.fence, 0, 0); // never wait on the render thread
        if (status == GL_TIMEOUT_EXPIRED) {
            return;
        }
        if (status != GL_WAIT_FAILED) {
            GLint old;
            glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &old);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, item.buffer);
            const auto* data = static_cast<const unsigned char*>(
                glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, item.bytes, GL_MAP_READ_BIT));
            if (data) {
                std::vector<unsigned char> pixels(data, data + item.bytes);
                glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
                writePreview(item.file, std::move(item.meta), std::move(pixels));
            }
            glBindBuffer(GL_PIXEL_PACK_BUFFER, old);
        }
        glDeleteSync(item.fence);
        glDeleteBuffers(1, &item.buffer);
        readbacks_.pop_front();
    }
    struct Inventory {
        std::uint64_t signature = 14695981039346656037ull;
        std::vector<std::string> paths;
    };
    std::filesystem::path inventoryRoot_;
    std::future<Inventory> inventoryJob_;
    std::uint64_t signature_ = 0;
    std::chrono::steady_clock::time_point nextInventory_{};
    struct Warm {
        std::string path;
        bool icon;
        std::chrono::steady_clock::time_point started{};
    };
    std::deque<Warm> warm_;
    bool rescan_ = true;
    std::chrono::steady_clock::time_point nextWarm_{}, nextGeneration_{};
    static Inventory inventory(AssetManager assets, std::uint64_t previous) {
        Inventory result;
        if (!std::filesystem::exists(assets.root)) {
            return result;
        }
        std::vector<std::string> files;
        for (auto it = std::filesystem::recursive_directory_iterator(assets.root);
             it != std::filesystem::recursive_directory_iterator();
             ++it) {
            if (it->is_symlink()) {
                if (it->is_directory()) {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (it->is_directory()) {
                if (it->path().filename() == ".cache") {
                    it.disable_recursion_pending();
                }
                continue;
            }
            if (it->is_regular_file()) {
                if (files.size() >= 10000) {
                    throw std::runtime_error("Preview inventory exceeds 10000 assets");
                }
                files.push_back(it->path().lexically_relative(assets.root).generic_string());
            }
        }
        std::sort(files.begin(), files.end());
        auto hash = [&](const std::string& value) {
            for (unsigned char c : value) {
                result.signature ^= c;
                result.signature *= 1099511628211ull;
            }
            result.signature ^= 255;
            result.signature *= 1099511628211ull;
        };
        for (const auto& file : files) {
            const auto physical = assets.resolve(file);
            hash(file);
            hash(std::to_string(std::filesystem::file_size(physical)));
            hash(std::to_string(
                std::filesystem::last_write_time(physical).time_since_epoch().count()));
        }
        if (result.signature == previous) {
            return result;
        }
        for (const auto& file : files) {
            const auto physical = assets.resolve(file);
            result.paths.push_back(file);
            if (modelAsset(file)) {
                try {
                    const auto doc = readModelContainer(physical);
                    if (doc->contains("embeddedAssets")) {
                        for (auto it = doc->at("embeddedAssets").begin();
                             it != doc->at("embeddedAssets").end() && result.paths.size() < 20000;
                             ++it) {
                            result.paths.push_back(file + "#" + it.key());
                        }
                    }
                } catch (...) { /* Corrupt files get a cached error descriptor. */
                }
            }
        }
        return result;
    }
    std::filesystem::path
    modelPreviewPath(const AssetManager& assets, const std::string& path, bool icon) {
        auto file = thumbnailCachePath(assets, path);
        file += icon ? ".icon-v3" : ".hover-v3";
        if (std::filesystem::is_symlink(file)) {
            throw std::runtime_error("Invalid preview cache");
        }
        return file;
    }
    bool
    loadModelPreview(Slot& slot, const AssetManager& assets, const std::string& path, bool icon) {
        try {
            const auto source = assetContainerPath(assets.resolve(path));
            const auto file = modelPreviewPath(assets, path, icon);
            if (!std::filesystem::exists(file) ||
                std::filesystem::file_size(file) > 512 * 512 * 4 + 65536) {
                return false;
            }
            std::ifstream input(file, std::ios::binary);
            std::string header;
            if (!std::getline(input, header) || header.size() > 65536) {
                return false;
            }
            const auto meta = parseJson(header, 65536);
            if (meta.value("version", 0) != 3 ||
                meta.at("project").get<std::uint64_t>() != signature_ ||
                meta.at("bytes").get<std::uintmax_t>() != std::filesystem::file_size(source) ||
                meta.at("stamp").get<std::int64_t>() !=
                    std::filesystem::last_write_time(source).time_since_epoch().count()) {
                return false;
            }
            AssetPreviewData data;
            data.width = meta.at("width");
            data.height = meta.at("height");
            data.flip = meta.at("flip");
            data.text = meta.value("text", std::string{});
            data.error = meta.value("error", std::string{});
            data.seconds = meta.value("seconds", 0.f);
            data.waveform = meta.value("waveform", std::vector<float>{});
            if (data.width < 1 || data.height < 1 || data.width > 512 || data.height > 512 ||
                data.text.size() > 8192 || data.error.size() > 8192 || data.waveform.size() > 256 ||
                !std::isfinite(data.seconds)) {
                return false;
            }
            if (meta.at("image").get<bool>()) {
                std::vector<unsigned char> pixels(std::size_t(data.width) * data.height * 4);
                input.read(reinterpret_cast<char*>(pixels.data()), pixels.size());
                if (!input || input.peek() != std::char_traits<char>::eof()) {
                    return false;
                }
                GLint binding = 0;
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
                glGenTextures(1, &slot.cachedTexture);
                glBindTexture(GL_TEXTURE_2D, slot.cachedTexture);
                glTexImage2D(GL_TEXTURE_2D,
                             0,
                             GL_RGBA8,
                             data.width,
                             data.height,
                             0,
                             GL_RGBA,
                             GL_UNSIGNED_BYTE,
                             pixels.data());
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glBindTexture(GL_TEXTURE_2D, binding);
                data.texture = slot.cachedTexture;
            }
            slot.data = std::move(data);
            slot.world.reset();
            slot.waitingBounds = slot.streaming = false;
            slot.saved = true;
            return true;
        } catch (...) {
            return false;
        }
    }
    void
    saveModelPreview(Slot& slot, const AssetManager& assets, const std::string& path, bool icon) {
        if (readbacks_.size() >= 8) {
            return;
        }
        Readback pending;
        try {
            const auto source = assetContainerPath(assets.resolve(path));
            const auto file = modelPreviewPath(assets, path, icon);
            const float scale =
                std::min(1.f, 512.f / std::max({1, slot.data.width, slot.data.height}));
            int width = icon ? 128 : std::max(1, int(slot.data.width * scale)),
                height = icon ? 128 : std::max(1, int(slot.data.height * scale));
            if (slot.data.texture) {
                struct Restore {
                    GLint read, draw, binding, renderbuffer;
                    bool scissor;
                    Restore() {
                        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
                        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
                        glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
                        glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
                        scissor = glIsEnabled(GL_SCISSOR_TEST);
                        glDisable(GL_SCISSOR_TEST);
                    }
                    ~Restore() {
                        glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
                        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
                        glBindTexture(GL_TEXTURE_2D, binding);
                        glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
                        if (scissor) {
                            glEnable(GL_SCISSOR_TEST);
                        } else {
                            glDisable(GL_SCISSOR_TEST);
                        }
                    }
                } restore;
                GLint read = 0, draw = 0, viewport[4];
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
                glGetIntegerv(GL_VIEWPORT, viewport);
                unsigned framebuffer = 0;
                glGenFramebuffers(1, &framebuffer);
                SceneSurface scaled;
                scaled.resize(width, height);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
                glFramebufferTexture2D(
                    GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, slot.data.texture, 0);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, scaled.framebuffer);
                GLint binding = 0, sw = 0, sh = 0;
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
                glBindTexture(GL_TEXTURE_2D, slot.data.texture);
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &sw);
                glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &sh);
                glBindTexture(GL_TEXTURE_2D, binding);
                glBlitFramebuffer(
                    0, 0, sw, sh, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, scaled.framebuffer);
                pending.bytes = std::size_t(width) * height * 4;
                GLint oldPack;
                glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &oldPack);
                glGenBuffers(1, &pending.buffer);
                glBindBuffer(GL_PIXEL_PACK_BUFFER, pending.buffer);
                glBufferData(GL_PIXEL_PACK_BUFFER, pending.bytes, nullptr, GL_STREAM_READ);
                glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                pending.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                glBindBuffer(GL_PIXEL_PACK_BUFFER, oldPack);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
                glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
                glDeleteFramebuffers(1, &framebuffer);
            }
            Json meta{
                {"version", 3},
                {"project", signature_},
                {"bytes", std::filesystem::file_size(source)},
                {"stamp", std::filesystem::last_write_time(source).time_since_epoch().count()},
                {"width", width},
                {"height", height},
                {"flip", slot.data.flip},
                {"image", pending.buffer != 0},
                {"text", slot.data.text.substr(0, 8192)},
                {"error", slot.data.error.substr(0, 8192)},
                {"seconds", slot.data.seconds},
                {"waveform", slot.data.waveform}};
            if (pending.buffer) {
                pending.file = file;
                pending.meta = std::move(meta);
                readbacks_.push_back(std::move(pending));
            } else {
                writePreview(file, std::move(meta), {});
            }
        } catch (...) {
            if (pending.fence) {
                glDeleteSync(pending.fence);
            }
            if (pending.buffer) {
                glDeleteBuffers(1, &pending.buffer);
            }
        }
    }

    void build(Slot& slot, const AssetManager& assets, const std::string& path, bool icon) {
        slot.data = {};
        slot.waitingBounds = false;
        slot.world = std::make_unique<World>();
        auto& world = *slot.world;
        if (textureAsset(path)) {
            if (icon) {
                pickerThumbnails_.beginFrame(assets);
                slot.data.texture = pickerThumbnails_.texture(assets, path);
                slot.data.width = slot.data.height = 128;
                slot.waitingBounds = !slot.data.texture;
                slot.world.reset();
                return;
            }
            if (AssetManager::extension(path) == ".hdr") {
                thumbnails_.beginFrame(assets);
                slot.data.texture = thumbnails_.texture(assets, path);
                slot.data.width = slot.data.height = 128;
                if (!slot.data.texture) {
                    slot.data.error = "HDR preview unavailable";
                }
            } else {
                slot.data.flip = true; // Material uploads are bottom-up; ImGui images are top-down.
                textures_.beginFrame();
                slot.data.texture = textures_.get(assets, path, slot.data.error);
                if (slot.data.texture) {
                    GLint binding = 0;
                    glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
                    glBindTexture(GL_TEXTURE_2D, slot.data.texture);
                    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &slot.data.width);
                    glGetTexLevelParameteriv(
                        GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &slot.data.height);
                    glBindTexture(GL_TEXTURE_2D, binding);
                }
            }
            slot.world.reset();
            return;
        }
        if (audioAsset(path)) {
            if (slot.audioJob.valid() &&
                slot.audioJob.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                slot.data = slot.audioJob.get();
            } else {
                if (!slot.audioJob.valid()) {
                    auto job = WorkerPool::shared().submit([assets, path] {
                        AssetPreviewData result;
                        const auto samples = decodeAudioClip(assets, path);
                        result.seconds = float(samples.size()) / 96000;
                        result.waveform.resize(256);
                        for (std::size_t i = 0; i < samples.size(); ++i) {
                            auto& value = result.waveform[std::min<std::size_t>(
                                255, i * 256 / std::max<std::size_t>(1, samples.size()))];
                            value = std::max(value, std::abs(samples[i]));
                        }
                        return result;
                    });
                    if (job) {
                        slot.audioJob = std::move(*job);
                    }
                }
                slot.waitingBounds = true;
            }
            slot.world.reset();
            return;
        }
        const auto extension = AssetManager::extension(path);
        if (AssetManager::script(path) || extension == ".txt" || extension == ".md" ||
            extension == ".json") {
            std::ifstream file(assets.resolve(path), std::ios::binary);
            if (!file) {
                throw std::runtime_error("Cannot read preview");
            }
            std::array<char, 4096> buffer{};
            file.read(buffer.data(), buffer.size());
            std::string text(buffer.data(), std::size_t(file.gcount()));
            unsigned lines = 0;
            std::size_t end = 0;
            for (; end < text.size(); ++end) {
                if (text[end] == '\n' && ++lines >= 18) {
                    break;
                }
                if (static_cast<unsigned char>(text[end]) < 32 && text[end] != '\n' &&
                    text[end] != '\t') {
                    text[end] = ' ';
                }
            }
            slot.data.text =
                text.substr(0, end) + (end < text.size() || !file.eof() ? "\n..." : "");
            slot.world.reset();
            return;
        }
        if (animationAsset(path)) {
            const auto clip =
                decodeAnimationClip(readJsonFile(assets.resolve(path), 16 * 1024 * 1024));
            std::size_t keys = 0;
            for (const auto& track : clip.tracks) {
                keys += track.keys.size();
            }
            slot.data.text = "Animation clip\nDuration: " + std::to_string(clip.duration) +
                             " seconds\nTracks: " + std::to_string(clip.tracks.size()) +
                             "\nKeys: " + std::to_string(keys);
            if (!clip.model.empty()) {
                slot.data.text += "\nModel: " + clip.model;
            }
            slot.world.reset();
            return;
        }
        if (Terrains::asset(path)) {
            auto e = world.create();
            world.add(e, Transform{});
            Terrain terrain;
            terrain.asset = path;
            Terrains::load(terrain, assets);
            world.add(e, terrain);
        } else if (materialAsset(path)) {
            auto entity = world.create();
            world.add(entity, Transform{});
            MeshRenderer mesh{meshes_[3]};
            loadMaterial(assets, path); // Validate before handing the shared reference to Renderer.
            mesh.material = path;
            world.add(entity, mesh);
        } else if (modelAsset(path)) {
            const auto doc = modelDocument(assets, path);
            auto add = [&](const std::string& meshPath, const std::string& material) {
                auto entity = world.create();
                world.add(entity, Transform{});
                MeshRenderer mesh;
                mesh.model = meshPath;
                mesh.material = material;
                world.add(entity, mesh);
            };
            if (doc.at("version") == 2) {
                if (!doc.at("parts").is_array() || doc.at("parts").size() > 1024) {
                    throw std::runtime_error("Invalid model bundle");
                }
                for (const auto& part : doc.at("parts")) {
                    add(part.at("mesh"), part.at("material"));
                }
            } else {
                add(path, "");
            }
        } else if (Prefabs::asset(path)) {
            auto local = assets;
            slot.world = Prefabs::load(local, path, meshes_);
        } else if (cubemapAsset(path)) {
            auto entity = world.create();
            Skybox sky;
            sky.cubemap = path;
            world.add(entity, sky);
        } else {
            slot.data.text = "No visual preview available for this asset type.";
            slot.world.reset();
            return;
        }
        auto& scene = *slot.world;
        updateHierarchy(scene);
        Terrains::update(scene, assets);
        for (auto entity : scene.entities()) {
            scene.remove<Camera>(entity);
            scene.remove<DirectionalLight>(entity);
            scene.remove<PointLight>(entity);
            scene.remove<SpotLight>(entity);
            scene.remove<Parent>(entity);
        }
        bool any = false;
        Vec3 low{}, high{};
        scene.each<Transform, MeshRenderer>(
            [&](Entity, const Transform& transform, const MeshRenderer& mesh) {
                auto include = [&](Vec3 p) {
                    p = transform.position + rotateVector(transform.rotation, p * transform.scale);
                    if (!any) {
                        low = high = p;
                        any = true;
                    }
                    low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
                    high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
                };
                if (!mesh.model.empty()) {
                    Vec3 a{}, b{};
                    if (renderer_.modelBounds(assets, mesh.model, a, b, 0, true)) {
                        for (int i = 0; i < 8; ++i) {
                            include({i & 1 ? b.x : a.x, i & 2 ? b.y : a.y, i & 4 ? b.z : a.z});
                        }
                    } else {
                        slot.waitingBounds = true;
                    }
                } else {
                    for (int i = 0; i < 8; ++i) {
                        const float extent = materialAsset(path) ? .5f : 1.f;
                        include({i & 1 ? extent : -extent,
                                 i & 2 ? extent : -extent,
                                 i & 4 ? extent : -extent});
                    }
                }
            });
        scene.eachReadOnly<Transform, Terrain>([&](Entity,
                                                   const Transform& t,
                                                   const Terrain& terrain) {
            if (!terrain.data) {
                return;
            }
            const auto& d = *terrain.data;
            for (int z = 0; z < d.resolution; ++z) {
                for (int x = 0; x < d.resolution; ++x) {
                    auto p =
                        t.position + rotateVector(t.rotation, Terrains::point(d, x, z) * t.scale);
                    if (!any) {
                        low = high = p;
                        any = true;
                    }
                    low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
                    high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
                }
            }
        });
        const auto center = (low + high) * .5f;
        const auto size = high - low;
        const float radius =
            materialAsset(path) ? .5f : std::max(1e-20f, std::sqrt(dot(size, size)) * .5f);
        if (any) {
            scene.each<Transform>([&](Entity e, Transform& pose) {
                if (scene.has<MeshRenderer>(e) || scene.has<Terrain>(e)) {
                    pose.position = (pose.position - center) * (1 / radius);
                    pose.scale = pose.scale * (1 / radius);
                }
            });
        }
        auto light = scene.create();
        Transform lightPose;
        lightPose.rotation = rotationFromEuler({-40, -30, 0});
        scene.add(light, lightPose);
        DirectionalLight key;
        key.shadows = false;
        key.intensity = 1.7f;
        scene.add(light, key);
        slot.camera = {};
        slot.camera.target = {};
        slot.camera.distance = 3.6f;
        slot.camera.pitch = radians(22.f);
        slot.data.width = 384;
        slot.data.height = 288;
        slot.data.flip = true;
    }

    const AssetPreviewData& get(const AssetManager& assets, const std::string& path, int index) {
        static const AssetPreviewData pending;
        if (inventoryRoot_ != assets.root || !signature_) {
            return pending;
        }
        if (iconFrame_ != ImGui::GetFrameCount()) {
            iconFrame_ = ImGui::GetFrameCount();
            iconBudget_ = 1;
            iconDrawBudget_ = 1;
        }
        if (index == 2 && !icons_.count(path)) {
            static const AssetPreviewData unavailable;
            if (iconBudget_ <= 0 || std::chrono::steady_clock::now() < nextIcon_) {
                return unavailable;
            }
            if (icons_.size() >= 256) {
                auto oldest = icons_.end();
                for (auto it = icons_.begin(); it != icons_.end(); ++it) {
                    if (it->second->lastUsed < iconFrame_ - 1 &&
                        (oldest == icons_.end() ||
                         it->second->lastUsed < oldest->second->lastUsed)) {
                        oldest = it;
                    }
                }
                if (oldest == icons_.end()) {
                    return unavailable;
                }
                icons_.erase(oldest);
            }
            icons_[path] = std::make_unique<Slot>();
            --iconBudget_;
            nextIcon_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
        }
        auto& slot =
            index == 2 ? *icons_.at(path) : slots_[index == 3 ? 2 : std::clamp(index, 0, 1)];
        slot.lastUsed = iconFrame_;
        const auto now = std::chrono::steady_clock::now();
        if (root_ != assets.root) {
            root_ = assets.root;
            textures_.clear();
            thumbnails_.clear();
            for (auto& item : icons_) {
                item.second->key.clear();
            }
            for (auto& item : slots_) {
                item.key.clear();
            }
        }
        const auto key = assets.root.generic_string() + "/" + path;
        const bool changed = slot.key != key || slot.revision != modelAssetRevision;
        try {
            bool rebuild = changed || (slot.streaming && textureAsset(path));
            if (slot.waitingBounds && now >= slot.nextBounds) {
                rebuild = true;
                slot.nextBounds = now + std::chrono::milliseconds(200);
            }
            if (changed || now - slot.checked > std::chrono::milliseconds(500)) {
                slot.checked = now;
                const auto stamp =
                    std::filesystem::last_write_time(assetContainerPath(assets.resolve(path)));
                rebuild |= stamp != slot.modified;
                slot.modified = stamp;
            }
            if (rebuild) {
                if (now < nextGeneration_ || readbacks_.size() >= 6 || writes_.size() >= 6) {
                    return slot.data;
                }
                nextGeneration_ = now + std::chrono::milliseconds(100);
                slot.key = key;
                slot.revision = modelAssetRevision;
                slot.saved = false;
                if (changed) {
                    slot.audioJob = {};
                }
                if (slot.cachedTexture) {
                    glDeleteTextures(1, &slot.cachedTexture);
                }
                slot.cachedTexture = 0;
                if (!loadModelPreview(slot, assets, path, index == 2)) {
                    build(slot, assets, path, index == 2);
                }
                if (index == 2) {
                    slot.data.width = slot.data.height = 128;
                }
            }
            if (slot.world && (rebuild || slot.streaming || renderer_.streaming())) {
                if (!rebuild && now < nextGeneration_) {
                    return slot.data;
                }
                nextGeneration_ = now + std::chrono::milliseconds(100);
                if (index == 2) {
                    if (iconDrawBudget_ <= 0) {
                        slot.streaming = true;
                        return slot.data;
                    }
                    --iconDrawBudget_;
                }
                GLint drawFramebuffer = 0, readFramebuffer = 0, viewport[4], scissor[4];
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
                glGetIntegerv(GL_VIEWPORT, viewport);
                glGetIntegerv(GL_SCISSOR_BOX, scissor);
                const bool scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
                auto restore = [&] {
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFramebuffer);
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
                    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
                    glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);
                    if (scissorEnabled) {
                        glEnable(GL_SCISSOR_TEST);
                    } else {
                        glDisable(GL_SCISSOR_TEST);
                    }
                };
                try {
                    renderer_.setAssetRoot(assets.root);
                    slot.surface.resize(slot.data.width, slot.data.height);
                    glBindFramebuffer(GL_FRAMEBUFFER, slot.surface.framebuffer);
                    renderer_.draw(
                        *slot.world,
                        slot.camera.viewProjection(float(slot.data.width) / slot.data.height),
                        slot.data.width,
                        slot.data.height,
                        slot.camera.position(),
                        0,
                        0,
                        false);
                    slot.data.texture = slot.surface.texture;
                    restore();
                } catch (...) {
                    restore();
                    throw;
                }
            }
            slot.streaming = textures_.streaming() || renderer_.streaming();
            if (!slot.streaming && !slot.waitingBounds && !slot.saved) {
                saveModelPreview(slot, assets, path, index == 2);
                slot.saved = true;
            }
        } catch (const std::exception& error) {
            slot.key = key;
            slot.revision = modelAssetRevision;
            slot.world.reset();
            slot.streaming = false;
            slot.data = {};
            slot.data.error = error.what();
            saveModelPreview(slot, assets, path, index == 2);
            slot.saved = true;
        }
        return slot.data;
    }

  public:
    // Called only on focused editor frames with an active GL/ImGui context.
    void service(const AssetManager& assets, bool allowWarm = true) {
        drainWrites();
        drainReadbacks();
        const auto now = std::chrono::steady_clock::now();
        if (inventoryRoot_ != assets.root) {
            inventoryRoot_ = assets.root;
            inventoryJob_ = {};
            signature_ = 0;
            warm_.clear();
            rescan_ = true;
            nextInventory_ = {};
        }
        if (inventoryJob_.valid() &&
            inventoryJob_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto batch = inventoryJob_.get();
                if (signature_ != batch.signature || rescan_) {
                    signature_ = batch.signature;
                    warm_.clear();
                    for (auto& slot : slots_) {
                        slot.key.clear();
                    }
                    for (auto& pair : icons_) {
                        pair.second->key.clear();
                    }
                    for (const auto& file : batch.paths) {
                        // One rendered hover supplies both resolutions; do not render every asset
                        // twice.
                        warm_.push_back({file, false});
                    }
                }
                rescan_ = false;
            } catch (...) { /* Retry transient import writes on the next inventory pass. */
            }
            nextInventory_ = now + std::chrono::seconds(5);
        }
        if (!inventoryJob_.valid() && (rescan_ || now >= nextInventory_)) {
            const auto previous = rescan_ ? 0 : signature_;
            auto job = WorkerPool::shared().submit(
                [assets, previous] { return inventory(assets, previous); });
            if (job) {
                inventoryJob_ = std::move(*job);
            }
        }
        const auto& io = ImGui::GetIO();
        if (ImGui::IsAnyMouseDown() || ImGui::IsAnyItemActive() || io.MouseWheel != 0 ||
            io.MouseDelta.x != 0 || io.MouseDelta.y != 0) {
            nextWarm_ = now + std::chrono::milliseconds(300);
            return;
        }
        if (!allowWarm || !signature_ || warm_.empty() || now < nextWarm_ ||
            readbacks_.size() >= 6 || writes_.size() >= 6) {
            return;
        }
        auto& work = warm_.front();
        if (work.started == std::chrono::steady_clock::time_point{}) {
            work.started = now;
        }
        get(assets, work.path, work.icon ? 2 : 3);
        if (work.icon && !icons_.count(work.path)) {
            return;
        }
        const auto& slot = work.icon ? *icons_.at(work.path) : slots_[2];
        if (slot.saved || now - work.started > std::chrono::seconds(30)) {
            if (slot.saved && !work.icon) {
                saveModelPreview(slots_[2], assets, work.path, true);
            }
            warm_.pop_front();
            nextWarm_ = now + std::chrono::milliseconds(200);
        }
    }
    AssetPreviewRenderer(Renderer& renderer, ProjectIO::Meshes meshes)
        : renderer_(renderer), meshes_(meshes) {
        assetPreviewImported = [this](const AssetManager& assets) {
            if (inventoryRoot_ == assets.root) {
                rescan_ = true;
                nextInventory_ = {};
            }
        };
        assetThumbnail = [this](const AssetManager& assets, const std::string& path) {
            AssetPreviewData image;
            if (textureAsset(path)) {
                if (thumbnailFrame_ != ImGui::GetFrameCount()) {
                    thumbnailFrame_ = ImGui::GetFrameCount();
                    pickerThumbnails_.beginFrame(assets);
                }
                image.texture = pickerThumbnails_.texture(assets, path);
                image.width = image.height = 128;
            } else if (modelAsset(path) || materialAsset(path) || Prefabs::asset(path) ||
                       cubemapAsset(path) || Terrains::asset(path)) {
                const auto& preview = get(assets, path, 2);
                image.texture = preview.texture;
                image.width = preview.width;
                image.height = preview.height;
                image.flip = preview.flip;
            }
            return image;
        };
        assetPreview =
            [this](const auto& assets, const auto& path, int slot) -> const AssetPreviewData& {
            return get(assets, path, slot);
        };
    }
    ~AssetPreviewRenderer() {
        for (auto& item : readbacks_) {
            glDeleteSync(item.fence);
            glDeleteBuffers(1, &item.buffer);
        }
        assetPreview = {};
        assetThumbnail = {};
        assetPreviewImported = {};
    }
};
