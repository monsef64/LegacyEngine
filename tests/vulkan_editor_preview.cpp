#include "editor/animation_window.hpp"
#include "editor/model_preview_window.hpp"
#include "editor/vulkan_asset_preview_renderer.hpp"
#include "editor/vulkan_preview_backend.hpp"
#include <ImGuizmo.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <fstream>
#include <imgui_internal.h>
#include <stdexcept>
#include <vector>

static void require(bool value, const char* text) {
    if (!value) {
        throw std::runtime_error(text);
    }
}
static std::vector<uint32_t> load(const char* path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        throw std::runtime_error("Shader not found");
    }
    auto size = f.tellg();
    if (size <= 0 || size % 4) {
        throw std::runtime_error("Bad shader");
    }
    std::vector<uint32_t> data(size_t(size) / 4);
    f.seekg(0);
    if (!f.read(reinterpret_cast<char*>(data.data()), size)) {
        throw std::runtime_error("Bad shader read");
    }
    return data;
}
int main(int argc, char** argv) {
    if (argc != 7) {
        return 1;
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return 77;
    }
    auto* window =
        SDL_CreateWindow("Editor preview test", 640, 480, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
    if (!window) {
        SDL_Quit();
        return 77;
    }
    int result = 0;
    try {
        struct Fixture {
            std::filesystem::path root =
                std::filesystem::temp_directory_path() /
                ("legacy-vulkan-editor-preview-" + std::to_string(SDL_GetTicksNS()));
            ~Fixture() {
                std::error_code ec;
                std::filesystem::remove_all(root, ec);
            }
        } fixture;
        std::filesystem::create_directories(fixture.root);
        writeJsonFile(fixture.root / "triangle.lemesh", Json::parse(R"({
            "format":"LegacyEngineMesh", "version":1,
            "vertices":[[-0.8,-0.8,0,0,0,1,0,0],[0.8,-0.8,0,0,0,1,1,0],[0,0.8,0,0,0,1,0.5,1]]
        })"));
        AssetManager assets;
        assets.root = fixture.root;
        VulkanContext context(window);
        VulkanEditorPreviewBackend::Shaders shaders{load(argv[1]),
                                                    load(argv[2]),
                                                    load(argv[3]),
                                                    load(argv[4]),
                                                    load(argv[5]),
                                                    load(argv[6])};
        VulkanSceneSurface owner(context.physicalDevice(),
                                 context.device(),
                                 context.graphicsQueue(),
                                 context.graphicsFamily(),
                                 shaders.unlitVertex,
                                 shaders.unlitFragment);
        VulkanSceneRenderer renderer(owner);
        auto imgui = std::unique_ptr<ImGuiContext, void (*)(ImGuiContext*)>(ImGui::CreateContext(),
                                                                            ImGui::DestroyContext);
        ImGui::GetIO().IniFilename = nullptr;
        VulkanImGui gui(window, context);
        VulkanEditorPreviewBackend previews(owner, renderer, gui, shaders);
        ModelPreviewWindow panel;
        panel.open(assets, "triangle.lemesh", previews);
        require(panel.visible, "model window failed to open");
        World animationWorld;
        auto object = animationWorld.create();
        animationWorld.add(object, Transform{});
        animationWorld.add(object,
                           MeshRenderer{owner.createMesh(makeBox({1, 1, 1})), {1, .2f, .1f}});
        auto lamp = animationWorld.create();
        animationWorld.add(lamp, Transform{});
        DirectionalLight light;
        light.shadows = false;
        animationWorld.add(lamp, light);
        Mat4 vp =
            multiply(perspective(radians(60), 1, .1f, 10), lookAt({0, 0, 3}, {0, 0, 0}, {0, 1, 0}));
        require(gui.beginFrame(), "UI begin failed");
        ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());
        ImGuizmo::BeginFrame();
        panel.draw(assets, previews, 1.f / 60);
        auto* model = previews.surface(EditorPreviewBackend::Slot::Model);
        require(model && model->width() == 720 && model->height() == 405,
                "real model preview panel did not render");
        const auto modelGeneration = model->imageGeneration();
        auto pixels = model->readPixels();
        std::size_t different = 0;
        for (std::size_t i = 4; i < pixels.size(); i += 4) {
            if (pixels[i] != pixels[0] || pixels[i + 1] != pixels[1] ||
                pixels[i + 2] != pixels[2]) {
                ++different;
            }
        }
        require(different > 100, "model preview image is empty");
        ImGui::Begin("Animation preview");
        auto image = previews.image(EditorPreviewBackend::Slot::Animation,
                                    animationWorld,
                                    assets,
                                    vp,
                                    {0, 0, 3},
                                    80,
                                    60,
                                    true);
        require(image.texture != ImTextureID_Invalid && !image.flip,
                "Vulkan preview image unavailable");
        ImGui::Image(image.texture, ImVec2(80, 60));
        ImGui::End();
        require(gui.endFrame() == VulkanContext::FrameResult::Presented,
                "preview frame not presented");
        context.requestResize();
        require(gui.beginFrame(), "UI begin after resize failed");
        panel.draw(assets, previews, 1.f / 60);
        require(model->imageGeneration() == modelGeneration,
                "unchanged preview rendered at a different extent");
        ImGui::Begin("Animation preview");
        auto cached = previews.image(EditorPreviewBackend::Slot::Animation,
                                     animationWorld,
                                     assets,
                                     vp,
                                     {0, 0, 3},
                                     80,
                                     60,
                                     false);
        require(cached.texture != ImTextureID_Invalid,
                "preview descriptor was not restored after swapchain reset");
        ImGui::Image(cached.texture, ImVec2(80, 60));
        ImGui::End();
        gui.endFrame();
        require(model->readPixels() == pixels, "other preview replaced model image");
        saveAnimationClip(assets, "preview.leanim", AnimationClip{});
        AnimationWindow animation;
        animation.open(assets, "preview.leanim");
        EditorHistory history(animationWorld);
        require(gui.beginFrame(), "animation UI begin failed");
        animation.draw(animationWorld, object, assets, previews, history, true, 1.f / 60);
        gui.endFrame();
        auto* animationPanel = ImGui::FindWindowByName("Animation###AnimationEditor");
        require(animationPanel != nullptr, "animation window unavailable");
        animationPanel->StateStorage.SetInt(animationPanel->GetID("Isolated preview"), 1);
        require(gui.beginFrame(), "animation preview UI begin failed");
        animation.draw(animationWorld, object, assets, previews, history, true, 1.f / 60);
        auto* animationSurface = previews.surface(EditorPreviewBackend::Slot::Animation);
        require(animationSurface && animationSurface->width() == 480 &&
                    animationSurface->height() == 270,
                "real animation preview panel did not render");
        gui.endFrame();
        {
            const unsigned char tga[]{0, 0, 2, 0, 0,  0,    0, 0, 0,   0, 0,   0,
                                      2, 0, 1, 0, 24, 0x20, 0, 0, 255, 0, 255, 0};
            std::ofstream out(fixture.root / "sample.tga", std::ios::binary);
            out.write(reinterpret_cast<const char*>(tga), sizeof(tga));
            out.close();
            std::ofstream(fixture.root / "sample.txt") << "A preview";
            ProjectIO::Meshes meshes{owner.createMesh(makeGrid(2, 1)),
                                     owner.createMesh(makePlane(1, 1)),
                                     owner.createMesh(makeBox({1, 1, 1})),
                                     owner.createMesh(makeSphere(.5f)),
                                     owner.createMesh(makeCapsule(.5f, 1)),
                                     owner.createMesh(makeCone(.5f, 1))};
            VulkanAssetPreviewRenderer assetsPreview(
                owner, renderer, gui, context, shaders, meshes);
            assetsPreview.prepare(assets);
            require(gui.beginFrame(), "asset preview UI begin failed");
            const auto& imagePreview = assetPreview(assets, "sample.tga", 0);
            require(imagePreview.texture != ImTextureID_Invalid && imagePreview.width == 2 &&
                        imagePreview.height == 1 && !imagePreview.flip,
                    "full-resolution image asset preview failed");
            const auto& textPreview = assetPreview(assets, "sample.txt", 1);
            require(textPreview.text == "A preview", "text asset preview failed");
            const auto& modelPreview = assetPreview(assets, "triangle.lemesh", 2);
            require(modelPreview.texture != ImTextureID_Invalid && !modelPreview.flip,
                    "model asset icon failed");
            gui.endFrame();
            context.requestResize();
            assetsPreview.prepare(assets);
            require(gui.beginFrame(), "asset preview UI begin after resize failed");
            require(assetPreview(assets, "sample.tga", 0).texture != ImTextureID_Invalid,
                    "image preview descriptor did not recover after resize");
            require(assetPreview(assets, "triangle.lemesh", 2).texture != ImTextureID_Invalid,
                    "model icon descriptor did not recover after resize");
            gui.endFrame();
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Vulkan editor preview: %s\n", e.what());
        result = 1;
    }
    SDL_DestroyWindow(window);
    SDL_Quit();
    return result;
}
