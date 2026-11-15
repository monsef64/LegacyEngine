#pragma once
#include "assets/cubemap.hpp"
#include "assets/texture_thumbnail.hpp"
#include "assets/vulkan_thumbnail_cache.hpp"
#include "audio/audio_clip.hpp"
#include "editor/asset_preview_ui.hpp"
#include "editor/rotation_editor.hpp"
#include "editor/vulkan_preview_backend.hpp"
#include "models/model_asset.hpp"
#include "prefabs/prefab.hpp"
#include "scene/camera.hpp"
#include <array>
#include <fstream>
#include <stb_image.h>

// Two independent slots prevent Inspector and tooltip images overwriting one another.
class VulkanAssetPreviewRenderer {
    struct Slot {
        std::unique_ptr<VulkanSceneSurface> surface;
        std::unique_ptr<VulkanTexture> image;
        int lastUsed = 0;
        AssetPreviewData data;
        std::unique_ptr<World> world;
        OrbitCamera camera;
        std::string key;
        unsigned revision = 0;
        std::filesystem::file_time_type modified{};
        std::chrono::steady_clock::time_point checked{};
    };
    VulkanSceneSurface& owner_;
    VulkanSceneRenderer& renderer_;
    VulkanImGui& gui_;
    VulkanEditorPreviewBackend::Shaders shaders_;
    ProjectIO::Meshes meshes_;
    std::array<Slot, 2> slots_;
    std::map<std::string, std::unique_ptr<Slot>> icons_;
    VulkanThumbnailCache thumbnails_;
    std::vector<std::unique_ptr<Slot>> retiredSlots_;
    std::vector<std::unique_ptr<VulkanTexture>> retiredImages_;
    std::filesystem::path root_;
    int iconFrame_ = -1, iconBudget_ = 4;

    void build(Slot& slot, const AssetManager& assets, const std::string& path) {
        slot.data = {};
        slot.world = std::make_unique<World>();
        auto& world = *slot.world;
        if (textureAsset(path)) {
            if (slot.image) {
                retiredImages_.push_back(std::move(slot.image));
            }
            if (AssetManager::extension(path) == ".hdr") {
                slot.data.width = slot.data.height = 128;
            } else {
                const auto file = assets.resolve(path);
                const auto bytes = std::filesystem::file_size(file);
                if (bytes == 0 || bytes > 32 * 1024 * 1024) {
                    throw std::runtime_error("Preview image exceeds size limit");
                }
                std::ifstream stream(file, std::ios::binary);
                std::vector<unsigned char> encoded(static_cast<std::size_t>(bytes));
                if (!stream.read(reinterpret_cast<char*>(encoded.data()), encoded.size())) {
                    throw std::runtime_error("Cannot read preview image");
                }
                int w = 0, h = 0, components = 0;
                if (!stbi_info_from_memory(encoded.data(), int(bytes), &w, &h, &components) ||
                    w <= 0 || h <= 0 || w > 4096 || h > 4096) {
                    throw std::runtime_error("Preview image requires at most 4096 x 4096 pixels");
                }
                std::unique_ptr<unsigned char, decltype(&stbi_image_free)> pixels(
                    stbi_load_from_memory(encoded.data(), int(bytes), &w, &h, &components, 4),
                    stbi_image_free);
                if (!pixels) {
                    throw std::runtime_error("Cannot decode preview image");
                }
                const std::vector<uint8_t> rgba(pixels.get(),
                                                pixels.get() + std::size_t(w) * h * 4);
                slot.image = std::make_unique<VulkanTexture>(owner_.physicalDevice(),
                                                             owner_.device(),
                                                             owner_.graphicsQueue(),
                                                             owner_.graphicsFamily(),
                                                             uint32_t(w),
                                                             uint32_t(h),
                                                             rgba);
                slot.data.width = w;
                slot.data.height = h;
            }
            slot.world.reset();
            return;
        }
        if (audioAsset(path)) {
            const auto samples = decodeAudioClip(assets, path);
            slot.data.seconds = float(samples.size()) / 96000;
            slot.data.waveform.resize(256);
            for (std::size_t i = 0; i < samples.size(); ++i) {
                slot.data.waveform[std::min<std::size_t>(
                    255, i * 256 / std::max<std::size_t>(1, samples.size()))] =
                    std::max(slot.data.waveform[std::min<std::size_t>(
                                 255, i * 256 / std::max<std::size_t>(1, samples.size()))],
                             std::abs(samples[i]));
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
        if (materialAsset(path)) {
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
                    if (renderer_.modelBounds(assets, mesh.model, a, b)) {
                        for (int i = 0; i < 8; ++i) {
                            include({i & 1 ? b.x : a.x, i & 2 ? b.y : a.y, i & 4 ? b.z : a.z});
                        }
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
        const auto center = (low + high) * .5f;
        const auto size = high - low;
        const float radius =
            materialAsset(path) ? .5f : std::max(1e-20f, std::sqrt(dot(size, size)) * .5f);
        if (any) {
            scene.each<Transform, MeshRenderer>([&](Entity, Transform& pose, MeshRenderer&) {
                pose.position = (pose.position - center) * (1 / radius);
                pose.scale = pose.scale * (1 / radius);
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
        slot.data.flip = false;
    }

    const AssetPreviewData& get(const AssetManager& assets, const std::string& path, int index) {
        if (iconFrame_ != ImGui::GetFrameCount()) {
            iconFrame_ = ImGui::GetFrameCount();
            iconBudget_ = 4;
        }
        if (index == 2 && !icons_.count(path)) {
            static const AssetPreviewData unavailable;
            if (iconBudget_ <= 0) {
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
                retiredSlots_.push_back(std::move(oldest->second));
                icons_.erase(oldest);
            }
            icons_[path] = std::make_unique<Slot>();
            --iconBudget_;
        }
        auto& slot = index == 2 ? *icons_.at(path) : slots_[std::clamp(index, 0, 1)];
        slot.lastUsed = iconFrame_;
        const auto now = std::chrono::steady_clock::now();
        if (root_ != assets.root) {
            root_ = assets.root;
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
            bool rebuild = changed;
            if (changed || now - slot.checked > std::chrono::milliseconds(500)) {
                slot.checked = now;
                const auto stamp = std::filesystem::last_write_time(assets.resolve(path));
                rebuild |= stamp != slot.modified;
                slot.modified = stamp;
            }
            if (rebuild) {
                slot.key = key;
                slot.revision = modelAssetRevision;
                build(slot, assets, path);
                if (index == 2) {
                    slot.data.width = slot.data.height = 64;
                }
            }
            if (slot.world) {
                if (!slot.surface) {
                    slot.surface = std::make_unique<VulkanSceneSurface>(owner_.physicalDevice(),
                                                                        owner_.device(),
                                                                        owner_.graphicsQueue(),
                                                                        owner_.graphicsFamily(),
                                                                        shaders_.unlitVertex,
                                                                        shaders_.unlitFragment,
                                                                        &owner_);
                    slot.surface->enableLighting(shaders_.litVertex, shaders_.litFragment);
                    slot.surface->enableSkybox(shaders_.skyVertex, shaders_.skyFragment);
                    rebuild = true;
                }
                if (rebuild) {
                    renderer_.setAssetRoot(assets.root);
                    renderer_.drawLitTo(
                        *slot.surface,
                        *slot.world,
                        slot.camera.viewProjection(float(slot.data.width) / slot.data.height),
                        slot.data.width,
                        slot.data.height,
                        slot.camera.position());
                }
                slot.data.texture = gui_.texture(*slot.surface);
            } else if (textureAsset(path)) {
                if (slot.image) {
                    slot.data.texture = gui_.texture(*slot.image);
                } else if (AssetManager::extension(path) == ".hdr") {
                    slot.data.texture = thumbnails_.texture(assets, path);
                }
            }
        } catch (const std::exception& error) {
            slot.key = key;
            slot.revision = modelAssetRevision;
            slot.world.reset();
            slot.data = {};
            slot.data.error = error.what();
        }
        return slot.data;
    }

  public:
    VulkanAssetPreviewRenderer(VulkanSceneSurface& owner,
                               VulkanSceneRenderer& renderer,
                               VulkanImGui& gui,
                               VulkanContext& context,
                               VulkanEditorPreviewBackend::Shaders shaders,
                               ProjectIO::Meshes meshes)
        : owner_(owner), renderer_(renderer), gui_(gui), shaders_(std::move(shaders)),
          meshes_(meshes), thumbnails_(context, gui) {
        if (assetPreview) {
            throw std::runtime_error("Asset preview provider already installed");
        }
        assetPreview =
            [this](const auto& assets, const auto& path, int slot) -> const AssetPreviewData& {
            return get(assets, path, slot);
        };
    }
    // Call before starting the ImGui frame so descriptors are retired safely.
    void prepare(const AssetManager& assets) {
        if (gui_.frameOpen()) {
            throw std::runtime_error("Prepare asset previews before the UI frame");
        }
        for (auto& slot : retiredSlots_) {
            forget(*slot);
        }
        retiredSlots_.clear();
        for (auto& image : retiredImages_) {
            gui_.forget(*image);
        }
        retiredImages_.clear();
        thumbnails_.beginFrame(assets);
    }
    ~VulkanAssetPreviewRenderer() {
        assetPreview = {};
        if (gui_.frameOpen()) {
            gui_.cancelFrame();
        }
        try {
            for (auto& slot : slots_) {
                forget(slot);
            }
            for (auto& item : icons_) {
                forget(*item.second);
            }
            for (auto& slot : retiredSlots_) {
                forget(*slot);
            }
            for (auto& image : retiredImages_) {
                gui_.forget(*image);
            }
        } catch (...) {
        }
    }

  private:
    void forget(Slot& slot) {
        if (slot.surface) {
            gui_.forget(*slot.surface);
        }
        if (slot.image) {
            gui_.forget(*slot.image);
        }
    }
};
