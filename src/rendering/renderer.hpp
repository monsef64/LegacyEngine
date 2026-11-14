// Draws the world with OpenGL 3.3.

#pragma once

#include "core/components.hpp"
#include "core/ecs.hpp"
#include "core/math.hpp"
#include "lighting/lighting.hpp"
#include "lighting/shadow_settings.hpp"
#include "materials/material_io.hpp"
#include "rendering/material_textures.hpp"
#include "rendering/mesh.hpp"
#include "rendering/skybox_renderer.hpp"
#include "scene/editor_grid.hpp"

#include <vector>

class Renderer {
  public:
    std::string materialError, modelError;
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

    void updateSkyboxFades(World& world, float dt);
    void drawEditorGrid(const GridSettings& settings, const Mat4& viewProjection);

    // Green collider overlay for a selected object, drawn over the scene.
    void drawCollider(const Transform& transform,
                      const Collider& collider,
                      const Mat4& viewProjection,
                      float opacity = 1.0f);

    void drawCamera(const Transform& transform, const Camera& camera, const Mat4& viewProjection);
    void drawRaycast(const Transform& transform, const Raycast& query, const Mat4& viewProjection);
    void drawAudioSource(const Transform& transform,
                         const AudioSource& source,
                         const Mat4& viewProjection);

  private:
    SkyboxRenderer skyboxRenderer_, skyboxFadeRenderer_;
    MeshHandle editorGrid_;
    int gridCells_ = 0;
    float gridSpacing_ = 0;
    AssetManager materialAssets_;
    MaterialAssetCache materialCache_;
    MaterialTextureCache materialTextures_;
    int litTransparent_ = -1;
    int litTexture_ = -1, litNormalTexture_ = -1, litHasTexture_ = -1, litBumped_ = -1,
        litStrength_ = -1, litTiling_ = -1;
    void drawWire(const MeshData& wire,
                  const Transform& transform,
                  const Mat4& viewProjection,
                  Vec3 color,
                  float opacity = 1.0f);
    int softShadowLocation_ = -1;
    ShadowSettings shadowSettings_;
    int shadowAtlasResolution_ = 0;
    int cascadeCountLocations_[16]{}, cascadeSplitLocations_[16]{}, shadowViewLocation_ = -1;
    int lightCountLocation_ = -1, shadowSamplerLocation_ = -1;
    int lightLocations_[16][4]{}, shadowMatrixLocations_[12]{};
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
    };
    // Reused across Scene/Game views. Entries are valid only during draw();
    // clear keeps capacity, so steady-state frames do not rebuild these arrays.
    std::vector<TransparentDraw> transparentDraws_;
    std::vector<ShadowCaster> shadowCasters_;
    std::vector<Lighting::Light> lights_;

    void clearModelCache();
    unsigned modelRevision_ = 0;
    std::map<std::string, MeshHandle> modelMeshes_;
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
