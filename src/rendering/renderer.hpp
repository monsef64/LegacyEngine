// Draws the world with OpenGL 3.3.

#pragma once

#include "core/components.hpp"
#include "core/ecs.hpp"
#include "core/math.hpp"
#include "effects/laser_renderer.hpp"
#include "effects/light_shafts_renderer.hpp"
#include "lighting/cookie_atlas.hpp"
#include "lighting/lighting.hpp"
#include "lighting/lightmap_state.hpp"
#include "lighting/shadow_settings.hpp"
#include "materials/material_io.hpp"
#include "particles/particle_renderer.hpp"
#include "rendering/editor_lines.hpp"
#include "rendering/gpu_skinning.hpp"
#include "rendering/material_textures.hpp"
#include "rendering/mesh.hpp"
#include "rendering/skybox_renderer.hpp"
#include "scene/editor_grid.hpp"
#include "terrain/terrain_renderer.hpp"

#include <future>
#include <map>
#include <vector>

class Renderer {
  public:
    std::string materialError, modelError;
    bool streaming() const {
        return !pendingModels_.empty() || materialTextures_.streaming();
    }
    void setShadowSettings(ShadowSettings settings) {
        if (settings.valid()) {
            shadowSettings_ = settings;
        }
    }
    const std::string& skyboxError() const {
        return skyboxFadeRenderer_.error.empty() ? skyboxRenderer_.error
                                                 : skyboxFadeRenderer_.error;
    }
    void setAssetRoot(const std::filesystem::path& root) {
        if (materialAssets_.root != root) {
            clearModelCache();
            materialCache_.clear();
            materialTextures_.clear();
            skyboxRenderer_.clear();
            skyboxFadeRenderer_.clear();
            materialAssets_.root = root;
            materialError.clear();
        }
    }
    // Shared static GPU geometry/bounds are reused by thumbnails and editors.
    // Opening a second viewer must not parse and skin every vertex again.
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
        const auto& doc = cachedModelDocument(materialAssets_, path, &modelDocuments_);
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
        auto found = modelMeshes_.find(path);
        if (found == modelMeshes_.end()) {
            found = modelMeshes_
                        .emplace(path,
                                 createMesh(loadModelAsset(
                                     materialAssets_, path, 0, -1, 0, &modelDocuments_)))
                        .first;
        }
        return meshBounds(found->second, low, high);
    }
    // Call once, after the window's OpenGL context has been created. Returns
    // false, with the reason logged, if OpenGL cannot be set up.
    bool init();

    // Frees what init() and createMesh() created. Call it if init() succeeded,
    // before the OpenGL context is destroyed.
    void shutdown();

    // Copies a mesh to the graphics card. Put the returned handle in the
    // MeshRenderer of every entity that should be drawn with it.
    MeshHandle createMesh(const MeshData& data);

    // Clears the picture, which is `width` x `height` pixels, and draws every
    // entity that has a Transform and a MeshRenderer.
    void draw(World& world,
              const Mat4& viewProjection,
              int width,
              int height,
              Vec3 cameraPosition,
              int x = 0,
              int y = 0,
              bool editorView = true);

    bool meshBounds(MeshHandle handle, Vec3& low, Vec3& high) const {
        if (handle.id >= meshes_.size() || !meshes_[handle.id].vertexCount) {
            return false;
        }
        low = meshes_[handle.id].low;
        high = meshes_[handle.id].high;
        return true;
    }
    void updateSkyboxFades(World& world, float dt);
    void drawEditorGrid(const GridSettings& settings, const Mat4& viewProjection);

    // Green collider overlay for a selected object, drawn over the scene.
    void drawCollider(const Transform& transform,
                      const Collider& collider,
                      const Mat4& viewProjection,
                      float opacity = 1.0f);

    void drawCamera(const Transform& transform, const Camera& camera, const Mat4& viewProjection);
    void drawRaycast(const Transform& transform,
                     const Raycast& query,
                     const Mat4& viewProjection,
                     float thickness = 2.f);
    void drawAudioSource(const Transform& transform,
                         const AudioSource& source,
                         const Mat4& viewProjection);

  private:
    SkyboxRenderer skyboxRenderer_, skyboxFadeRenderer_;
    MeshHandle editorGrid_;
    int gridCells_ = 0;
    float gridSpacing_ = 0;
    AssetManager materialAssets_;
    std::map<std::pair<std::string, int>, std::vector<Vec3>> colliderTriangles_;
    MaterialAssetCache materialCache_;
    MaterialTextureCache materialTextures_;
    ParticleRenderer particles_;
    LaserRenderer lasers_;
    LightShaftsRenderer shafts_;
    CookieAtlas cookies_;
    int cookieSampler_ = -1, cookieRight_[16]{}, cookieUp_[16]{};
    Lightmapping::ValidationCache lightmapValidation_;
    int litReflective_ = -1, environmentCube_ = -1, environmentPanorama_ = -1,
        environmentType_ = -1, environmentColor_ = -1;
    Terrains::Renderer terrainRenderer_;
    EditorLines editorLines_;
    int litTransparent_ = -1;
    int litTexture_ = -1, litNormalTexture_ = -1, litHasTexture_ = -1, litBumped_ = -1,
        litStrength_ = -1, litTiling_ = -1;
    void drawWire(const MeshData& wire,
                  const Transform& transform,
                  const Mat4& viewProjection,
                  Vec3 color,
                  float opacity = 1.0f,
                  float thickness = 1.f);
    int softShadowLocation_ = -1;
    int bakedSoft_ = -1, dynamicShadow_[16]{};
    ShadowSettings shadowSettings_;
    int shadowAtlasResolution_ = 0;
    int cascadeCountLocations_[16]{}, cascadeSplitLocations_[16]{}, shadowViewLocation_ = -1;
    int lightCountLocation_ = -1, shadowSamplerLocation_ = -1;
    int lightLocations_[16][4]{}, shadowMatrixLocations_[12]{};
    int indirectLocation_ = -1, hasIndirectLocation_ = -1;
    int lightmapLocation_ = -1, lightmapGridLocation_ = -1, lightBakedLocations_[16]{};
    unsigned int shadowTexture_ = 0, shadowFramebuffer_ = 0;
    void prepareLighting(World& world, const Mat4& viewProjection, Vec3 cameraPosition);
    MeshHandle audioModel_;
    unsigned int colliderVertexArray_ = 0;
    unsigned int colliderVertexBuffer_ = 0;
    struct GpuMesh {
        unsigned int vertexArray = 0;
        unsigned int vertexBuffer = 0;
        int vertexCount = 0;
        float radius = 0;
        Vec3 low{}, high{};
        bool lines = false;
    };

    struct TransparentDraw {
        Transform transform;
        const MeshRenderer* renderer;
        MaterialProperties material;
        float distance;
        std::size_t order;
    };
    struct ShadowCaster {
        const GpuMesh* mesh;
        Mat4 model;
        Vec3 position;
        float radius;
        bool mirrored;
        bool dynamic;
    };
    // Reused across Scene/Game views. Entries are valid only during draw();
    // clear keeps capacity, so steady-state frames do not rebuild these arrays.
    std::vector<TransparentDraw> transparentDraws_;
    std::vector<ShadowCaster> shadowCasters_;
    std::vector<Lighting::Light> lights_;

    struct StreamedModel {
        MeshData mesh;
        std::map<std::string, Json> documents;
    };
    std::map<std::string, std::future<StreamedModel>> pendingModels_;
    std::map<std::string, std::chrono::steady_clock::time_point> failedModels_;
    void pumpModels();
    bool requestModel(const std::string& path);
    MeshHandle cloneModelMesh(MeshHandle source);
    void clearModelCache();
    unsigned modelRevision_ = 0;
    std::map<std::string, MeshHandle> modelMeshes_;
    std::map<std::string, Json> modelDocuments_;
    GpuSkinning gpuSkinning_;
    std::vector<GpuMesh> meshes_; // MeshHandle::id is an index into this

    // Flat colour, for lines.
    unsigned int unlitProgram_ = 0;
    int unlitModelViewProjection_ = -1;
    int unlitColor_ = -1;

    // Bounded dynamic directional/point/spot lighting and shadows for surfaces.
    unsigned int checkerTexture_ = 0;
    unsigned int litProgram_ = 0;
    int litModel_ = -1;
    int litCameraPosition_ = -1;
    int litCheckerboard_ = -1;
    int litCheckerTexture_ = -1;
    int litSpecular_ = -1;
    int litModelViewProjection_ = -1;
    int litNormalMatrix_ = -1;
    int litColor_ = -1;
};
