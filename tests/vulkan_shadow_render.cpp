// Vulkan shadow rendering and sampling integration test with GPU readback.
#include "lighting/cascades.hpp"
#include "lighting/lighting.hpp"
#include "rendering/vulkan_context.hpp"
#include "rendering/vulkan_scene_renderer.hpp"
#include <SDL3/SDL.h>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <vector>

static void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
static std::vector<uint32_t> load(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Cannot open shader");
    }
    const auto length = file.tellg();
    if (length <= 0 || length % 4) {
        throw std::runtime_error("Invalid SPIR-V");
    }
    std::vector<uint32_t> data(static_cast<size_t>(length) / 4);
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(data.data()), length)) {
        throw std::runtime_error("Cannot read shader");
    }
    return data;
}
int main(int argc, char** argv) {
    if (argc != 6) {
        return 1;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return 77;
    }
    auto* window =
        SDL_CreateWindow("Vulkan shadow test", 64, 64, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (!window) {
        SDL_Quit();
        return 77;
    }
    int result = 0;
    try {
        VulkanContext context(window);
        VulkanSceneSurface surface(context.physicalDevice(),
                                   context.device(),
                                   context.graphicsQueue(),
                                   context.graphicsFamily(),
                                   load(argv[1]),
                                   load(argv[2]));
        surface.enableLighting(load(argv[3]), load(argv[4]), load(argv[5]));
        require(surface.supportsShadows(), "shadow pipeline not created");
        MeshData receiver, occluder;
        receiver.vertices = {
            {{-.9f, -.9f, 0}, {0, 0, 1}}, {{.9f, -.9f, 0}, {0, 0, 1}}, {{0, .9f, 0}, {0, 0, 1}}};
        occluder.vertices = {{{-.55f, -.55f, 0}, {0, 0, 1}},
                             {{.55f, -.55f, 0}, {0, 0, 1}},
                             {{0, .55f, 0}, {0, 0, 1}}};
        auto receiverMesh = surface.createMesh(receiver),
             occluderMesh = surface.createMesh(occluder);
        Mat4 identity{};
        identity.m[0] = identity.m[5] = identity.m[10] = identity.m[15] = 1;
        VulkanSceneSurface::Draw ground{receiverMesh, identity, {1, 1, 1, 1}};
        ground.lit = true;
        ground.model = ground.normalMatrix = identity;
        VulkanSceneSurface::Draw caster{occluderMesh, identity, {1, 1, 1, 1}};
        caster.lit = true;
        caster.model = caster.normalMatrix = identity;
        caster.model.m[14] = .5f;
        caster.viewProjectionModel.m[12] =
            3; // offscreen to the camera, still casts in the light view
        VulkanShaderLayout::Lighting light{};
        light.lightCount = 1;
        light.lightPosition[0] = {0, 0, 0, 0};
        light.lightDirection[0] = {0, 0, -1, 0};
        light.lightColor[0] = {.8f, .8f, .8f, 10};
        light.lightShadow[0] = {0, .001f, 0, 1};
        light.cascadeCount[0].x = 1;
        light.cameraPositionAndSpecular = {0, 0, 2, 0};
        VulkanSceneSurface::ShadowPlan shadows{};
        shadows.atlasResolution = 64;
        shadows.soft = false;
        Mat4 lightVP = identity;
        lightVP.m[10] = -1;
        shadows.layers[0] = {lightVP, 64};
        auto center = [&]() {
            auto pixels = surface.readPixels();
            return pixels[(32 * 64 + 32) * 4];
        };
        auto render = [&](const VulkanSceneSurface::ShadowPlan* plan) {
            surface.render(64, 64, {ground, caster}, {0, 0, 0, 1}, &light, plan);
            return center();
        };
        const auto unshadowed = render(nullptr);
        require(unshadowed > 220, "direct light without shadows failed");
        auto shadowed = render(&shadows);
        require(shadowed >= 27 && shadowed <= 36, "directional shadow missing");
        caster.castsShadow = false;
        require(render(&shadows) > 220, "disabled caster still casts a shadow");
        caster.castsShadow = true;
        caster.transparent = true;
        require(render(&shadows) > 220, "transparent caster should not cast an opaque shadow");
        caster.transparent = false;
        shadows.soft = true;
        shadows.atlasResolution = 128;
        shadows.layers[0].resolution = 64;
        light.lightShadow[0].w = .5f;
        require(render(&shadows) <= 40, "resized soft shadow atlas failed");
        shadows.layers[0].resolution = 128;
        light.lightShadow[0].w = 1;
        require(render(&shadows) <= 40, "shadow layer quality change failed");
        auto bad = shadows;
        bad.layers[0].resolution = 0;
        bool rejected = false;
        try {
            render(&bad);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "invalid shadow plan was accepted");
        require(render(&shadows) <= 40, "invalid shadow plan poisoned rendering");

        // Test all six point-light layers, including selecting -Z at the receiver.
        Lighting::Light point{};
        point.type = 1;
        point.position = {0, 0, 2};
        point.settings.range = 10;
        shadows = {};
        shadows.atlasResolution = 64;
        shadows.soft = false;
        for (int face = 0; face < 6; ++face) {
            shadows.layers[face] = {Lighting::shadowMatrix(point, {}, face, 64), 64};
        }
        light.lightPosition[0] = {0, 0, 2, 1};
        light.lightColor[0] = {.8f, .8f, .8f, 10};
        light.lightShadow[0] = {0, .001f, 0, 1};
        auto pointLit = render(nullptr);
        auto pointShadow = render(&shadows);
        require(pointLit > 100 && pointShadow < pointLit / 2, "point-light shadow failed");
        // Spot uses a single layer and a perspective cone.
        Lighting::Light spot = point;
        spot.type = 2;
        spot.direction = {0, 0, -1};
        spot.settings.outerAngle = 45;
        shadows = {};
        shadows.atlasResolution = 64;
        shadows.soft = false;
        shadows.layers[0] = {Lighting::shadowMatrix(spot, {}, 0, 64), 64};
        light.lightPosition[0].w = 2;
        light.lightDirection[0] = {0, 0, -1, .7f};
        light.lightShadow[0].z = .9f;
        auto spotLit = render(nullptr);
        auto spotShadow = render(&shadows);
        require(spotLit > 100 && spotShadow < spotLit / 2, "spot-light shadow failed");

        // Cascades use the shared engine camera/split builder.
        Mat4 vp = multiply(perspective(60 * kPi / 180, 1, .1f, 10.f),
                           lookAt({0, 0, 2}, {0, 0, 0}, {0, 1, 0}));
        ground.viewProjectionModel = vp;
        caster.viewProjectionModel = multiply(vp, transformMatrix({3, 0, .5f}, {}, {1, 1, 1}));
        const auto cascades = Lighting::buildCascades(vp, {0, 0, 2}, {0, 0, -1}, 10, 64);
        shadows = {};
        shadows.atlasResolution = 64;
        shadows.soft = false;
        for (int i = 0; i < 4; ++i) {
            shadows.layers[i] = {cascades.matrices[i], 64};
        }
        light.lightPosition[0] = {0, 0, 0, 0};
        light.lightDirection[0] = {0, 0, -1, 0};
        light.lightShadow[0] = {0, .001f, 0, 1};
        light.cascadeCount[0].x = 4;
        light.cascadeSplits[0] = {
            cascades.splits[0], cascades.splits[1], cascades.splits[2], cascades.splits[3]};
        light.shadowView = {cascades.forward.x,
                            cascades.forward.y,
                            cascades.forward.z,
                            -dot(cascades.origin, cascades.forward)};
        auto cascadeLit = render(nullptr);
        auto cascadeShadow = render(&shadows);
        require(cascadeLit > 220 && cascadeShadow < 90, "cascaded shadow failed");

        // Exercise the ECS bridge's light gathering, atlas planning and caster
        // construction, not just the lower-level shadow surface API.
        World world;
        const auto floor = world.create();
        world.add(floor, Transform{});
        world.add(floor, MeshRenderer{receiverMesh});
        const auto block = world.create();
        world.add(block, Transform{{-.5f, 0, .5f}});
        world.add(block, MeshRenderer{occluderMesh});
        const auto lamp = world.create();
        Transform lampTransform;
        lampTransform.rotation = axisAngle({0, 1, 0}, -45 * kPi / 180);
        world.add(lamp, lampTransform);
        DirectionalLight sun;
        sun.shadowDistance = 4;
        sun.shadows = false;
        world.add(lamp, sun);
        VulkanSceneRenderer renderer(surface);
        ShadowSettings settings;
        settings.mode = 1;
        settings.resolution = 256;
        settings.soft = false;
        renderer.setShadowSettings(settings);
        renderer.drawLitTo(surface, world, identity, 64, 64, {0, 0, 2});
        const auto ecsLit = center();
        world.get<DirectionalLight>(lamp).shadows = true;
        renderer.drawLitTo(surface, world, identity, 64, 64, {0, 0, 2});
        const auto ecsShadow = center();
        if (!(ecsLit > 175 && ecsShadow < ecsLit / 2)) {
            throw std::runtime_error("ECS shadow failed: lit=" + std::to_string(ecsLit) +
                                     " shadow=" + std::to_string(ecsShadow) +
                                     " error=" + renderer.shadowError);
        }
        settings.mode = 0;
        renderer.setShadowSettings(settings);
        world.get<DirectionalLight>(lamp).shadows = false;
        renderer.drawLitTo(surface, world, vp, 64, 64, {0, 0, 2});
        const auto ecsCascadeLit = center();
        world.get<DirectionalLight>(lamp).shadows = true;
        renderer.drawLitTo(surface, world, vp, 64, 64, {0, 0, 2});
        const auto ecsCascadeShadow = center();
        if (!(ecsCascadeLit > 175 && ecsCascadeShadow < ecsCascadeLit / 2)) {
            throw std::runtime_error("ECS cascades failed: lit=" + std::to_string(ecsCascadeLit) +
                                     " shadow=" + std::to_string(ecsCascadeShadow) +
                                     " error=" + renderer.shadowError);
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Vulkan shadow render test: %s\n", e.what());
        result = 1;
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
