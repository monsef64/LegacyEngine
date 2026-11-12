// Draws the world with OpenGL 3.3.

#pragma once

#include "ecs.hpp"
#include "components.hpp"
#include "math.hpp"
#include "mesh.hpp"

#include <vector>

class Renderer {
public:
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
    void draw(World& world, const Mat4& viewProjection, int width, int height, Vec3 cameraPosition, int x=0, int y=0, bool editorView=true);

    // Green collider overlay for a selected object, drawn over the scene.
    void drawCollider(const Transform& transform, const Collider& collider, const Mat4& viewProjection);

private:
    MeshHandle cameraModel_;
    unsigned int colliderVertexArray_ = 0;
    unsigned int colliderVertexBuffer_ = 0;
    struct GpuMesh {
        unsigned int vertexArray  = 0;
        unsigned int vertexBuffer = 0;
        int          vertexCount  = 0;
        bool         lines        = false;
    };

    std::vector<GpuMesh> meshes_; // MeshHandle::id is an index into this

    // Flat colour, for lines.
    unsigned int unlitProgram_ = 0;
    int          unlitModelViewProjection_ = -1;
    int          unlitColor_ = -1;

    // Colour shaded by a single directional light, for surfaces.
    unsigned int checkerTexture_ = 0;
    unsigned int litProgram_ = 0;
    int          litModel_ = -1;
    int          litCameraPosition_ = -1;
    int          litCheckerboard_ = -1;
    int          litCheckerTexture_ = -1;
    int          litSpecular_ = -1;
    int          litModelViewProjection_ = -1;
    int          litNormalMatrix_ = -1;
    int          litColor_ = -1;
    int          litLightDirection_ = -1;
};
