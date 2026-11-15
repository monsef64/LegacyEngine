// Host-side layouts for data/shaders/vulkan/. All offsets follow std140 or
// push-constant layout. Shader bindings are documented next to each structure.
#pragma once

#include "core/math.hpp"
#include "rendering/mesh.hpp"
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace VulkanShaderLayout {
struct alignas(16) Float4 {
    float x = 0, y = 0, z = 0, w = 0;
};
struct alignas(16) Int4 {
    std::int32_t x = 0, y = 0, z = 0, w = 0;
};
// Lit vertex: set 0, binding 0.
struct alignas(16) Object {
    Mat4 modelViewProjection{};
    Mat4 model{};
    Mat4 normalMatrix{};
};
// Lit fragment: set 1, binding 4. Bindings 0..3 are material, normal,
// checker and shadow combined-image samplers respectively.
struct alignas(16) Lighting {
    Float4 lightPosition[16]{};
    Float4 lightColor[16]{};
    Float4 lightDirection[16]{};
    Float4 lightShadow[16]{};
    Mat4 shadowMatrix[12]{};
    Int4 cascadeCount[16]{}; // GLSL uses the x component
    Float4 cascadeSplits[16]{};
    Float4 shadowView{};
    Float4 cameraPositionAndSpecular{};
    Float4 colorAndNormalStrength{};
    float tiling[2]{};
    std::int32_t lightCount = 0, softShadows = 0;
    std::int32_t hasMaterialTexture = 0, bumped = 0, transparent = 0, checkerboard = 0;
};
// Skybox fragment: set 0, binding 0; bindings 1 and 2 are cube and panorama.
struct alignas(16) Skybox {
    Mat4 inverseVP{};
    Float4 tintOpacity{};
    std::int32_t hasTexture = 0, isHDR = 0;
    std::int32_t padding[2]{};
};
// Unlit graphics: push constants in both vertex and fragment stages.
struct alignas(16) UnlitDraw {
    Mat4 modelViewProjection{};
    Float4 color{};
};
// Skinning compute: five storage buffers (vertices, ranges, influences,
// alternating position/normal matrices, output vertices), set 0 bindings 0..4.
// Dispatch ceil(count / 64) groups, then synchronize shader writes with the
// vertex input stage before using the output as a vertex buffer.
struct SkinningParameters {
    std::uint32_t count = 0;
    float scale = 1;
};
static_assert(sizeof(Mat4) == 64 && sizeof(Vertex) == 32);
static_assert(std::is_standard_layout<Vertex>::value && offsetof(Vertex, normal) == 12 &&
              offsetof(Vertex, u) == 24);
static_assert(sizeof(Float4) == 16 && sizeof(Int4) == 16);
static_assert(sizeof(Object) == 192 && offsetof(Object, normalMatrix) == 128);
static_assert(offsetof(Lighting, shadowMatrix) == 1024 &&
              offsetof(Lighting, cascadeCount) == 1792 &&
              offsetof(Lighting, cascadeSplits) == 2048 && offsetof(Lighting, shadowView) == 2304 &&
              offsetof(Lighting, tiling) == 2352 && offsetof(Lighting, checkerboard) == 2380 &&
              sizeof(Lighting) == 2384);
static_assert(sizeof(Skybox) == 96 && offsetof(Skybox, hasTexture) == 80);
static_assert(sizeof(UnlitDraw) == 80 && sizeof(SkinningParameters) == 8);
} // namespace VulkanShaderLayout
