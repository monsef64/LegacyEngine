#include "models/model_import.hpp"
#include "rendering/temporal_sampling.hpp"
#include "rendering/vulkan_context.hpp"
#include "rendering/vulkan_scene_renderer.hpp"
#include "rendering/vulkan_scene_surface.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <vector>

struct Unsupported {};
static std::vector<uint32_t> load(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        throw std::runtime_error("shader open failed");
    }
    auto n = f.tellg();
    if (n <= 0 || n % 4) {
        throw std::runtime_error("invalid shader");
    }
    std::vector<uint32_t> out(static_cast<size_t>(n) / 4);
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(out.data()), n)) {
        throw std::runtime_error("shader read failed");
    }
    return out;
}
static void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
static Mat4 matrix(float x = 0, float y = 0) {
    Mat4 m{};
    m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1;
    m.m[12] = x;
    m.m[13] = y;
    return m;
}
int main(int argc, char** argv) {
    if (argc != 7) {
        return 1;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return 77;
    }
    auto* window = SDL_CreateWindow("Motion test", 64, 64, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (!window) {
        SDL_Quit();
        return 77;
    }
    int status = 0;
    try {
        VulkanContext context(window);
        VulkanSceneSurface surface(context.physicalDevice(),
                                   context.device(),
                                   context.graphicsQueue(),
                                   context.graphicsFamily(),
                                   load(argv[1]),
                                   load(argv[2]));
        VkFormatProperties velocityProperties{}, depthProperties{};
        vkGetPhysicalDeviceFormatProperties(
            context.physicalDevice(), VK_FORMAT_R32G32_SFLOAT, &velocityProperties);
        vkGetPhysicalDeviceFormatProperties(
            context.physicalDevice(), VK_FORMAT_D32_SFLOAT, &depthProperties);
        const auto velocityFeatures =
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        const auto depthFeatures =
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
        if ((velocityProperties.optimalTilingFeatures & velocityFeatures) != velocityFeatures ||
            (depthProperties.optimalTilingFeatures & depthFeatures) != depthFeatures) {
            throw Unsupported{};
        }
        surface.enableMotionVectors(load(argv[3]), load(argv[4]));
        surface.enableLighting(load(argv[5]), load(argv[6]));
        auto triangle = [](float x, float z) {
            MeshData m;
            m.vertices = {{{x - .8f, -.8f, z}}, {{x + .8f, -.8f, z}}, {{x, .8f, z}}};
            return m;
        };
        auto base = surface.createMesh(triangle(0, 0));
        auto moved = surface.createMesh(triangle(.2f, 0));
        auto nearer = surface.createMesh(triangle(0, -.5f));
        using Motion = VulkanSceneSurface::MotionDraw;
        auto testPixel = [&](const std::vector<float>& pixels,
                             unsigned w,
                             unsigned x,
                             unsigned y,
                             float a,
                             float b) {
            const size_t i = (size_t(y) * w + x) * 2;
            require(std::abs(pixels[i] - a) < .0001f && std::abs(pixels[i + 1] - b) < .0001f,
                    "unexpected motion vector");
        };
        const Mat4 identity = matrix();
        surface.renderMotionVectors(64, 64, {{base, {}, identity, identity, identity}});
        require(surface.motionImage() && surface.motionImageView() && surface.motionDepthImage() &&
                    surface.motionDepthImageView(),
                "missing motion or depth image");
        auto pixels = surface.readMotionVectors();
        testPixel(pixels, 64, 32, 32, 0, 0);
        testPixel(pixels, 64, 0, 0, 0, 0);
        // Previous UV minus current UV, including the engine-to-Vulkan Y flip.
        const Mat4 current = matrix(.2f, .2f);
        const Mat4 jittered =
            TemporalSampling::jitterViewProjection(current, {.3f, -.2f}, {64, 64});
        surface.renderMotionVectors(64, 64, {{base, {}, jittered, current, identity}});
        pixels = surface.readMotionVectors();
        testPixel(pixels, 64, 32, 32, -.1f, .1f);
        // Previous deformed vertices can differ from the current ones.
        surface.renderMotionVectors(64, 64, {{moved, base, identity, identity, identity}});
        pixels = surface.readMotionVectors();
        testPixel(pixels, 64, 32, 32, -.1f, 0);
        // The nearest opaque triangle wins regardless of submission order.
        const Motion nearDraw{nearer, {}, current, current, identity};
        const Motion farDraw{base, {}, identity, identity, matrix(.5f, 0)};
        surface.renderMotionVectors(64, 64, {nearDraw, farDraw});
        pixels = surface.readMotionVectors();
        testPixel(pixels, 64, 32, 32, -.1f, .1f);
        Mat4 mirror = identity;
        mirror.m[0] = -1;
        surface.renderMotionVectors(64, 64, {{base, {}, mirror, mirror, mirror, true}});
        pixels = surface.readMotionVectors();
        testPixel(pixels, 64, 32, 32, 0, 0);
        surface.renderMotionVectors(32, 24, {});
        pixels = surface.readMotionVectors();
        require(pixels.size() == 32u * 24u * 2u, "resize failed");
        testPixel(pixels, 32, 12, 12, 0, 0);
        // Exercise ECS transform history and camera jitter through the scene
        // bridge, including invalidation when the underlying mesh is replaced.
        World world;
        auto entity = world.create();
        world.add(entity, Transform{});
        world.add(entity, MeshRenderer{base, {1, 1, 1}});
        VulkanSceneRenderer renderer(surface);
        TemporalHistory history;
        TemporalHistory::Configuration config{{64, 64}, {128, 128}, 1, 2, 3};
        require(surface.colorImage() == VK_NULL_HANDLE,
                "unrendered color image must be unavailable");
        auto first = renderer.drawTemporalTo(surface, world, identity, config, history);
        require(first.frame.reset && first.missingObjectHistory, "first temporal frame must reset");
        require(surface.colorImage() != VK_NULL_HANDLE && surface.width() == config.render.width &&
                    surface.height() == config.render.height &&
                    surface.imageLayout() == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                "temporal color image not ready for compute");
        world.get<Transform>(entity).position = {.2f, .2f, 0};
        auto second = renderer.drawTemporalTo(surface, world, identity, config, history);
        require(!second.frame.reset && !second.missingObjectHistory, "temporal history lost");
        pixels = surface.readMotionVectors();
        testPixel(pixels, 64, 32, 32, -.1f, .1f);
        surface.updateMesh(base, triangle(0, 0));
        auto replaced = renderer.drawTemporalTo(surface, world, identity, config, history);
        require(replaced.missingObjectHistory, "replaced geometry must invalidate history");
        pixels = surface.readMotionVectors();
        testPixel(pixels, 64, 32, 32, 0, 0);
        // Camera motion is encoded independently of object motion, and the
        // lit path produces the same vectors for opaque geometry.
        auto cameraMoved =
            renderer.drawTemporalTo(surface, world, matrix(.1f, 0), config, history, {}, true);
        require(!cameraMoved.frame.reset && !cameraMoved.missingObjectHistory,
                "lit temporal frame lost history");
        pixels = surface.readMotionVectors();
        testPixel(pixels, 64, 32, 32, -.05f, 0);
        const auto sky = world.create();
        world.add(sky, Skybox{});
        bool unsupported = false;
        try {
            renderer.drawTemporalTo(surface, world, matrix(.2f, 0), config, history);
        } catch (const std::runtime_error&) {
            unsupported = true;
        }
        require(unsupported, "unsupported temporal sky must be rejected");
        world.destroy(sky);
        auto retry = renderer.drawTemporalTo(surface, world, matrix(.2f, 0), config, history);
        require(retry.frame.index == cameraMoved.frame.index + 1 && !retry.frame.reset,
                "failed temporal draw must not commit history");
        pixels = surface.readMotionVectors();
        testPixel(pixels, 64, 32, 32, -.05f, 0);
        bool consumerFailed = false;
        try {
            renderer.drawTemporalTo(
                surface, world, matrix(.3f, 0), config, history, {}, false, false, [](const auto&) {
                    throw std::runtime_error("dispatch failed");
                });
        } catch (const std::runtime_error&) {
            consumerFailed = true;
        }
        require(consumerFailed, "temporal consumer failure was not propagated");
        auto afterFailedConsumer =
            renderer.drawTemporalTo(surface, world, matrix(.3f, 0), config, history);
        require(afterFailedConsumer.frame.index == retry.frame.index + 1,
                "consumer failure must not commit temporal history");
        pixels = surface.readMotionVectors();
        testPixel(pixels, 64, 32, 32, -.05f, 0);
        // Import a real skinned glTF and check per-view previous vertex poses.
        const auto root =
            std::filesystem::temp_directory_path() /
            ("legacy-temporal-skin-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code ec;
                std::filesystem::remove_all(path, ec);
            }
        } cleanup{root};
        std::filesystem::create_directories(root);
        AssetManager assets;
        assets.root = root;
        const auto model = importModel(
            assets, std::filesystem::path(LEGACY_FIXTURES_DIR) / "animated_triangle.gltf");
        renderer.setAssetRoot(root);
        World animatedWorld;
        auto animatedEntity = animatedWorld.create();
        animatedWorld.add(animatedEntity, Transform{});
        MeshRenderer modelRenderer{};
        modelRenderer.model = model;
        animatedWorld.add(animatedEntity, modelRenderer);
        Animation animation{};
        animation.model = model;
        animation.modelAnimation = 0;
        animation.poseActive = true;
        animatedWorld.add(animatedEntity, animation);
        TemporalHistory viewA, viewB;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        bool loaded = false;
        while (std::chrono::steady_clock::now() < deadline) {
            auto frame = renderer.drawTemporalTo(surface, animatedWorld, identity, config, viewA);
            if (frame.missingObjectHistory) {
                loaded = true;
                break;
            }
            std::this_thread::yield();
        }
        require(loaded, "animated model did not load");
        animatedWorld.get<Animation>(animatedEntity).time = .1f;
        auto posed = renderer.drawTemporalTo(surface, animatedWorld, identity, config, viewA);
        require(!posed.missingObjectHistory, "animated history lost");
        pixels = surface.readMotionVectors();
        testPixel(pixels, 64, 46, 27, -.1f, 0);
        renderer.drawTemporalTo(surface, animatedWorld, identity, config, viewB);
        animatedWorld.get<Animation>(animatedEntity).time = .2f;
        auto otherPose = renderer.drawTemporalTo(surface, animatedWorld, identity, config, viewB);
        require(!otherPose.missingObjectHistory, "second view lost animation history");
        pixels = surface.readMotionVectors();
        testPixel(pixels, 64, 52, 29, -.1f, 0);
        animatedWorld.get<Animation>(animatedEntity).time = .3f;
        posed = renderer.drawTemporalTo(surface, animatedWorld, identity, config, viewA);
        require(!posed.missingObjectHistory, "another view overwrote animation history");
        pixels = surface.readMotionVectors();
        testPixel(pixels, 64, 57, 29, -.2f, 0);
        animatedWorld.get<Animation>(animatedEntity).poseActive = false;
        posed = renderer.drawTemporalTo(surface, animatedWorld, identity, config, viewA);
        require(posed.missingObjectHistory, "switch to rigid pose must reset");
        bool rejected = false;
        try {
            surface.renderMotionVectors(64,
                                        64,
                                        {{base, moved, identity, identity, identity},
                                         {MeshHandle{}, {}, identity, identity, identity}});
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "invalid mesh accepted");
    } catch (const Unsupported&) {
        std::fprintf(stderr, "SKIP: sampled motion/depth formats unavailable\n");
        status = 77;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        status = 1;
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return status;
}
