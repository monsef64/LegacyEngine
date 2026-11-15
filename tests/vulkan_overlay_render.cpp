#include "rendering/vulkan_camera_region.hpp"
#include "rendering/vulkan_context.hpp"
#include "rendering/vulkan_editor_overlays.hpp"
#include "rendering/vulkan_scene_renderer.hpp"
#include <SDL3/SDL.h>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <vector>

static std::vector<uint32_t> load(const char* path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Cannot open shader");
    }
    auto size = file.tellg();
    if (size <= 0 || size % 4) {
        throw std::runtime_error("Invalid shader");
    }
    std::vector<uint32_t> words(static_cast<size_t>(size) / 4);
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(words.data()), size)) {
        throw std::runtime_error("Cannot read shader");
    }
    return words;
}
static void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
int main(int argc, char** argv) {
    if (argc != 5) {
        return 1;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return 77;
    }
    auto* window =
        SDL_CreateWindow("Vulkan overlays", 64, 64, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (!window) {
        SDL_Quit();
        return 77;
    }
    int result = 0;
    try {
        VulkanContext context(window);
        auto vert = load(argv[1]), frag = load(argv[2]);
        VulkanSceneSurface surface(context.physicalDevice(),
                                   context.device(),
                                   context.graphicsQueue(),
                                   context.graphicsFamily(),
                                   vert,
                                   frag);
        VulkanSceneSurface second(context.physicalDevice(),
                                  context.device(),
                                  context.graphicsQueue(),
                                  context.graphicsFamily(),
                                  vert,
                                  frag,
                                  &surface);
        surface.enableLighting(load(argv[3]), load(argv[4]));
        VulkanSceneRenderer renderer(surface);
        VulkanEditorOverlays overlay(surface);
        Mat4 identity{};
        identity.m[0] = identity.m[5] = identity.m[10] = identity.m[15] = 1;
        MeshData triangle;
        triangle.vertices = {{{-.8f, -.8f, 0}}, {{.8f, -.8f, 0}}, {{0, .8f, 0}}};
        auto mesh = surface.createMesh(triangle);
        MeshData line;
        line.topology = MeshData::Topology::Lines;
        line.vertices = {{{-.8f, 0, .8f}}, {{.8f, 0, .8f}}};
        overlay.begin(identity);
        overlay.wire(line, Transform{}, {0, 1, 0}, .5f);
        auto through = overlay.draws().at(0);
        require(through.depthAlways && through.transparent && !through.castsShadow,
                "overlay flags incorrect");
        auto gridlike = through;
        gridlike.depthAlways = false;
        using Draw = VulkanSceneSurface::Draw;
        Draw blue{mesh, identity, {0, 0, 1, 1}};
        auto pixels = [&]() { return surface.readPixels(); };
        auto count = [&](const std::vector<uint8_t>& p, int color) {
            int n = 0;
            for (int y = 30; y <= 33; ++y) {
                if (p[(y * 64 + 32) * 4 + color] > 20) {
                    ++n;
                }
            }
            return n;
        };
        surface.render(64, 64, {blue, gridlike}, {0, 0, 0, 1});
        require(count(pixels(), 1) == 0, "depth-tested grid appeared through geometry");
        surface.render(64, 64, {blue, through}, {0, 0, 0, 1});
        auto p = pixels();
        require(count(p, 1) > 0, "depth-independent overlay hidden behind geometry");
        bool blend = false;
        for (int y = 30; y <= 33; ++y) {
            auto i = (y * 64 + 32) * 4;
            blend |= p[i + 1] >= 126 && p[i + 1] <= 129 && p[i + 2] >= 126 && p[i + 2] <= 129;
        }
        require(blend, "overlay opacity not blended");
        bool rejected = false;
        auto invalid = blue;
        invalid.depthAlways = true;
        invalid.transparent = true;
        try {
            surface.render(64, 64, {invalid});
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "depth-independent triangle accepted");
        World world;
        auto e = world.create();
        world.add(e, Transform{});
        world.add(e, MeshRenderer{mesh, {0, 0, 1}});
        renderer.drawTo(surface, world, identity, 64, 64, overlay.draws());
        require(count(pixels(), 1) > 0, "ECS overlay not drawn after scene");
        Camera viewportCamera;
        viewportCamera.viewportX = .125f;
        viewportCamera.viewportY = .125f;
        viewportCamera.viewportWidth = .5f;
        viewportCamera.viewportHeight = .5f;
        auto region = vulkanCameraRegion(viewportCamera, 64, 64);
        require(region.x == 8 && region.y == 8 && region.width == 32 && region.height == 32,
                "camera viewport normalization incorrect");
        renderer.drawTo(surface, world, identity, 64, 64, {}, &region);
        auto regionPixels = pixels();
        require(regionPixels[(24 * 64 + 24) * 4 + 2] > 250 &&
                    regionPixels[(48 * 64 + 48) * 4 + 2] < 30,
                "ECS viewport region not applied");
        renderer.drawLitTo(surface, world, identity, 64, 64, {0, 0, 2}, overlay.draws());
        require(count(pixels(), 1) > 0, "lit ECS overlay not drawn after scene");
        // Reuse the same dynamic buffer across views and ensure stale geometry
        // does not contaminate the other surface's completed image.
        auto first = overlay.draws().at(0).mesh.id;
        overlay.begin(identity);
        line.vertices[0].position.y = .5f;
        line.vertices[1].position.y = .5f;
        overlay.wire(line, Transform{}, {1, 0, 0});
        require(overlay.draws().at(0).mesh.id == first, "dynamic overlay mesh not reused");
        renderer.drawTo(second, world, identity, 64, 64, overlay.draws());
        require(count(surface.readPixels(), 1) > 0, "second viewport changed first viewport image");
        GridSettings grid;
        grid.cells = 4;
        grid.plane = 1;
        grid.spacing = .25f;
        overlay.begin(identity);
        overlay.grid(grid);
        require(overlay.draws().size() == 1 && !overlay.draws()[0].depthAlways &&
                    !overlay.draws()[0].castsShadow,
                "grid flags incorrect");
        auto gridId = overlay.draws()[0].mesh.id;
        overlay.begin(identity);
        grid.cells = 6;
        overlay.grid(grid);
        require(overlay.draws()[0].mesh.id == gridId, "grid mesh not reused");
        rejected = false;
        try {
            overlay.grid(grid);
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "multiple grids silently reused the same handle");
        overlay.begin(identity);
        Collider collider;
        overlay.collider(Transform{}, collider, .25f);
        Camera camera;
        overlay.camera(Transform{}, camera);
        Raycast ray;
        overlay.raycast(Transform{}, ray);
        AudioSource audio;
        overlay.audioSource(Transform{}, audio);
        require(overlay.draws().size() == 4, "editor gizmo generation failed");
        auto speaker = world.create();
        world.add(speaker, Transform{});
        world.add(speaker, AudioSource{});
        overlay.audioIcons(world);
        require(overlay.draws().size() == 5 && !overlay.draws().back().depthAlways &&
                    !overlay.draws().back().castsShadow,
                "scene audio icon generation failed");
        for (std::size_t i = 0; i < 4; ++i) {
            const auto& draw = overlay.draws()[i];
            require(draw.depthAlways && !draw.castsShadow, "gizmo flags incorrect");
        }
        renderer.drawTo(surface, world, identity, 64, 64, overlay.draws());
        overlay.begin(identity);
        line.topology = MeshData::Topology::Triangles;
        rejected = false;
        try {
            overlay.wire(line, Transform{}, {1, 1, 1});
        } catch (const std::runtime_error&) {
            rejected = true;
        }
        require(rejected, "triangle overlay accepted");
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Vulkan overlay test: %s\n", e.what());
        result = 1;
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
