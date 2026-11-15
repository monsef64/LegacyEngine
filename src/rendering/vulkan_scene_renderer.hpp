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
#include "rendering/vulkan_material_textures.hpp"
#include "rendering/vulkan_model_animation.hpp"
#include "rendering/vulkan_scene_surface.hpp"
#include "rendering/vulkan_skybox_cache.hpp"
#include "scene/skybox_transition.hpp"
#include <algorithm>
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
    void clearModelCache() {
        // Remove entries only after the GPU resource was successfully released.
        while (!models_.empty()) {
            surface_.destroyMesh(models_.begin()->second);
            models_.erase(models_.begin());
        }
        while (!animated_.empty()) {
            surface_.destroyMesh(animated_.begin()->second.mesh);
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

  private:
    void drawImpl(VulkanSceneSurface& target,
                  const World& world,
                  const Mat4& viewProjection,
                  uint32_t width,
                  uint32_t height,
                  Vec3 cameraPosition,
                  bool lit,
                  const std::vector<VulkanSceneSurface::Draw>& overlays,
                  const VulkanSceneSurface::RenderRegion* region) {
        if (!surface_.sharesMeshesWith(target)) {
            throw std::runtime_error("Vulkan scene target does not share renderer meshes");
        }
        if (modelRevision_ != modelAssetRevision) {
            clearModelCache();
            modelRevision_ = modelAssetRevision;
        }
        ++drawFrame_;
        // Remove dead, hidden or retargeted entities before applying the cache
        // limit, so replacing many animated entities in one frame still works.
        for (auto it = animated_.begin(); it != animated_.end();) {
            const Entity entity{std::get<0>(it->first), std::get<1>(it->first)};
            const auto* component =
                world.alive(entity) ? world.tryGet<MeshRenderer>(entity) : nullptr;
            if (!component || component->model != std::get<2>(it->first) ||
                !isActive(world, entity)) {
                surface_.destroyMesh(it->second.mesh);
                it = animated_.erase(it);
            } else {
                ++it;
            }
        }
        std::vector<VulkanSceneSurface::Draw> draws;
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
                if (!renderer.model.empty()) {
                    const auto& doc = cachedModelDocument(assets_, renderer.model, &documents_);
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
                        if (found == animated_.end()) {
                            const auto handle = surface_.createMesh(data);
                            try {
                                found =
                                    animated_.emplace(key, AnimatedMesh{handle, drawFrame_}).first;
                            } catch (...) {
                                surface_.destroyMesh(handle);
                                throw;
                            }
                        } else {
                            surface_.updateMesh(found->second.mesh, data);
                            found->second.seen = drawFrame_;
                        }
                        mesh = found->second.mesh;
                    } else {
                        auto found = models_.find(renderer.model);
                        if (found == models_.end()) {
                            if (models_.size() >= 1024) {
                                throw std::runtime_error("Vulkan model cache limit exceeded");
                            }
                            auto data =
                                loadModelAsset(assets_, renderer.model, 0, -1, 0, &documents_);
                            auto handle = surface_.createMesh(data);
                            try {
                                found = models_.emplace(renderer.model, handle).first;
                            } catch (...) {
                                surface_.destroyMesh(handle);
                                throw;
                            }
                        }
                        mesh = found->second;
                    }
                }
                if (mesh.id == UINT32_MAX) {
                    return;
                }
                const Mat4 model =
                    transformMatrix(transform.position, transform.rotation, transform.scale);
                VulkanSceneSurface::Draw draw{
                    mesh,
                    multiply(viewProjection, model),
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
    }

  private:
    VulkanSceneSurface& surface_;
    VulkanMaterialTextures textures_;
    VulkanSkyboxCache skies_;
    AssetManager assets_;
    MaterialAssetCache materials_;
    ShadowSettings shadowSettings_;
    unsigned modelRevision_ = 0;
    uint64_t drawFrame_ = 0;
    struct AnimatedMesh {
        MeshHandle mesh;
        uint64_t seen;
    };
    std::map<std::tuple<uint32_t, uint32_t, std::string>, AnimatedMesh> animated_;
    std::map<std::string, MeshHandle> models_;
    std::map<std::string, Json> documents_;
};
