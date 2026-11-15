// GPU line geometry for Scene/Game editor overlays. The surface owns the
// underlying Vulkan device and shared mesh store and must outlive this object.
#pragma once
#include "audio/audio_gizmo.hpp"
#include "models/mesh_simplify.hpp"
#include "models/model_asset.hpp"
#include "physics/collider_debug.hpp"
#include "rendering/vulkan_scene_surface.hpp"
#include "scene/camera_gizmo.hpp"
#include "scene/editor_grid.hpp"
#include "scene/raycast_gizmo.hpp"
#include <map>
#include <stdexcept>
#include <vector>

class VulkanEditorOverlays {
  public:
    explicit VulkanEditorOverlays(VulkanSceneSurface& owner) : owner_(owner) {}
    ~VulkanEditorOverlays() {
        try {
            if (grid_.id != UINT32_MAX) {
                owner_.destroyMesh(grid_);
            }
            if (audioGlyph_.id != UINT32_MAX) {
                owner_.destroyMesh(audioGlyph_);
            }
            for (auto mesh : wires_) {
                owner_.destroyMesh(mesh);
            }
        } catch (...) { /* the owner still frees all resources on device loss */
        }
    }
    VulkanEditorOverlays(const VulkanEditorOverlays&) = delete;
    VulkanEditorOverlays& operator=(const VulkanEditorOverlays&) = delete;

    // Begin a view, add its overlays, then render immediately before beginning
    // another view. Geometry handles are reused and updateMesh waits for work;
    // draw lists must not be retained across begin() or object destruction.
    void begin(const Mat4& viewProjection) {
        vp_ = viewProjection;
        draws_.clear();
        used_ = 0;
        gridAdded_ = false;
    }
    const std::vector<VulkanSceneSurface::Draw>& draws() const {
        return draws_;
    }
    void setAssetRoot(const std::filesystem::path& root) {
        if (root != assets_.root) {
            assets_.root = root;
            colliderTriangles_.clear();
        }
    }
    void grid(const GridSettings& settings) {
        if (!settings.visible || !settings.valid()) {
            return;
        }
        if (gridAdded_) {
            throw std::runtime_error("Only one grid per Vulkan overlay view");
        }
        if (grid_.id == UINT32_MAX || cells_ != settings.cells || spacing_ != settings.spacing) {
            auto mesh = makeGrid(settings.cells, settings.spacing);
            if (grid_.id == UINT32_MAX) {
                grid_ = owner_.createMesh(mesh);
            } else {
                owner_.updateMesh(grid_, mesh);
            }
            cells_ = settings.cells;
            spacing_ = settings.spacing;
        }
        Quat rotation{};
        if (settings.plane == 1) {
            rotation = axisAngle({1, 0, 0}, kPi * .5f);
        } else if (settings.plane == 2) {
            rotation = axisAngle({0, 0, 1}, kPi * .5f);
        }
        VulkanSceneSurface::Draw draw{
            grid_,
            multiply(vp_, transformMatrix(settings.position, rotation, {1, 1, 1})),
            {settings.color.x, settings.color.y, settings.color.z, 1}};
        draw.transparent = true;
        draw.castsShadow = false;
        draws_.push_back(draw);
        gridAdded_ = true;
    }
    void collider(const Transform& transform, const Collider& collider, float opacity = 1) {
        const std::vector<Vec3>* triangles = nullptr;
        if (collider.shape == ColliderShape::Mesh) {
            if (collider.model.empty()) {
                return;
            }
            if (modelRevision_ != modelAssetRevision) {
                colliderTriangles_.clear();
                modelRevision_ = modelAssetRevision;
            }
            const auto key = std::make_pair(collider.model, std::clamp(collider.meshDetail, 2, 15));
            auto found = colliderTriangles_.find(key);
            if (found == colliderTriangles_.end()) {
                if (colliderTriangles_.size() >= 4) {
                    colliderTriangles_.clear();
                }
                auto trianglesData =
                    simplifyMeshTriangles(loadModelAsset(assets_, collider.model), key.second);
                found = colliderTriangles_.emplace(key, std::move(trianglesData)).first;
            }
            triangles = &found->second;
        }
        wire(
            colliderWireframe(collider, transform.scale, triangles), transform, {0, 1, 0}, opacity);
    }
    void camera(const Transform& transform, const Camera& camera) {
        wire(cameraFrustumWire(camera), transform, {.92f, .94f, .98f});
    }
    void raycast(const Transform& transform, const Raycast& raycast) {
        wire(raycastWire(raycast), transform, {1, .75f, .2f});
    }
    void audioSource(const Transform& transform, const AudioSource& source) {
        wire(audioSourceRange(source), transform, {.35f, .8f, 1});
    }
    // Add the scene-only speaker glyphs before the grid and selected gizmos.
    // Like the existing renderer, glyphs use depth testing and ignore scale.
    void audioIcons(const World& world) {
        world.eachReadOnly<AudioSource, Transform>(
            [&](Entity, const AudioSource&, const Transform& t) {
                if (audioGlyph_.id == UINT32_MAX) {
                    audioGlyph_ = owner_.createMesh(audioSourceGlyph());
                }
                VulkanSceneSurface::Draw draw{
                    audioGlyph_,
                    multiply(vp_, transformMatrix(t.position, t.rotation, {1, 1, 1})),
                    {.35f, .8f, 1, 1}};
                draw.castsShadow = false;
                draws_.push_back(draw);
            });
    }
    // Geometry is local to the object's position and rotation; scale must be
    // incorporated by the geometry generator (matching the GL debug overlay).
    void wire(const MeshData& geometry, const Transform& transform, Vec3 color, float opacity = 1) {
        if (geometry.vertices.empty()) {
            return;
        }
        if (geometry.topology != MeshData::Topology::Lines || !std::isfinite(opacity) ||
            opacity < 0 || opacity > 1) {
            throw std::runtime_error("Invalid Vulkan editor overlay");
        }
        if (used_ >= 4096) {
            throw std::runtime_error("Too many Vulkan editor overlays");
        }
        if (used_ == wires_.size()) {
            const auto handle = owner_.createMesh(geometry);
            try {
                wires_.push_back(handle);
            } catch (...) {
                owner_.destroyMesh(handle);
                throw;
            }
        } else {
            owner_.updateMesh(wires_[used_], geometry);
        }
        VulkanSceneSurface::Draw draw{
            wires_[used_++],
            multiply(vp_, transformMatrix(transform.position, transform.rotation, {1, 1, 1})),
            {color.x, color.y, color.z, opacity}};
        draw.transparent = true;
        draw.castsShadow = false;
        draw.depthAlways = true;
        draws_.push_back(draw);
    }

  private:
    VulkanSceneSurface& owner_;
    Mat4 vp_{};
    MeshHandle grid_, audioGlyph_;
    int cells_ = 0;
    float spacing_ = 0;
    bool gridAdded_ = false;
    std::vector<MeshHandle> wires_;
    std::size_t used_ = 0;
    std::vector<VulkanSceneSurface::Draw> draws_;
    AssetManager assets_;
    unsigned modelRevision_ = 0;
    std::map<std::pair<std::string, int>, std::vector<Vec3>> colliderTriangles_;
};
