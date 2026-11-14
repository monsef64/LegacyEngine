#pragma once
#include "assets/cubemap.hpp"
#include "assets/texture_preview.hpp"
#include "audio/audio_clip.hpp"
#include "editor/asset_preview_ui.hpp"
#include "editor/rotation_editor.hpp"
#include "models/model_asset.hpp"
#include "prefabs/prefab.hpp"
#include "rendering/renderer.hpp"
#include "rendering/scene_surface.hpp"
#include "scene/camera.hpp"
#include <array>
#include <fstream>

// Two independent slots prevent Inspector and tooltip images overwriting one another.
class AssetPreviewRenderer {
    struct Slot {
        SceneSurface surface;
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
    std::array<Slot, 2> slots_;
    MaterialTextureCache textures_;
    TexturePreviewCache thumbnails_;
    std::filesystem::path root_;

    void build(Slot& slot, const AssetManager& assets, const std::string& path) {
        slot.data = {};
        slot.world = std::make_unique<World>();
        auto& world = *slot.world;
        if (textureAsset(path)) {
            if (AssetManager::extension(path) == ".hdr") {
                thumbnails_.beginFrame(assets);
                slot.data.texture = thumbnails_.texture(assets, path);
                slot.data.width = slot.data.height = 128;
                if (!slot.data.texture) {
                    slot.data.error = "HDR preview unavailable";
                }
            } else {
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
                    const auto data = loadModelAsset(assets, mesh.model);
                    for (const auto& vertex : data.vertices) {
                        include(vertex.position);
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
            materialAsset(path) ? .5f : std::max(.000001f, std::sqrt(dot(size, size)) * .5f);
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
        slot.data.flip = true;
    }

    const AssetPreviewData& get(const AssetManager& assets, const std::string& path, int index) {
        auto& slot = slots_[std::clamp(index, 0, 1)];
        const auto now = std::chrono::steady_clock::now();
        if (root_ != assets.root) {
            root_ = assets.root;
            textures_.clear();
            thumbnails_.clear();
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
            }
            if (slot.world && rebuild) {
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
    AssetPreviewRenderer(Renderer& renderer, ProjectIO::Meshes meshes)
        : renderer_(renderer), meshes_(meshes) {
        assetPreview =
            [this](const auto& assets, const auto& path, int slot) -> const AssetPreviewData& {
            return get(assets, path, slot);
        };
    }
    ~AssetPreviewRenderer() {
        assetPreview = {};
    }
};
