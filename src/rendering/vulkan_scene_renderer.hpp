// ECS-to-Vulkan scene bridge. Lit materials, shadows, CPU-skinned animation
// and separately supplied editor overlays are supported.
#pragma once

#include "core/components.hpp"
#include "core/profiler.hpp"
#include "lighting/cascades.hpp"
#include "lighting/lighting.hpp"
#include "lighting/shadow_settings.hpp"
#include "materials/material_io.hpp"
#include "models/model_asset.hpp"
#include "rendering/temporal_history.hpp"
#include "rendering/vulkan_material_textures.hpp"
#include "rendering/vulkan_model_animation.hpp"
#include "rendering/vulkan_scene_surface.hpp"
#include "rendering/vulkan_skybox_cache.hpp"
#include "scene/skybox_transition.hpp"
#include <algorithm>
#include <functional>
#include <map>
#include <stdexcept>
#include <tuple>
#include <vector>

class VulkanSceneRenderer {
  public:
    // The surface (and, for shared surfaces, its mesh owner) must outlive this renderer.
    std::string materialError, shadowError, skyboxError;
    void setShadowSettings(ShadowSettings settings) {
        if (settings.valid()) {
            shadowSettings_ = settings;
        }
    }
    explicit VulkanSceneRenderer(VulkanSceneSurface& surface)
        : surface_(surface), textures_(surface), skies_(surface) {}
    ~VulkanSceneRenderer() {
        try {
            clearModelCache();
        } catch (...) { /* device lost: surface owns remaining buffers */
        }
    }
    VulkanSceneRenderer(const VulkanSceneRenderer&) = delete;
    VulkanSceneRenderer& operator=(const VulkanSceneRenderer&) = delete;
    MeshHandle createMesh(const MeshData& data) {
        return surface_.createMesh(data);
    }
    bool meshBounds(MeshHandle mesh, Vec3& low, Vec3& high) const {
        return surface_.meshBounds(mesh, low, high);
    }
    // Bind-pose model bounds for editor selection/preview framing. Reuses the
    // same document and mesh caches as scene rendering.
    bool modelBounds(const AssetManager& assets,
                     const std::string& path,
                     Vec3& low,
                     Vec3& high,
                     unsigned depth = 0) {
        if (depth > 2) {
            throw std::runtime_error("Cyclic model reference");
        }
        setAssetRoot(assets.root);
        if (modelRevision_ != modelAssetRevision) {
            clearModelCache();
            modelRevision_ = modelAssetRevision;
        }
        const auto& doc = cachedModelDocument(assets_, path, &documents_);
        if (doc.at("version") == 2) {
            bool any = false;
            const auto& parts = doc.at("parts");
            if (!parts.is_array() || parts.empty() || parts.size() > 1024) {
                throw std::runtime_error("Invalid model parts");
            }
            for (const auto& part : parts) {
                Vec3 a{}, b{};
                if (!modelBounds(assets, part.at("mesh").get<std::string>(), a, b, depth + 1)) {
                    continue;
                }
                if (!any) {
                    low = a;
                    high = b;
                    any = true;
                } else {
                    low = {std::min(low.x, a.x), std::min(low.y, a.y), std::min(low.z, a.z)};
                    high = {std::max(high.x, b.x), std::max(high.y, b.y), std::max(high.z, b.z)};
                }
            }
            return any;
        }
        auto found = models_.find(path);
        if (found == models_.end()) {
            if (models_.size() >= 1024) {
                throw std::runtime_error("Vulkan model cache limit exceeded");
            }
            auto data = loadModelAsset(assets_, path, 0, -1, 0, &documents_);
            auto handle = surface_.createMesh(data);
            try {
                found = models_.emplace(path, handle).first;
            } catch (...) {
                surface_.destroyMesh(handle);
                throw;
            }
        }
        return meshBounds(found->second, low, high);
    }
    void setAssetRoot(const std::filesystem::path& root) {
        if (assets_.root != root) {
            clearModelCache();
            materials_.clear();
            textures_.clear();
            skies_.clear();
            assets_.root = root;
        }
    }
    // Call outside a render while the project world is updating. Missing or
    // invalid destination assets cancel the fade, matching the editor path.
    void updateSkyboxFades(World& world, float dt) {
        skyboxError.clear();
        world.each<SkyboxTransition>([&](Entity entity, const SkyboxTransition& fade) {
            try {
                skies_.get(1, assets_, fade.to);
            } catch (const std::exception& e) {
                skyboxError = e.what();
                world.remove<SkyboxTransition>(entity);
            }
        });
        updateSkyboxTransitions(world, dt);
    }
    // Pending resources render using an empty mesh / default texture until ready.
    // Preview surfaces can use this to redraw after background work completes.
    bool pendingStreaming() const {
        return !pendingModels_.empty() || deferredModels_ || textures_.pending();
    }
    void clearModelCache() {
        modelWorker_.cancelQueued();
        pendingModels_.clear();
        modelFailures_.clear();
        deferredModels_ = false;
        // Remove entries only after the GPU resource was successfully released.
        while (!models_.empty()) {
            surface_.destroyMesh(models_.begin()->second);
            models_.erase(models_.begin());
        }
        while (!animated_.empty()) {
            surface_.destroyMesh(animated_.begin()->second.mesh);
            if (animated_.begin()->second.previous.id != UINT32_MAX) {
                surface_.destroyMesh(animated_.begin()->second.previous);
            }
            animated_.erase(animated_.begin());
        }
        documents_.clear();
    }

    // Models use the engine asset revision and skeleton evaluator. Animated
    // geometry is currently CPU-skinned and uploaded to Vulkan buffers. The flat
    // path rejects materials; the lit path uses shadow maps when the target has
    // been initialized with shadow-capable shaders. Callers synchronize access
    // to the device and queue.
    void draw(const World& world, const Mat4& viewProjection, uint32_t width, uint32_t height) {
        drawTo(surface_, world, viewProjection, width, height);
    }
    // Render Scene, Game and camera previews with the same mesh/model cache.
    // The target must use the mesh owner associated with this renderer.
    void drawTo(VulkanSceneSurface& target,
                const World& world,
                const Mat4& viewProjection,
                uint32_t width,
                uint32_t height,
                const std::vector<VulkanSceneSurface::Draw>& overlays = {},
                const VulkanSceneSurface::RenderRegion* region = nullptr) {
        drawImpl(target, world, viewProjection, width, height, {}, false, overlays, region);
    }
    // Target must have enableLighting() called with the appropriate shaders.
    void drawLitTo(VulkanSceneSurface& target,
                   const World& world,
                   const Mat4& viewProjection,
                   uint32_t width,
                   uint32_t height,
                   Vec3 cameraPosition,
                   const std::vector<VulkanSceneSurface::Draw>& overlays = {},
                   const VulkanSceneSurface::RenderRegion* region = nullptr) {
        drawImpl(
            target, world, viewProjection, width, height, cameraPosition, true, overlays, region);
    }

    // Experimental input generation for a temporal upscaler, not an FSR
    // dispatch. The caller must enable the target's motion pipeline and keep
    // one history per view. At present only opaque triangles
    // and full viewports are supported. Missing object history must reset the
    // upscaler until a reactive/disocclusion mask is implemented. If supplied,
    // consume executes after input generation but before history commits. It
    // must throw on dispatch/submission failure so the previous frame remains
    // intact. Its GPU work must finish before the target is reused or resized.
    struct TemporalDrawResult {
        TemporalHistory::Frame frame;
        bool missingObjectHistory = false;
    };
    TemporalDrawResult
    drawTemporalTo(VulkanSceneSurface& target,
                   const World& world,
                   const Mat4& viewProjection,
                   const TemporalHistory::Configuration& config,
                   TemporalHistory& history,
                   Vec3 cameraPosition = {},
                   bool lit = false,
                   bool forceReset = false,
                   const std::function<void(const TemporalDrawResult&)>& consume = {}) {
        TemporalDrawResult result{history.begin(config, viewProjection, forceReset), false};
        try {
            TemporalPass temporal{history, result.frame, result.missingObjectHistory};
            drawImpl(target,
                     world,
                     viewProjection,
                     config.render.width,
                     config.render.height,
                     cameraPosition,
                     lit,
                     {},
                     nullptr,
                     &temporal);
            if (consume) {
                consume(result);
            }
            history.commit();
            return result;
        } catch (...) {
            history.discard();
            throw;
        }
    }

  private:
    struct TemporalPass {
        TemporalHistory& history;
        TemporalHistory::Frame frame;
        bool& missingObjectHistory;
    };
    void drawImpl(VulkanSceneSurface& target,
                  const World& world,
                  const Mat4& viewProjection,
                  uint32_t width,
                  uint32_t height,
                  Vec3 cameraPosition,
                  bool lit,
                  const std::vector<VulkanSceneSurface::Draw>& overlays,
                  const VulkanSceneSurface::RenderRegion* region,
                  TemporalPass* temporal = nullptr) {
        if (!surface_.sharesMeshesWith(target)) {
            throw std::runtime_error("Vulkan scene target does not share renderer meshes");
        }
        if (modelRevision_ != modelAssetRevision) {
            clearModelCache();
            modelRevision_ = modelAssetRevision;
        }
        ++drawFrame_;
        deferredModels_ = false;
        const auto now = std::chrono::steady_clock::now();
        for (auto it = pendingModels_.begin(); it != pendingModels_.end();) {
            if (now - it->second.used > std::chrono::seconds(2)) {
                it = pendingModels_.erase(it);
            } else {
                ++it;
            }
        }
        // Remove dead, hidden or retargeted entities before applying the cache
        // limit, so replacing many animated entities in one frame still works.
        for (auto it = animated_.begin(); it != animated_.end();) {
            const Entity entity{std::get<0>(it->first), std::get<1>(it->first)};
            const auto* component =
                world.alive(entity) ? world.tryGet<MeshRenderer>(entity) : nullptr;
            if (!component || component->model != std::get<2>(it->first) ||
                !isActive(world, entity)) {
                surface_.destroyMesh(it->second.mesh);
                if (it->second.previous.id != UINT32_MAX) {
                    surface_.destroyMesh(it->second.previous);
                }
                it = animated_.erase(it);
            } else {
                ++it;
            }
        }
        std::vector<VulkanSceneSurface::Draw> draws;
        std::vector<VulkanSceneSurface::MotionDraw> motionDraws;
        const Mat4 rasterViewProjection =
            temporal ? TemporalSampling::jitterViewProjection(
                           viewProjection, temporal->frame.jitter, {width, height})
                     : viewProjection;
        std::vector<VulkanSceneSurface::SkyDraw> skyDraws;
        {
            ProfileScope skyProfile(ProfilePart::Skybox);
            const Skybox* activeSky = nullptr;
            const SkyboxTransition* fade = nullptr;
            world.eachReadOnly<Skybox>([&](Entity entity, const Skybox& sky) {
                if (!activeSky && sky.enabled && isActive(world, entity)) {
                    activeSky = &sky;
                    fade = world.tryGet<SkyboxTransition>(entity);
                }
            });
            skyboxError.clear();
            if (activeSky) {
                if (temporal) {
                    throw std::runtime_error("Temporal sky motion is not implemented");
                }
                if (!target.supportsSkybox()) {
                    throw std::runtime_error("Vulkan skybox pipeline is not initialized");
                }
                auto addSky = [&](const Skybox& sky, unsigned slot, float opacity) {
                    if (!validSkybox(sky)) {
                        skyboxError = "Invalid Skybox properties";
                        return;
                    }
                    const VulkanTexture* image = nullptr;
                    try {
                        image = skies_.get(slot, assets_, sky);
                    } catch (const std::exception& e) {
                        skyboxError = e.what();
                    }
                    skyDraws.push_back({viewProjection,
                                        {sky.color.x, sky.color.y, sky.color.z, opacity},
                                        image,
                                        sky.type == 1});
                };
                addSky(fade ? fade->from : *activeSky, 0, 1);
                if (fade) {
                    addSky(fade->to, 1, std::clamp(fade->weight(), 0.f, 1.f));
                }
            }
        }
        struct SortedDraw {
            VulkanSceneSurface::Draw draw;
            float distance;
        };
        std::vector<SortedDraw> transparent;
        VulkanShaderLayout::Lighting lighting{};
        VulkanSceneSurface::ShadowPlan shadowPlan{};
        bool hasShadows = false;
        if (lit) {
            ProfileScope lightProfile(ProfilePart::Lights);
            textures_.beginFrame();
            shadowError.clear();
            shadowPlan.atlasResolution = uint32_t(shadowSettings_.resolution);
            shadowPlan.soft = shadowSettings_.soft;
            std::vector<Lighting::Light> lights;
            Lighting::gather(world,
                             viewProjection,
                             cameraPosition,
                             lights,
                             target.supportsShadows() && shadowSettings_.mode == 0);
            lighting.lightCount = static_cast<int32_t>(lights.size());
            lighting.cameraPositionAndSpecular = {
                cameraPosition.x, cameraPosition.y, cameraPosition.z, 0};
            for (std::size_t i = 0; i < lights.size(); ++i) {
                const auto& l = lights[i];
                const auto& s = l.settings;
                lighting.lightPosition[i] = {
                    l.position.x, l.position.y, l.position.z, float(l.type)};
                lighting.lightColor[i] = {s.color.x * s.intensity,
                                          s.color.y * s.intensity,
                                          s.color.z * s.intensity,
                                          s.range};
                lighting.lightDirection[i] = {l.direction.x,
                                              l.direction.y,
                                              l.direction.z,
                                              std::cos(s.outerAngle * kPi / 180)};
                int shadowLayer = target.supportsShadows() ? l.shadowLayer : -1;
                const int resolution = shadowSettings_.lightResolution(s.shadowQuality);
                if (shadowLayer >= 0) {
                    try {
                        Lighting::Cascades cascades{};
                        if (l.type == 0 && l.shadowCount == 4) {
                            cascades = Lighting::buildCascades(viewProjection,
                                                               cameraPosition,
                                                               l.direction,
                                                               s.shadowDistance * 2,
                                                               resolution);
                            lighting.cascadeSplits[i] = {cascades.splits[0],
                                                         cascades.splits[1],
                                                         cascades.splits[2],
                                                         cascades.splits[3]};
                            lighting.shadowView = {cascades.forward.x,
                                                   cascades.forward.y,
                                                   cascades.forward.z,
                                                   -dot(cascades.origin, cascades.forward)};
                        }
                        for (int face = 0; face < l.shadowCount; ++face) {
                            const auto matrix =
                                l.type == 0 && l.shadowCount == 4
                                    ? cascades.matrices[face]
                                    : Lighting::shadowMatrix(l, cameraPosition, face, resolution);
                            shadowPlan.layers[shadowLayer + face] = {matrix, uint32_t(resolution)};
                        }
                        lighting.cascadeCount[i].x = l.shadowCount == 4 ? 4 : 1;
                        hasShadows = true;
                    } catch (const std::exception& e) {
                        shadowError = e.what();
                        shadowLayer = -1;
                    }
                }
                lighting.lightShadow[i] = {float(shadowLayer),
                                           s.shadowBias,
                                           std::cos(s.innerAngle * kPi / 180),
                                           float(resolution) / shadowPlan.atlasResolution};
            }
        }
        {
            ProfileScope surfacesProfile(ProfilePart::Surfaces);
            world.eachReadOnly<Transform, MeshRenderer>([&](Entity entity,
                                                            const Transform& transform,
                                                            const MeshRenderer& renderer) {
                if (!isActive(world, entity) || world.has<Camera>(entity)) {
                    return;
                }
                if (!lit && (!renderer.material.empty() || renderer.materialOverrides ||
                             renderer.checkerboard || renderer.specular != 0)) {
                    throw std::runtime_error("Vulkan flat renderer does not yet support materials");
                }
                MeshHandle mesh = renderer.mesh;
                AnimatedMesh* animatedMesh = nullptr;
                std::optional<MeshData> posedMesh;
                uint64_t stableGeometryRevision = 0;
                if (!renderer.model.empty()) {
                    if (!prepareModel(renderer.model)) {
                        return;
                    }
                    const auto& doc = documents_.at(renderer.model);
                    const Animation* animation = nullptr;
                    Entity current = entity;
                    for (std::size_t depth = 0; depth < 4096 && world.alive(current); ++depth) {
                        if (const auto* candidate = world.tryGet<Animation>(current);
                            candidate && candidate->poseActive) {
                            animation = candidate;
                            break;
                        }
                        const auto* parent = world.tryGet<Parent>(current);
                        if (!parent) {
                            break;
                        }
                        current = parent->entity;
                    }
                    const bool animated =
                        animation && (renderer.model == animation->model ||
                                      doc.value("settings", std::string{}) == animation->model);
                    if (animated) {
                        const auto key =
                            std::make_tuple(entity.index, entity.generation, renderer.model);
                        auto found = animated_.find(key);
                        if (found == animated_.end() && animated_.size() >= 1024) {
                            throw std::runtime_error("Vulkan animated model cache limit exceeded");
                        }
                        auto data = loadVulkanAnimatedModel(
                            assets_, renderer.model, *animation, documents_);
                        if (temporal) {
                            stableGeometryRevision =
                                surface_.meshRevision(models_.at(renderer.model));
                        }
                        if (found == animated_.end()) {
                            const auto handle = surface_.createMesh(data);
                            try {
                                found = animated_.emplace(key, AnimatedMesh{handle, {}, drawFrame_})
                                            .first;
                            } catch (...) {
                                surface_.destroyMesh(handle);
                                throw;
                            }
                        } else {
                            surface_.updateMesh(found->second.mesh, data);
                            found->second.seen = drawFrame_;
                        }
                        mesh = found->second.mesh;
                        if (temporal) {
                            animatedMesh = &found->second;
                            posedMesh = std::move(data);
                        }
                    } else {
                        mesh = models_.at(renderer.model);
                    }
                }
                if (mesh.id == UINT32_MAX) {
                    return;
                }
                const Mat4 model =
                    transformMatrix(transform.position, transform.rotation, transform.scale);
                VulkanSceneSurface::Draw draw{
                    mesh,
                    multiply(rasterViewProjection, model),
                    {renderer.color.x, renderer.color.y, renderer.color.z, 1},
                    transform.scale.x * transform.scale.y * transform.scale.z < 0,
                    false};
                if (lit) {
                    MaterialProperties material;
                    material.color = renderer.color;
                    material.specular = renderer.specular;
                    if (!renderer.material.empty()) {
                        try {
                            material = materials_.get(assets_, renderer.material);
                        } catch (const std::exception& e) {
                            materialError = e.what();
                        }
                    }
                    material = applyMaterialOverrides(
                        material, renderer.materialValues, renderer.materialOverrides);
                    if (!validMaterial(material)) {
                        materialError = "Invalid material overrides";
                        material = {};
                    }
                    draw.transparent = material.shader == 2;
                    if (!surface_.meshIsLines(mesh)) {
                        draw.lit = true;
                        draw.model = model;
                        const Mat3 normals = normalMatrix(transform.rotation, transform.scale);
                        for (int col = 0; col < 3; ++col) {
                            for (int row = 0; row < 3; ++row) {
                                draw.normalMatrix.m[col * 4 + row] = normals.m[col * 3 + row];
                            }
                        }
                        draw.normalMatrix.m[15] = 1;
                        draw.color = {material.color.x, material.color.y, material.color.z, 1};
                        draw.specular = material.specular;
                        draw.normalStrength = material.normalStrength;
                        draw.tilingX = material.tilingX;
                        draw.tilingY = material.tilingY;
                        draw.checkerboard = renderer.checkerboard;
                        try {
                            draw.materialTexture = textures_.get(assets_, material.texture);
                        } catch (const std::exception& e) {
                            materialError = material.texture + ": " + e.what();
                        }
                        if (material.shader == 1) {
                            try {
                                draw.normalTexture = textures_.get(assets_, material.normalMap);
                            } catch (const std::exception& e) {
                                materialError = material.normalMap + ": " + e.what();
                            }
                        }
                    }
                }
                if (temporal) {
                    if (draw.transparent || surface_.meshIsLines(mesh)) {
                        throw std::runtime_error(
                            "Temporal transparency and lines are not implemented");
                    }
                    const uint64_t key = (uint64_t(entity.generation) << 32) | entity.index;
                    TemporalHistory::Object object;
                    MeshHandle previousMesh;
                    if (posedMesh) {
                        const auto deformed = temporal->history.deformedObject(
                            key, stableGeometryRevision, model, *posedMesh);
                        object = deformed.transform;
                        if (deformed.previousMesh) {
                            if (animatedMesh->previous.id == UINT32_MAX) {
                                animatedMesh->previous =
                                    surface_.createMesh(*deformed.previousMesh);
                            } else {
                                surface_.updateMesh(animatedMesh->previous, *deformed.previousMesh);
                            }
                            previousMesh = animatedMesh->previous;
                        }
                    } else {
                        object = temporal->history.object(key, surface_.meshRevision(mesh), model);
                    }
                    temporal->missingObjectHistory |= !object.hasHistory;
                    motionDraws.push_back({mesh,
                                           previousMesh,
                                           draw.viewProjectionModel,
                                           object.currentViewProjectionModel,
                                           object.previousViewProjectionModel,
                                           draw.mirrored});
                }
                if (draw.transparent) {
                    const Vec3 delta = transform.position - cameraPosition;
                    transparent.push_back({draw, dot(delta, delta)});
                } else {
                    draws.push_back(draw);
                }
            });
            for (auto it = animated_.begin(); it != animated_.end();) {
                if (it->second.seen != drawFrame_) {
                    surface_.destroyMesh(it->second.mesh);
                    if (it->second.previous.id != UINT32_MAX) {
                        surface_.destroyMesh(it->second.previous);
                    }
                    it = animated_.erase(it);
                } else {
                    ++it;
                }
            }
            std::stable_sort(
                transparent.begin(),
                transparent.end(),
                [](const SortedDraw& a, const SortedDraw& b) { return a.distance > b.distance; });
            for (const auto& item : transparent) {
                draws.push_back(item.draw);
            }
            // Debug geometry is drawn last, and never contributes to shadow maps.
            for (const auto& overlay : overlays) {
                if (overlay.lit || overlay.castsShadow ||
                    (overlay.depthAlways && !overlay.transparent) ||
                    !surface_.meshIsLines(overlay.mesh)) {
                    throw std::runtime_error("Invalid Vulkan editor overlay draw");
                }
                draws.push_back(overlay);
            }
        }
        target.render(width,
                      height,
                      draws,
                      {0.07f, 0.08f, 0.10f, 1.0f},
                      lit ? &lighting : nullptr,
                      hasShadows ? &shadowPlan : nullptr,
                      skyDraws,
                      region);
        if (temporal) {
            target.renderMotionVectors(width, height, motionDraws);
        }
    }

  private:
    struct StreamedModel {
        MeshData mesh;
        std::map<std::string, Json> documents;
    };
    struct PendingModel {
        std::future<StreamedModel> future;
        std::chrono::steady_clock::time_point used;
    };
    struct ModelFailure {
        std::string message;
        std::chrono::steady_clock::time_point retry;
    };
    bool prepareModel(const std::string& path) {
        if (models_.count(path) && documents_.count(path)) {
            pendingModels_.erase(path);
            return true;
        }
        const auto now = std::chrono::steady_clock::now();
        auto failure = modelFailures_.find(path);
        if (failure != modelFailures_.end()) {
            if (now < failure->second.retry) {
                throw std::runtime_error(failure->second.message);
            }
            modelFailures_.erase(failure);
        }
        auto pending = pendingModels_.find(path);
        if (pending == pendingModels_.end()) {
            if (pendingModels_.size() >= 2) {
                deferredModels_ = true;
                return false;
            }
            const auto assets = assets_;
            auto future = modelWorker_.submit([assets, path] {
                StreamedModel result;
                result.mesh = loadModelAsset(assets, path, 0, -1, 0, &result.documents);
                return result;
            });
            if (future) {
                pendingModels_.emplace(path, PendingModel{std::move(*future), now});
            } else {
                deferredModels_ = true;
            }
            return false;
        }
        pending->second.used = now;
        if (pending->second.future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            return false;
        }
        StreamedModel loaded;
        try {
            loaded = pending->second.future.get();
        } catch (const std::exception& error) {
            std::string message = error.what();
            pendingModels_.erase(pending);
            if (modelFailures_.size() >= 128) {
                modelFailures_.erase(modelFailures_.begin());
            }
            modelFailures_[path] = {message, now + std::chrono::seconds(1)};
            throw std::runtime_error(message);
        }
        pendingModels_.erase(pending);
        if (!models_.count(path)) {
            if (models_.size() >= 1024) {
                throw std::runtime_error("Vulkan model cache limit exceeded");
            }
            const auto handle = surface_.createMesh(loaded.mesh);
            try {
                models_.emplace(path, handle);
            } catch (...) {
                surface_.destroyMesh(handle);
                throw;
            }
        }
        documents_.merge(loaded.documents);
        return true;
    }
    VulkanSceneSurface& surface_;
    VulkanMaterialTextures textures_;
    VulkanSkyboxCache skies_;
    AssetManager assets_;
    MaterialAssetCache materials_;
    ShadowSettings shadowSettings_;
    unsigned modelRevision_ = 0;
    uint64_t drawFrame_ = 0;
    struct AnimatedMesh {
        MeshHandle mesh, previous;
        uint64_t seen;
    };
    std::map<std::tuple<uint32_t, uint32_t, std::string>, AnimatedMesh> animated_;
    std::map<std::string, MeshHandle> models_;
    std::map<std::string, Json> documents_;
    std::map<std::string, PendingModel> pendingModels_;
    std::map<std::string, ModelFailure> modelFailures_;
    bool deferredModels_ = false;
    BackgroundWorker modelWorker_;
};
