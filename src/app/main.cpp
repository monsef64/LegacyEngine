#include <cctype>
// LegacyEngine - Scene editing and runtime orchestration.
//
// World conventions:
//   * 1 unit = 1 metre
//   * +Y is up, the ground is the XZ plane (y = 0)
//   * right-handed coordinates

#include "animation/animation_system.hpp"
#include "assets/vulkan_browser_thumbnails.hpp"
#include "audio/audio_system.hpp"
#include "core/components.hpp"
#include "core/ecs.hpp"
#include "editor/animation_window.hpp"
#include "editor/asset_browser.hpp"
#include "editor/create_menu.hpp"
#include "editor/dock_workspace.hpp"
#include "editor/editor_history.hpp"
#include "editor/editor_input.hpp"
#include "editor/editor_layout.hpp"
#include "editor/editor_theme.hpp"
#include "editor/fix_project.hpp"
#include "editor/input_settings.hpp"
#include "editor/inspector.hpp"
#include "editor/level_objects.hpp"
#include "editor/model_preview_window.hpp"
#include "editor/play_mode.hpp"
#include "editor/playback_controls.hpp"
#include "editor/profiler_window.hpp"
#include "editor/scene_orientation.hpp"
#include "editor/script_editor.hpp"
#include "editor/transform_editor.hpp"
#include "editor/vulkan_asset_preview_renderer.hpp"
#include "editor/vulkan_preview_backend.hpp"
#include "gameplay/first_person.hpp"
#include "gameplay/raycast.hpp"
#include "levels/level_system.hpp"
#include "models/collider_source.hpp"
#include "physics/collider_debug.hpp"
#include "physics/physics.hpp"
#include "platform/engine_log.hpp"
#include "platform/project_dialog.hpp"
#include "prefabs/prefab.hpp"
#include "project/project_backup.hpp"
#include "project/project_document.hpp"
#include "project/project_io.hpp"
#include "project/rendering_settings.hpp"
#include "rendering/mesh.hpp"
#include "rendering/vulkan_camera_region.hpp"
#include "rendering/vulkan_editor_overlays.hpp"
#include "rendering/vulkan_scene_renderer.hpp"
#ifdef LEGACY_FSR3_GAME
#include "rendering/vulkan_fsr3_game.hpp"
#endif
#include "scene/camera.hpp"
#include "scene/scene_camera.hpp"
#include "scene/skybox_transition.hpp"
#include "scripting/script_components.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

// ---------------------------------------------------------------------------
// Scene description
// ---------------------------------------------------------------------------

constexpr int kGridCells = 32;      // grid blocks per side
constexpr float kCellSize = 1.0f;   // metres per grid block
constexpr float kPlaneSize = 16.0f; // metres per side of the plane
constexpr float kCubeSize = 1.0f;   // metres per edge of a cube

// Where the centre of the first cube is before it falls.
constexpr Vec3 kCubeStart = {0.0f, 1.0f, 0.0f};

// A press of the left mouse button is a click as long as the pointer stays
// within this distance of where it went down. Further away it is a drag.
constexpr float kClickRadius = 5.0f;

// How far from the camera a click reaches, in metres.
constexpr float kClickReach = 100.0f;

// Where there is no room for a new cube, room is looked for beside the spot
// and above it: this many metres further away at each try, for this many
// tries.
constexpr float kRoomStep = 0.02f;
constexpr int kRoomSteps = 250;

// ---------------------------------------------------------------------------
// The cubes that clicks create
// ---------------------------------------------------------------------------

// The ray from the camera through a mouse position. Returns false while the
// window has no size (it is minimised, say).
static bool mouseRay(SDL_Window* window,
                     const OrbitCamera& camera,
                     float x,
                     float y,
                     Ray& ray,
                     const EditorRect& scene) {
    // Mouse positions are counted in the units of the window's size, which are
    // not pixels on a high-density display. The shape of the picture comes
    // from its size in pixels, as it does for drawing.
    int windowWidth = 0;
    int windowHeight = 0;
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);
    SDL_GetWindowSizeInPixels(window, &width, &height);
    if (windowWidth <= 0 || windowHeight <= 0 || width <= 0 || height <= 0) {
        return false;
    }
    if (!scene.contains(x, y) || scene.width <= 0 || scene.height <= 0) {
        return false;
    }
    const auto pixels = scenePixels(scene, windowWidth, windowHeight, width, height);
    ray = camera.rayThrough((x * width / windowWidth - pixels.x) / pixels.width,
                            (y * height / windowHeight - pixels.top) / pixels.height,
                            pixels.aspect());
    return true;
}

// The place nearest to `center` where a cube has room: `center` itself if
// nothing is in the way there. If something is, the nearest place where
// nothing is: beside `center`, no further away than a cube is wide, or above
// it.
static Vec3
findRoom(const Physics& physics, Vec3 center, ColliderShape shape = ColliderShape::Box) {
    // Things that rest on each other touch, and reach into each other by up to
    // a centimetre. Neither is in the way, so room is looked for with a cube
    // that is that much smaller on every side.
    const float inner = kCubeSize - 2.0f * 0.01f;
    const Vec3 size = {inner, shape == ColliderShape::Capsule ? inner + 1.0f : inner, inner};
    if (!physics.checkBox(center, size)) {
        return center;
    }

    static const Vec3 kWays[] = {
        {1.0f, 0.0f, 0.0f},
        {-1.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f, -1.0f},
        {0.0f, 1.0f, 0.0f}, // up, but never down: that is where the ground is
    };
    for (int step = 1; step <= kRoomSteps; ++step) {
        const float shift = static_cast<float>(step) * kRoomStep;
        for (const Vec3& way : kWays) {
            if (way.y == 0.0f && shift > kCubeSize) {
                continue; // that far to the side is the other side of whatever is in the way
            }
            const Vec3 place = center + way * shift;
            if (!physics.checkBox(place, size)) {
                return place;
            }
        }
    }
    return center; // no room anywhere near: the physics pushes things apart
}

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

// Shaders are compiled at build time and installed next to the executable.
static std::vector<uint32_t> loadVulkanShader(const char* name) {
    const char* base = SDL_GetBasePath();
    if (!base) {
        throw std::runtime_error("Cannot locate the executable directory");
    }
    const auto path = std::filesystem::path(base) / "data/shaders/vulkan/compiled" / name;
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("Cannot open Vulkan shader: " + path.string());
    }
    const auto size = stream.tellg();
    if (size <= 0 || size > 4 * 1024 * 1024 || size % 4 != 0) {
        throw std::runtime_error("Invalid Vulkan shader: " + path.string());
    }
    std::vector<uint32_t> words(static_cast<std::size_t>(size) / 4);
    stream.seekg(0);
    if (!stream.read(reinterpret_cast<char*>(words.data()), size) || words[0] != 0x07230203) {
        throw std::runtime_error("Invalid Vulkan shader: " + path.string());
    }
    return words;
}

// ---------------------------------------------------------------------------
// The scene and the main loop
// ---------------------------------------------------------------------------

static bool hasVisibleGeometry(VulkanSceneSurface& surface) {
    const auto pixels = surface.readPixels();
    if (pixels.size() < 8) {
        return false;
    }
    std::size_t different = 0;
    for (std::size_t i = 4; i + 2 < pixels.size(); i += 4) {
        if (pixels[i] != pixels[0] || pixels[i + 1] != pixels[1] || pixels[i + 2] != pixels[2]) {
            if (++different >= 50) {
                return true;
            }
        }
    }
    return false;
}

static bool run(SDL_Window* window,
                VulkanContext& context,
                VulkanSceneSurface& sceneSurface,
                VulkanSceneRenderer& renderer,
                VulkanImGui& gui,
                const VulkanEditorPreviewBackend::Shaders& shaders,
                const std::vector<uint32_t>& litFragment,
                const std::vector<uint32_t>& shadowVertex,
                bool smokeTest) {
    World world;
    Physics physics;
    LayerSettings layerSettings;
    sceneQuery = [&](World& w, Entity e, const Raycast& q) { return queryScene(w, physics, e, q); };

    // Meshes are created once and can be shared by any number of entities.
    const MeshHandle gridMesh = renderer.createMesh(makeGrid(kGridCells, kCellSize));
    const MeshHandle planeMesh = renderer.createMesh(makePlane(kPlaneSize, kPlaneSize));
    const MeshHandle cubeMesh = renderer.createMesh(makeBox({kCubeSize, kCubeSize, kCubeSize}));

    // Default illumination; editor grid is not scene content.
    const Entity sun = world.create();
    world.add(sun, ObjectName{"Directional Light"});
    Transform sunTransform;
    sunTransform.rotation = rotationFromEuler({-55, -30, 0});
    world.add(sun, sunTransform);
    world.add(sun, DirectionalLight{});
    // An ordinary initial cube, with the same physics as other created shapes.
    const Entity cube = world.create();
    world.add(cube, ObjectName{"Cube"});
    world.add(cube, Transform{kCubeStart});
    world.add(cube, MeshRenderer{cubeMesh, {1, 1, 1}});
    world.add(cube, BoxCollider{{kCubeSize, kCubeSize, kCubeSize}});
    world.add(cube, RigidBody{});

    const MeshHandle sphereMesh = renderer.createMesh(makeSphere(0.5f));
    const MeshHandle capsuleMesh = renderer.createMesh(makeCapsule(0.5f, 1.0f));
    const MeshHandle coneMesh = renderer.createMesh(makeCone(0.5f, 1.0f));
    // Exercise both the editor-only camera model and active Game render path in CI.
    if (smokeTest) {
        const Entity testCamera = world.create();
        Transform pose{{3, 2, 6}};
        aimCamera(pose, {0, .5f, 0});
        world.add(testCamera, ObjectName{"Smoke Camera"});
        world.add(testCamera, pose);
        Camera c;
        c.active = true;
        world.add(testCamera, c);
    }
    float cameraBoost = 3.0f;
    float cameraSpeed = 5.0f;
    Entity selectedEntity;
    if (smokeTest) {
        selectedEntity = activeCamera(world);
    }
    ScriptEditor scripts;
    scripts.bindWorld(world, cubeMesh, sphereMesh, capsuleMesh, coneMesh, planeMesh);
    EditorHistory history(world);
    PlayMode playMode;
    ObjectClipboard clipboard;
    AssetBrowser browser;
    VulkanBrowserThumbnails browserThumbnails(browser.thumbnailSource, context, gui);
    ScriptComponents componentScripts;
    AudioSystem audio;
    AnimationSystem animations;
    VulkanEditorPreviewBackend editorPreviews(sceneSurface, renderer, gui, shaders);
    AnimationWindow animationWindow;
    ModelPreviewWindow modelPreviewWindow;
    VulkanSceneSurface gameSurface(context.physicalDevice(),
                                   context.device(),
                                   context.graphicsQueue(),
                                   context.graphicsFamily(),
                                   shaders.unlitVertex,
                                   shaders.unlitFragment,
                                   &sceneSurface);
#ifdef LEGACY_FSR3_GAME
    gameSurface.enableTransferDestination();
    std::unique_ptr<VulkanSceneSurface> fsrInputSurface;
    std::unique_ptr<VulkanFsr3Game> fsrGame;
    TemporalHistory fsrHistory;
    TemporalSampling::Extent fsrRender{}, fsrDisplay{};
    std::string fsrStatus = "Off";
    std::uint64_t fsrFrameNumber = 0, fsrLastFrame = 0;
    Vec3 fsrPreviousEye{}, fsrPreviousForward{};
    bool fsrHasPreviousEye = false, fsrHardwareUnavailable = false;
    int fsrLastQuality = 0;
#endif
    VulkanSceneSurface cameraPreviewSurface(context.physicalDevice(),
                                            context.device(),
                                            context.graphicsQueue(),
                                            context.graphicsFamily(),
                                            shaders.unlitVertex,
                                            shaders.unlitFragment,
                                            &sceneSurface);
    for (auto* surface : {&gameSurface, &cameraPreviewSurface}) {
        surface->enableLighting(shaders.litVertex, litFragment, shadowVertex);
        surface->enableSkybox(shaders.skyVertex, shaders.skyFragment);
    }
    struct SurfaceRegistrations {
        VulkanImGui& gui;
        VulkanSceneSurface& scene;
        VulkanSceneSurface& game;
        VulkanSceneSurface& camera;
        ~SurfaceRegistrations() {
            gui.cancelFrame();
            try {
                gui.forget(scene);
                gui.forget(game);
                gui.forget(camera);
            } catch (...) {
            }
        }
    } registrations{gui, sceneSurface, gameSurface, cameraPreviewSurface};
    VulkanEditorOverlays overlays(sceneSurface);
    VulkanAssetPreviewRenderer assetPreviews(
        sceneSurface,
        renderer,
        gui,
        context,
        shaders,
        {gridMesh, planeMesh, cubeMesh, sphereMesh, capsuleMesh, coneMesh});
    EditorRect sceneRect{};
    bool sceneHovered = false, resetWorkspace = false;
    Entity editSelection;
    TransformEditor editor;
    editor.meshBounds = [&renderer](MeshHandle mesh, Vec3& low, Vec3& high) {
        return renderer.meshBounds(mesh, low, high);
    };
    OrbitCamera camera;
    const ProjectIO::Meshes projectMeshes{
        gridMesh, planeMesh, cubeMesh, sphereMesh, capsuleMesh, coneMesh};
    std::unique_ptr<World> prefabLevel;
    std::optional<EditorHistory> prefabHistory;
    std::string editingPrefab, editingCubemap;
    CubemapAsset cubemapDraft;
    Entity prefabLevelSelection;
    OrbitCamera prefabLevelCamera;
    LevelSystem levels(projectMeshes);
    levels.setRoot(browser.assets.root);
    LevelSystem::active = &levels;
    RenderingSettings renderingSettings;
    RenderingSettings::active = &renderingSettings;
    renderingSettings.applyVsync = [&](bool enabled) { context.setVsync(enabled); };
    renderingSettings.presentationStatus = [&] {
        return renderingSettings.vsync ? std::string("Vulkan VSync requested")
                                       : std::string("Vulkan non-VSync requested");
    };
    std::string editLevelName, editSource, editScriptPath, embeddedLegacySource, editLegacySource;
    OrbitCamera editCamera;
    EditorHistory editHistory(world);
    bool renameLevelPopup = false;
    char renameLevelName[129]{};
    bool newLevelPopup = false;
    char newLevelName[129] = "New Level";
    ProjectDialog projectDialog;
    std::string currentProject, projectError, savedProjectState, lastWindowTitle, backupStatus;
    bool projectDirty = true;
    InputState input;
    input.window = window;
    InputSettings inputSettings;
    EngineLog::Errors terminalErrors;
    bool gameHovered = false, gameFocused = false, focusGame = false;
    std::optional<EditorSettings> settingsGesture;
    bool separateGame = smokeTest;
    SDL_Window* gameWindow = nullptr;
    std::unique_ptr<VulkanContext> gameContext;
    struct GameWindowGuard {
        SDL_Window*& window;
        std::unique_ptr<VulkanContext>& context;
        ~GameWindowGuard() {
            context.reset();
            if (window) {
                SDL_DestroyWindow(window);
            }
        }
    } gameWindowGuard{gameWindow, gameContext};
    auto toggleGameFullscreen = [&]() {
        if (!gameWindow) {
            return;
        }
        const bool fullscreen = (SDL_GetWindowFlags(gameWindow) & SDL_WINDOW_FULLSCREEN) != 0;
        if (!SDL_SetWindowFullscreen(gameWindow, !fullscreen)) {
            projectError = SDL_GetError();
        }
    };
    Entity mouseOver, mousePressedObject;
    ImVec2 gameImageOrigin{}, gameImageSize{};
    bool showFPS = false, showGameColliders = false, showProfiler = false;
    FrameProfiler profiler;
    activeProfiler = &profiler;
    Uint64 lastTicks = SDL_GetTicksNS();
    bool running = true, exitRequested = false, exitAfterSave = false;
    auto lastAutosave = std::chrono::steady_clock::now();

    // The left mouse button does two things. Pressed and released in one
    // place, it is a click, which selects. Moving away cancels
    // the click; camera navigation uses the right button and keyboard.
    bool clicking = false; // the button is down and this may still be a click
    float pressX = 0.0f;   // where the button went down
    float pressY = 0.0f;

    bool cameraLooking = false, orientationInteracting = false;
    unsigned renderedFrames = 0;
    unsigned smokeScene = 0, smokeGame = 0, smokeEmbeddedGame = 0, smokeCamera = 0,
             smokeSecondary = 0;
    bool smokeFailed = false;
#ifdef LEGACY_FSR3_GAME
    const bool smokeFsr = smokeTest && std::getenv("LEGACY_SMOKE_FSR") != nullptr;
    unsigned smokeFsrEmbedded = 0, smokeFsrDetached = 0, smokeFsrFallback = 0, smokeFsrOff = 0;
#endif
    constexpr unsigned smokeFrames = 120;
    std::optional<Vec3> lastOrbitPivot;
    while (running) {
#ifdef LEGACY_FSR3_GAME
        ++fsrFrameNumber;
#endif
        if (!playMode.active()) {
            if (const auto* pose = world.tryGet<Transform>(selectedEntity)) {
                lastOrbitPivot = pose->position;
            }
        }
        profiler.begin(showProfiler);
        int windowWidth = 1, windowHeight = 1;
        SDL_GetWindowSize(window, &windowWidth, &windowHeight);
        EditorLayout layout = EditorLayout::calculate(static_cast<float>(windowWidth),
                                                      static_cast<float>(windowHeight),
                                                      std::max(20.0f, ImGui::GetFrameHeight()));
        layout.scene = sceneRect;
        scripts.console().setAssetRoot(browser.assets.root);
        inputSettings.load(browser.assets, input);
        renderingSettings.load(browser.assets);
#ifdef LEGACY_FSR3_GAME
        if (smokeFsr) {
            renderingSettings.fsr3GameQuality =
                renderedFrames < 5 || renderedFrames >= 90 ? 0 : (renderedFrames < 50 ? 2 : 3);
            if (const auto e = activeCamera(world); world.alive(e)) {
                world.get<Camera>(e).orthographic = renderedFrames >= 60 && renderedFrames < 65;
            }
            if (gameWindow && renderedFrames == 70) {
                SDL_SetWindowSize(gameWindow, 640, 360);
            }
        }
#endif
        layerSettings.load(browser.assets);
        if (levels.root() != browser.assets.root) {
            levels.setRoot(browser.assets.root);
        }
        if (auto transition = levels.poll()) {
            levels.suspended = true;
            editor.finish(world);
            audio.clear();
            componentScripts.stop();
            scripts.shutdown();
            input.releaseMouse();
            LuaConsole::clearEvents();
            world.restore(transition->level->world);
            try {
                Prefabs::refreshAll(browser.assets, world, projectMeshes);
            } catch (const std::exception& e) {
                browser.assets.error = e.what();
            }
            embeddedLegacySource = transition->level->source;
            camera = transition->level->camera;
            scripts.resetRuntime();
            browser.openedScript.clear();
            scripts.setAssetContext("", {});
            scripts.setSource("");
            selectedEntity = {};
            history = EditorHistory(world);
            physics.reset();
            clicking = false;
            try {
                levels.commit(transition->name, !playMode.active());
            } catch (const std::exception& error) {
                levels.error = error.what();
            }
            levels.suspended = false;
            if (playMode.active()) {
                audio.start(world);
                scripts.setAssetContext("", {});
                scripts.console().setAssetRoot(browser.assets.root);
                scripts.startPlay();
                const MeshHandle runtimeMeshes[]{cubeMesh, sphereMesh, capsuleMesh, coneMesh};
                componentScripts.start(world, browser.assets, runtimeMeshes, planeMesh);
                if (!transition->name.empty()) {
                    LuaConsole::postEvent("onLevelLoaded", {transition->name});
                }
            }
        }
        if (auto failure = levels.takeFailure()) {
            LuaConsole::postEvent("onLevelFailedToLoad",
                                  {failure->name, failure->error.substr(0, 4096)});
        }
        const bool externalFocus =
            gameWindow && (SDL_GetWindowFlags(gameWindow) & SDL_WINDOW_INPUT_FOCUS);
        input.window = externalFocus ? gameWindow : window;
        input.mouseLocked =
            playMode.active() && input.window && SDL_GetWindowRelativeMouseMode(input.window);
        input.enabled = ((SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) || externalFocus) &&
                        playMode.playing() && (gameFocused || externalFocus || input.mouseLocked) &&
                        (!ImGui::GetIO().WantTextInput || input.mouseLocked) &&
                        !inputSettings.visible;
        input.mouseEnabled = externalFocus || gameHovered || sceneHovered || input.mouseLocked;
        input.beginFrame();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
                event.type == SDL_EVENT_WINDOW_RESTORED) {
                if (event.window.windowID == SDL_GetWindowID(window)) {
                    context.requestResize();
                } else if (gameWindow && event.window.windowID == SDL_GetWindowID(gameWindow) &&
                           gameContext) {
                    gameContext->requestResize();
                }
            }
            if (gameWindow && event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(gameWindow)) {
                input.releaseMouse();
                input.window = window;
                gameContext.reset();
                SDL_DestroyWindow(gameWindow);
                gameWindow = nullptr;
                continue;
            }
            if (gameWindow && SDL_GetWindowFromEvent(&event) == gameWindow) {
                const bool fullscreenKey =
                    event.type == SDL_EVENT_KEY_DOWN &&
                    (event.key.key == SDLK_F11 ||
                     (event.key.key == SDLK_RETURN && (event.key.mod & SDL_KMOD_ALT)));
                if (fullscreenKey) {
                    if (!event.key.repeat) {
                        toggleGameFullscreen();
                    }
                    continue;
                }
                input.event(event);
                continue;
            }
            const auto captureButtons =
                inputSettings.waiting.empty() ? decltype(input.buttons){} : input.buttons;
            const auto captureAlternatives =
                inputSettings.waiting.empty() ? decltype(input.alternatives){} : input.alternatives;
            if (inputSettings.capture(event, browser.assets, input)) {
                if (!playMode.active() && (captureButtons != input.buttons ||
                                           captureAlternatives != input.alternatives)) {
                    const auto afterButtons = input.buttons;
                    const auto afterAlternatives = input.alternatives;
                    input.buttons = captureButtons;
                    input.alternatives = captureAlternatives;
                    history.perform(
                        world,
                        selectedEntity,
                        [&, captureButtons, captureAlternatives, afterButtons, afterAlternatives](
                            bool forward) {
                            input.buttons = forward ? afterButtons : captureButtons;
                            input.alternatives = forward ? afterAlternatives : captureAlternatives;
                            inputSettings.save(browser.assets, input);
                            scripts.console().message(forward ? "Set/Redo input binding"
                                                              : "Undo input binding");
                        });
                }
                continue;
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
                (cameraLooking && event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                 event.button.button == SDL_BUTTON_RIGHT)) {
                cameraLooking = false;
                SDL_SetWindowRelativeMouseMode(window, false);
                SDL_SetWindowMouseGrab(window, false);
                SDL_SetWindowMouseRect(window, nullptr);
            }
            gui.processEvent(event);
            input.event(event);
            const bool mouseEvent = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                                    event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                                    event.type == SDL_EVENT_MOUSE_MOTION ||
                                    event.type == SDL_EVENT_MOUSE_WHEEL;
            float pointerX = 0, pointerY = 0;
            SDL_GetMouseState(&pointerX, &pointerY);
            if (mouseEvent && !cameraLooking &&
                (orientationInteracting ||
                 (sceneHovered && layout.scene.width >= 100 && layout.scene.height >= 100 &&
                  orientationRect(layout.scene).contains(pointerX, pointerY)))) {
                clicking = false;
                continue;
            }
            if (mouseEvent &&
                (projectDialog.pending() || browser.dialog.pending() ||
                 (ImGui::GetIO().WantCaptureMouse && !sceneHovered && !cameraLooking) ||
                 (editor.capturesMouse() && !cameraLooking &&
                  !(SDL_GetModState() & SDL_KMOD_ALT)))) {
                clicking = false;
                continue;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && ImGui::GetIO().WantCaptureKeyboard) {
                continue;
            }
            switch (event.type) {
            case SDL_EVENT_DROP_FILE:
                if (!playMode.active() && !projectDialog.pending() && !levels.busy() &&
                    event.drop.data) {
                    browser.importFile(event.drop.data);
                }
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                editor.finish(world);
                clicking = false;
                break;
            case SDL_EVENT_QUIT:
                SDL_Log("Shutdown requested: SDL quit event");
                exitRequested = true;
                break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                exitRequested = true;
                break;
            case SDL_EVENT_KEY_DOWN:
                break; // Escape is reserved for UI cancellation, never application exit.
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_RIGHT && !playMode.active() && sceneHovered &&
                    layout.scene.contains(event.button.x, event.button.y) &&
                    !ImGui::GetIO().WantTextInput && !cameraModifiersHeld(SDL_GetModState())) {
                    SDL_SetWindowMouseRect(window, nullptr);
                    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_CENTER, "0");
                    SDL_SetWindowMouseGrab(window, true);
                    cameraLooking = SDL_SetWindowRelativeMouseMode(window, true);
                    clicking = false;
                }
                if (event.button.button == SDL_BUTTON_LEFT) {
                    clicking = !(SDL_GetModState() & SDL_KMOD_ALT);
                    pressX = event.button.x;
                    pressY = event.button.y;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (clicking &&
                    std::hypot(event.motion.x - pressX, event.motion.y - pressY) > kClickRadius) {
                    clicking = false; // too far for a click: a drag from here on
                }
                // Right-drag looks around from the camera's position. Left-drag
                // cancels a click but never moves the camera or selects.
                if (!playMode.active() && sceneHovered && !editor.dragging() &&
                    (event.motion.state & SDL_BUTTON_LMASK) && (SDL_GetModState() & SDL_KMOD_ALT) &&
                    !(SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) {
                    clicking = false;
                    if (const auto* t = world.tryGet<Transform>(selectedEntity)) {
                        camera.orbit(t->position, event.motion.xrel, event.motion.yrel);
                    } else if (lastOrbitPivot) {
                        camera.orbit(*lastOrbitPivot, event.motion.xrel, event.motion.yrel);
                    }
                }
                if (event.motion.state & SDL_BUTTON_RMASK) {
                    clicking = false;
                    if (cameraLooking && !cameraModifiersHeld(SDL_GetModState())) {
                        camera.look(event.motion.xrel, event.motion.yrel);
                    }
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_LEFT && clicking) {
                    clicking = false;

                    // Scene clicks only select; object creation belongs to Create menus.
                    Ray ray;
                    if (mouseRay(
                            window, camera, event.button.x, event.button.y, ray, layout.scene)) {
                        {
                            RayHit hit;
                            const bool found = physics.raycast(ray, kClickReach, hit);
                            selectedEntity = found ? hit.entity : Entity{};
                            float closest = found ? hit.distance : kClickReach;
                            world.each<Camera, Transform>([&](Entity e, Camera&, Transform& t) {
                                const auto offset = ray.origin - t.position;
                                const float radius = 1.1f * std::max({std::abs(t.scale.x),
                                                                      std::abs(t.scale.y),
                                                                      std::abs(t.scale.z)});
                                const float b = dot(offset, ray.direction),
                                            disc = b * b - dot(offset, offset) + radius * radius;
                                if (disc >= 0) {
                                    float distance = -b - std::sqrt(disc);
                                    if (distance < 0) {
                                        distance = -b + std::sqrt(disc);
                                    }
                                    if (distance >= 0 && distance < closest) {
                                        closest = distance;
                                        selectedEntity = e;
                                    }
                                }
                            });
                            break;
                        }
                    }
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL: {
                if (!playMode.active() && (sceneHovered || cameraLooking) &&
                    !cameraModifiersHeld(SDL_GetModState()) && !ImGui::GetIO().WantTextInput) {
                    if (cameraLooking) {
                        if (SDL_GetModState() & SDL_KMOD_SHIFT) {
                            cameraBoost = adjustSceneCameraBoost(cameraBoost, event.wheel.y);
                        } else {
                            cameraSpeed = std::clamp(
                                cameraSpeed * std::pow(1.2f, event.wheel.y), .1f, 500.0f);
                        }
                    } else {
                        camera.zoom(event.wheel.y);
                        camera.pan(-event.wheel.x);
                    }
                }
                break;
            }
            default:
                break;
            }
        }

        browserThumbnails.prepare(browser.assets);
        assetPreviews.prepare(browser.assets);
        // Restore the editor to show its unsaved-changes confirmation rather
        // than silently discarding a pending close while minimized.
        if (exitRequested && (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)) {
            if (!SDL_RestoreWindow(window)) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_APPLICATION, "Could not restore window: %s", SDL_GetError());
            }
        }
        if (!gui.beginFrame(gameWindow && gameContext && playMode.active())) {
            SDL_Delay(16);
            continue;
        }
        ImGuizmo::BeginFrame();
        if (smokeTest && renderedFrames == 3) {
            ImGui::SetWindowFocus("Game");
        }
        if (smokeTest && renderedFrames == 12) {
            ImGui::SetWindowFocus("Scene");
        }
        if (smokeTest && renderedFrames == 70) {
            context.requestResize();
            if (gameContext) {
                gameContext->requestResize();
            }
            renderingSettings.vsync = false;
            renderingSettings.apply();
        }
        if (smokeTest && renderedFrames == 72) {
            renderingSettings.vsync = true;
            renderingSettings.apply();
        }
        bool openProject = false, saveProject = false, saveAsProject = false;
        const bool projectEnabled = !browser.importing() && !prefabLevel && !levels.busy() &&
                                    !playMode.active() && !editor.dragging() &&
                                    !projectDialog.pending() && !browser.dialog.pending();
        profiler.checkpoint(ProfilePart::Events);
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                openProject = ImGui::MenuItem("Open Project...", "Ctrl+O", false, projectEnabled);
                saveProject = ImGui::MenuItem("Save Project", "Ctrl+S", false, projectEnabled);
                saveAsProject =
                    ImGui::MenuItem("Save Project As...", "Ctrl+Shift+S", false, projectEnabled);
                ImGui::Separator();
                if (ImGui::MenuItem("New Level...", nullptr, false, projectEnabled)) {
                    newLevelPopup = true;
                }
                if (ImGui::MenuItem("Save Level", nullptr, false, projectEnabled)) {
                    try {
                        if (!animationWindow.saveIfDirty(browser.assets)) {
                            throw std::runtime_error(animationWindow.error());
                        }
                        scripts.saveFile(browser.assets);
                        levels.saveCurrent(world, embeddedLegacySource, camera);
                    } catch (const std::exception& e) {
                        levels.error = e.what();
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Quit")) {
                    SDL_Log("Shutdown requested: File > Quit");
                    exitRequested = true;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        if (projectEnabled && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive()) {
            openProject |= ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O, ImGuiInputFlags_RouteGlobal);
            saveProject |= ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal);
            saveAsProject |= ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S,
                                             ImGuiInputFlags_RouteGlobal);
        }
        auto saveTo = [&](const std::string& path) {
            bool saved = false;
            auto assetRoot = projectAssetsPath(path);
            try {
                if (browser.importing()) {
                    throw std::runtime_error("Wait for the asset import to finish before saving");
                }
                if (!animationWindow.saveIfDirty(browser.assets)) {
                    throw std::runtime_error(animationWindow.error());
                }
                scripts.saveFile(browser.assets);
                levels.saveCurrent(world, embeddedLegacySource, camera);
                browser.assets.copyTo(assetRoot);
                AssetManager destination;
                destination.root = assetRoot;
                renderingSettings.save(destination);
                inputSettings.save(destination, input);
                if (!inputSettings.error.empty()) {
                    throw std::runtime_error(inputSettings.error);
                }
            } catch (const std::exception& e) {
                projectError = e.what();
                exitAfterSave = false;
                return false;
            }
            if (ProjectIO::save(
                    path, world, embeddedLegacySource, camera, projectMeshes, projectError)) {
                if (std::filesystem::weakly_canonical(browser.assets.root) !=
                    std::filesystem::weakly_canonical(assetRoot)) {
                    history = EditorHistory(
                        world,
                        selectedEntity); // Old Trash receipts belong to the previous project root.
                }
                browser.assets.root = assetRoot;
                currentProject = path;
                savedProjectState = projectContentState(world, embeddedLegacySource, camera);
                projectDirty = false;
                saved = true;
                lastAutosave = std::chrono::steady_clock::now();
                if (exitAfterSave) {
                    running = false;
                }
            }
            exitAfterSave = false;
            return saved;
        };
        if (projectEnabled) {
            if (saveProject && !currentProject.empty()) {
                saveTo(currentProject);
            } else if (openProject || saveProject || saveAsProject) {
                bool proceed = true;
                if (openProject && projectDirty) {
                    const SDL_MessageBoxButtonData buttons[]{
                        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel"},
                        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Open project"}};
                    const SDL_MessageBoxData box{
                        SDL_MESSAGEBOX_WARNING,
                        window,
                        "Unsaved project",
                        "Opening another project replaces unsaved changes. Continue?",
                        2,
                        buttons,
                        nullptr};
                    int button = 0;
                    proceed = SDL_ShowMessageBox(&box, &button) && button == 1;
                }
                if (proceed) {
                    projectDialog.show(window, !openProject, currentProject);
                }
            }
        }
        if (auto result = projectDialog.poll()) {
            if (result->path.empty() || !result->error.empty()) {
                exitAfterSave = false;
            }
            if (!result->error.empty()) {
                projectError = result->error;
            } else if (!result->path.empty()) {
                if (result->saving) {
                    auto path = std::filesystem::path(result->path);
                    if (!path.has_extension()) {
                        path += ".legacy";
                    }
                    saveTo(path.string());
                } else {
                    std::string source, assetDirectory;
                    World loadedWorld;
                    OrbitCamera loadedCamera;
                    if (ProjectIO::load(result->path,
                                        loadedWorld,
                                        source,
                                        loadedCamera,
                                        projectMeshes,
                                        projectError,
                                        &assetDirectory)) {
                        audio.clear();
                        componentScripts.stop();
                        scripts.shutdown();
                        world.restore(loadedWorld);
                        camera = loadedCamera;
                        browser.setRoot(
                            assetDirectory.empty()
                                ? existingProjectAssetsPath(result->path)
                                : std::filesystem::absolute(result->path).parent_path() /
                                      assetDirectory);
                        try {
                            Prefabs::refreshAll(browser.assets, world, projectMeshes);
                        } catch (const std::exception& e) {
                            browser.assets.error = e.what();
                        }
                        levels.setRoot(browser.assets.root);
                        if (!levels.current().empty()) {
                            levels.load();
                        }
                        scripts.resetRuntime();
                        scripts.setSource("");
                        embeddedLegacySource = source;
                        selectedEntity = {};
                        history = EditorHistory(world);
                        physics.reset();
                        clicking = false;
                        currentProject = result->path;
                        savedProjectState =
                            projectContentState(world, embeddedLegacySource, camera);
                        projectDirty = false;
                        lastAutosave = std::chrono::steady_clock::now();
                    }
                }
            }
        }
        if (!projectError.empty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Project: %s", projectError.c_str());
            SDL_ShowSimpleMessageBox(
                SDL_MESSAGEBOX_ERROR, "Project error", projectError.c_str(), window);
            projectError.clear();
        }
        drawWorkspace(layout.toolbar, resetWorkspace);
        ImGui::BeginDisabled(projectDialog.pending() || browser.dialog.pending());
        const int created = drawCreateMenu();
        auto createObject = [&](int created) {
            editor.finish(world);
            clicking = false;
            if (!validEditorObjectType(created)) {
                return;
            }
            const ColliderShape shapes[]{ColliderShape::Box,
                                         ColliderShape::Capsule,
                                         ColliderShape::Cone,
                                         ColliderShape::Box,
                                         ColliderShape::Sphere};
            Vec3 position = camera.target;
            position.y = std::max(position.y, created == 1 ? 1.0f : created == 0 ? 0.0f : 0.5f);
            if (created > 0 && created < 5) {
                position = findRoom(physics, position, shapes[created]);
            }
            Entity prefabRoot;
            if (prefabLevel) {
                try {
                    prefabRoot = Prefabs::root(world);
                } catch (const std::exception&) {
                }
            }
            selectedEntity = createEditorObject(
                world, created, position, cubeMesh, sphereMesh, capsuleMesh, coneMesh, planeMesh);
            if (prefabRoot != Entity{} && world.alive(selectedEntity)) {
                setParent(world, selectedEntity, prefabRoot);
            }
        };
        if (created >= 0) {
            createObject(created);
        }
        bool undoRequested = false, redoRequested = false;
        bool duplicateRequested = false, copyRequested = false, pasteRequested = false,
             parentRequested = false;
        const bool editAllowed = !playMode.active() && !editor.dragging() &&
                                 !projectDialog.pending() && !browser.dialog.pending();
        const bool objectSelected = world.alive(selectedEntity);
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Edit")) {
                undoRequested =
                    ImGui::MenuItem("Undo", "Ctrl+Z", false, editAllowed && history.canUndo());
                redoRequested = ImGui::MenuItem(
                    "Redo", "Ctrl+Y / Ctrl+Shift+Z", false, editAllowed && history.canRedo());
                ImGui::Separator();
                duplicateRequested =
                    ImGui::MenuItem("Duplicate", "Ctrl+D", false, editAllowed && objectSelected);
                copyRequested =
                    ImGui::MenuItem("Copy", "Ctrl+C", false, editAllowed && objectSelected);
                pasteRequested =
                    ImGui::MenuItem("Paste", "Ctrl+V", false, editAllowed && clipboard.available());
                parentRequested = ImGui::MenuItem(
                    "Parent / Unparent", "Ctrl+P", false, editAllowed && objectSelected);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Windows")) {
                for (int i = 0; i < 7; ++i) {
                    ImGui::MenuItem(panelNames[i], nullptr, &panelOpen[i]);
                }
                ImGui::MenuItem("Animation", nullptr, &animationWindow.visible);
                ImGui::Separator();
                if (ImGui::MenuItem("Reset layout")) {
                    resetWorkspace = true;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        bool showAbout = false;
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Project", !prefabLevel)) {
                if (ImGui::MenuItem("Open in VS Code", nullptr, false, projectEnabled)) {
                    const auto folder =
                        currentProject.empty()
                            ? browser.assets.root
                            : std::filesystem::absolute(currentProject).parent_path();
                    if (!browser.settings.openExternal(folder)) {
                        projectError = browser.settings.error;
                    }
                }
                if (ImGui::MenuItem(
                        "Backup", nullptr, false, projectEnabled && !currentProject.empty())) {
                    try {
                        if (saveTo(currentProject)) {
                            backupStatus =
                                "Backup created: " + backupProject(currentProject).u8string();
                        }
                    } catch (const std::exception& e) {
                        projectError = e.what();
                    }
                }
                if (!backupStatus.empty()) {
                    ImGui::TextDisabled("Last backup created");
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s", backupStatus.c_str());
                    }
                }

                if (ImGui::MenuItem(
                        "Fix Project", nullptr, false, projectEnabled && !currentProject.empty())) {
                    try {
                        if (!std::filesystem::exists(currentProject + ".before-fix.bak")) {
                            std::filesystem::copy_file(currentProject,
                                                       currentProject + ".before-fix.bak");
                        }
                        fixProjectAssets(browser.assets, projectMeshes);
                        saveTo(currentProject);
                    } catch (const std::exception& e) {
                        projectError = e.what();
                    }
                }
                if (ImGui::MenuItem("Input Settings")) {
                    inputSettings.visible = true;
                }
                if (ImGui::MenuItem("Layers")) {
                    layerSettings.visible = true;
                }
                if (ImGui::MenuItem("Rendering")) {
                    renderingSettings.visible = true;
                }
                if (ImGui::BeginMenu("Levels",
                                     !playMode.active() && !levels.busy() &&
                                         !projectDialog.pending())) {
                    for (const auto& name : std::vector<std::string>(levels.names())) {
                        if (ImGui::MenuItem(name.c_str(), nullptr, name == levels.current())) {
                            try {
                                scripts.saveFile(browser.assets);
                                levels.saveCurrent(world, embeddedLegacySource, camera);
                                levels.load(name);
                            } catch (const std::exception& e) {
                                levels.error = e.what();
                            }
                        }
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Settings")) {
                if (ImGui::MenuItem("Editor Settings")) {
                    browser.settings.visible = true;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Debug")) {
                ImGui::MenuItem("Show FPS", nullptr, &showFPS);
                ImGui::MenuItem("Profiler", nullptr, &showProfiler);
                ImGui::MenuItem("Show Game Colliders", nullptr, &showGameColliders);
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Help")) {
                auto openManual = [&](const char* page) {
                    // Release packages place docs beside the executable, independent of project
                    // cwd.
                    const char* base = SDL_GetBasePath();
                    auto path = std::filesystem::path(base ? base : ".") / "docs" / page;
                    if (!std::filesystem::is_regular_file(path)) {
                        SDL_LogError(
                            SDL_LOG_CATEGORY_APPLICATION,
                            "%s",
                            "Offline documentation missing. Build docs with python docs/build.py --output build/docs.");
                        return;
                    }
                    auto raw = std::filesystem::absolute(path).generic_string();
                    std::string url = "file://";
                    if (raw.empty() || raw.front() != '/') {
                        url += '/';
                    }
                    for (unsigned char c : raw) {
                        if (std::isalnum(c) || c == '/' || c == ':' || c == '-' || c == '_' ||
                            c == '.' || c == '~') {
                            url += static_cast<char>(c);
                        } else {
                            const char* hex = "0123456789ABCDEF";
                            url += '%';
                            url += hex[c >> 4];
                            url += hex[c & 15];
                        }
                    }
                    if (!SDL_OpenURL(url.c_str())) {
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", SDL_GetError());
                    }
                };
                if (ImGui::MenuItem("Documentation")) {
                    openManual("index.html");
                }
                if (ImGui::MenuItem("Scripting Reference")) {
                    openManual("scripting-reference.html");
                }
                if (ImGui::MenuItem("GitHub")) {
                    SDL_OpenURL("https://github.com/monsef64/LegacyEngine");
                }
                showAbout = ImGui::MenuItem("About");
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        if (renameLevelPopup) {
            std::snprintf(renameLevelName,
                          sizeof(renameLevelName),
                          "%s",
                          levels.current().empty() ? "Main" : levels.current().c_str());
            ImGui::OpenPopup("Rename Level");
            renameLevelPopup = false;
        }
        if (ImGui::BeginPopupModal("Rename Level", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            const bool enter = ImGui::InputText("Name",
                                                renameLevelName,
                                                sizeof(renameLevelName),
                                                ImGuiInputTextFlags_EnterReturnsTrue);
            if (ImGui::Button("Rename") || enter) {
                try {
                    scripts.saveFile(browser.assets);
                    levels.saveCurrent(world, embeddedLegacySource, camera);
                    levels.renameCurrent(renameLevelName);
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) {
                    levels.error = e.what();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            if (!levels.error.empty()) {
                ImGui::TextWrapped("%s", levels.error.c_str());
            }
            ImGui::EndPopup();
        }
        if (newLevelPopup) {
            ImGui::OpenPopup("New Level");
            newLevelPopup = false;
        }
        if (ImGui::BeginPopupModal("New Level", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("The current level is saved before creating an empty level.");
            ImGui::InputText("Name", newLevelName, sizeof(newLevelName));
            if (ImGui::Button("Create")) {
                try {
                    scripts.saveFile(browser.assets);
                    levels.saveCurrent(world, embeddedLegacySource, camera);
                    levels.create(newLevelName);
                    levels.load(newLevelName);
                    ImGui::CloseCurrentPopup();
                } catch (const std::exception& e) {
                    levels.error = e.what();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }
            if (!levels.error.empty()) {
                ImGui::TextWrapped("%s", levels.error.c_str());
            }
            ImGui::EndPopup();
        }
        if (renderingSettings.visible) {
            ImGui::SetNextWindowSize(ImVec2(540, 320), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSizeConstraints(ImVec2(360, 220),
                                                ImGui::GetMainViewport()->WorkSize);
            if (ImGui::Begin("Rendering", &renderingSettings.visible)) {
                ImGui::SeparatorText("Rendering");
                if (ImGui::Checkbox("Enable VSync", &renderingSettings.vsync)) {
                    const bool after = renderingSettings.vsync;
                    renderingSettings.vsync = !after;
                    history.perform(world, selectedEntity, [&, after](bool forward) {
                        renderingSettings.vsync = forward ? after : !after;
                        renderingSettings.apply();
                        renderingSettings.save(browser.assets);
                        scripts.console().message(forward ? "Set/Redo VSync setting"
                                                          : "Undo VSync setting");
                    });
                }
#ifdef LEGACY_FSR3_GAME
                int gameQuality = renderingSettings.fsr3GameQuality;
                if (ImGui::Combo(
                        "Game upscaling (experimental)",
                        &gameQuality,
                        "Off\0FSR 3 Native AA\0FSR 3 Quality\0FSR 3 Balanced\0FSR 3 Performance\0FSR 3 Ultra Performance\0")) {
                    const int before = renderingSettings.fsr3GameQuality;
                    history.perform(world, selectedEntity, [&, before, gameQuality](bool forward) {
                        renderingSettings.fsr3GameQuality = forward ? gameQuality : before;
                        renderingSettings.save(browser.assets);
                    });
                }
                ImGui::TextWrapped("Game FSR: %s", fsrStatus.c_str());
#else
                ImGui::TextWrapped(
                    "Game FSR 3 is unavailable in this build; native rendering is used.");
#endif
                const auto shadowsBefore = renderingSettings.shadows;
                bool shadowChanged = ImGui::Combo("Shadow mapping",
                                                  &renderingSettings.shadows.mode,
                                                  "Cascaded (CSM)\0Single-map PCF (PCM)\0");
                int filtering = renderingSettings.shadows.soft ? 1 : 0;
                if (ImGui::Combo("Shadow filtering", &filtering, "Hard\0Soft (PCF)\0")) {
                    renderingSettings.shadows.soft = filtering == 1;
                    shadowChanged = true;
                }
                const int sizes[]{256, 512, 1024, 2048};
                int resolution = 0;
                for (int i = 0; i < 4; ++i) {
                    if (sizes[i] == renderingSettings.shadows.resolution) {
                        resolution = i;
                    }
                }
                if (ImGui::Combo(
                        "Shadow resolution", &resolution, "256\000512\0001024\0002048\0")) {
                    renderingSettings.shadows.resolution = sizes[resolution];
                    shadowChanged = true;
                }
                if (shadowChanged) {
                    const auto after = renderingSettings.shadows;
                    renderingSettings.shadows = shadowsBefore;
                    history.perform(world, selectedEntity, [&, shadowsBefore, after](bool forward) {
                        renderingSettings.shadows = forward ? after : shadowsBefore;
                        renderingSettings.save(browser.assets);
                        scripts.console().message(forward ? "Set/Redo shadow settings"
                                                          : "Undo shadow settings");
                    });
                }
                ImGui::TextWrapped(
                    "CSM uses four directional cascades. Point/spot lights retain PCF. Light quality scales the project resolution; Off disables that light's shadows.");
                if (renderingSettings.presentationStatus) {
                    const auto status = renderingSettings.presentationStatus();
                    if (!status.empty()) {
                        ImGui::TextUnformatted(status.c_str());
                    }
                }
                ImGui::TextWrapped(
                    "Disable VSync to measure uncapped performance. Use Debug > Profiler to distinguish rendering cost from presentation wait.");
                if (!renderingSettings.error.empty()) {
                    ImGui::TextWrapped("%s", renderingSettings.error.c_str());
                }
            }
            ImGui::End();
        }
        if (layerSettings.visible) {
            const auto area = ImGui::GetMainViewport()->WorkSize;
            ImGui::SetNextWindowSize(ImVec2(std::min(460.f, area.x), std::min(420.f, area.y)),
                                     ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSizeConstraints(
                ImVec2(std::min(320.f, area.x), std::min(240.f, area.y)), area);
            if (ImGui::Begin("Layers", &layerSettings.visible)) {
                ImGui::TextUnformatted("Default (built-in)");
                for (int i = 1; i < 32; ++i) {
                    if (!LayerSettings::names[i].empty()) {
                        ImGui::PushID(i);
                        ImGui::TextUnformatted(LayerSettings::names[i].c_str());
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Remove")) {
                            LayerSettings::names[i].clear();
                            world.each<ObjectLayer>([&](Entity, ObjectLayer& layer) {
                                if (layer.value == i) {
                                    layer.value = 0;
                                }
                            });
                            world.each<Raycast>(
                                [&](Entity, Raycast& query) { query.layerMask &= ~(1u << i); });
                            layerSettings.save(browser.assets);
                        }
                        ImGui::PopID();
                    }
                }
                static char newLayer[65]{};
                ImGui::SetNextItemWidth(std::max(60.f, ImGui::GetContentRegionAvail().x - 50));
                ImGui::InputText("##LayerName", newLayer, sizeof(newLayer));
                ImGui::SameLine();
                if (ImGui::Button("Add") && newLayer[0]) {
                    if (LayerSettings::find(newLayer) >= 0) {
                        layerSettings.error = "Layer already exists";
                    } else {
                        int slot = 1;
                        while (slot < 32 && !LayerSettings::names[slot].empty()) {
                            ++slot;
                        }
                        if (slot == 32) {
                            layerSettings.error = "Maximum 32 layers";
                        } else {
                            LayerSettings::names[slot] = newLayer;
                            newLayer[0] = 0;
                            layerSettings.save(browser.assets);
                        }
                    }
                }
                if (!layerSettings.error.empty()) {
                    ImGui::TextWrapped("%s", layerSettings.error.c_str());
                }
            }
            ImGui::End();
        }
        if (showAbout) {
            ImGui::OpenPopup("About LegacyEngine");
        }
        if (ImGui::BeginPopupModal(
                "About LegacyEngine", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("LegacyEngine");
            ImGui::TextUnformatted("3D scene editor with LegacyScript (Lua 5.4)");
            ImGui::TextUnformatted("SDL3 / Vulkan / Dear ImGui / Jolt Physics");
            if (ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        drawProfiler(profiler, showProfiler);
        // Let text widgets own editing shortcuts while typing. Global routing
        // otherwise makes shortcuts independent of which editor panel has focus.
        if (editAllowed && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive()) {
            const auto flags = ImGuiInputFlags_RouteGlobal;
            undoRequested |= ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, flags);
            redoRequested |= ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, flags) ||
                             ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, flags);
            duplicateRequested |= ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D, flags);
            copyRequested |= ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C, flags);
            pasteRequested |= ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V, flags);
            parentRequested |= ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_P, flags);
        }
        ImGui::SetNextWindowPos(ImVec2(layout.toolbar.x, layout.toolbar.y));
        ImGui::SetNextWindowSize(ImVec2(layout.toolbar.width, layout.toolbar.height));
        ImGui::Begin("Scene controls",
                     nullptr,
                     fixedPanelFlags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDocking |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (editAllowed) {
            if (copyRequested && objectSelected) {
                clipboard.copy(world, selectedEntity);
            }
            if ((duplicateRequested && objectSelected) ||
                (pasteRequested && clipboard.available())) {
                editor.finish(world);
                clicking = false;
                history.commit(world, selectedEntity);
                selectedEntity = duplicateRequested ? duplicateLevelObject(world, selectedEntity)
                                                    : clipboard.paste(world);
                history.commit(world, selectedEntity);
            }
            if (parentRequested && objectSelected) {
                editor.finish(world);
                clicking = false;
                if (getParent(world, selectedEntity) != Entity{}) {
                    history.commit(world, selectedEntity);
                    setParent(world, selectedEntity, {});
                    history.commit(world, selectedEntity);
                } else {
                    ImGui::OpenPopup("Choose parent");
                }
            }
        }
        if (ImGui::BeginPopup("Choose parent")) {
            ImGui::TextUnformatted("Choose parent for selected GameObject");
            ImGui::Separator();
            for (const auto& object : levelObjects(world)) {
                if (!editAllowed || !canParent(world, selectedEntity, object.entity)) {
                    continue;
                }
                ImGui::PushID(static_cast<int>(object.entity.index));
                if (ImGui::Selectable(object.name.c_str())) {
                    history.commit(world, selectedEntity);
                    setParent(world, selectedEntity, object.entity);
                    history.commit(world, selectedEntity);
                }
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
        if ((undoRequested || redoRequested) && !playMode.active()) {
            editor.finish(world);
            clicking = false;
            history.commit(world, selectedEntity);
            const bool restored = undoRequested ? history.undo(world, selectedEntity)
                                                : history.redo(world, selectedEntity);
            if (restored) {
                scripts.console().message(undoRequested ? "Undo editor change"
                                                        : "Redo editor change");
                physics.reset();
            } else if (!history.error.empty()) {
                projectError = history.error;
                history.error.clear();
            }
        }
        const bool playbackKeys = !ImGui::GetIO().WantTextInput && !editor.dragging() &&
                                  !projectDialog.pending() && !browser.dialog.pending();
        const bool playKey =
            (smokeTest && renderedFrames == 35) ||
            (playbackKeys && ImGui::Shortcut(ImGuiKey_F5, ImGuiInputFlags_RouteGlobal));
        const bool pauseKey =
            playbackKeys && ImGui::Shortcut(ImGuiKey_F6, ImGuiInputFlags_RouteGlobal);
        const bool stopKey =
            playbackKeys && ImGui::Shortcut(ImGuiKey_F7, ImGuiInputFlags_RouteGlobal);
        const float playbackWidth = 90 + 2 * ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorPosX((layout.toolbar.width - playbackWidth) * 0.5f);
        ImGui::BeginDisabled(prefabLevel || playMode.active() || levels.busy());
        if (playbackButton("##Play", "Play (F5)", PlaybackIcon::Play, playMode.active()) ||
            (playKey && !prefabLevel && !playMode.active() && !levels.busy())) {
            editor.finish(world);
            clicking = false;
            try {
                scripts.saveFile(browser.assets);
                levels.saveCurrent(world, embeddedLegacySource, camera);
            } catch (const std::exception& e) {
                levels.error = e.what();
            }
            editSelection = selectedEntity;
            editLevelName = levels.current();
            editSource = scripts.source();
            editScriptPath = browser.openedScript;
            editLegacySource = embeddedLegacySource;
            editCamera = camera;
            history.commit(world, selectedEntity);
            editHistory = std::move(history);
            history = EditorHistory(world, selectedEntity);
            if (playMode.play(world)) {
                focusGame = true;
                if (separateGame && !gameWindow) {
                    gameWindow = SDL_CreateWindow(
                        "Game", 960, 540, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
                    if (!gameWindow) {
                        projectError = SDL_GetError();
                    } else {
                        try {
                            gameContext = std::make_unique<VulkanContext>(
                                gameWindow, context, renderingSettings.vsync);
                        } catch (const std::exception& error) {
                            projectError = error.what();
                            SDL_DestroyWindow(gameWindow);
                            gameWindow = nullptr;
                        }
                    }
                }
                panelOpen[6] = true;
                physics.reset();
                audio.start(world);
                scripts.setAssetContext(browser.openedScript, selectedEntity);
                scripts.console().setAssetRoot(browser.assets.root);
                scripts.startPlay();
                const MeshHandle runtimeMeshes[]{cubeMesh, sphereMesh, capsuleMesh, coneMesh};
                componentScripts.start(world, browser.assets, runtimeMeshes, planeMesh);
                LuaConsole::postEvent("onLevelLoaded", {levels.current()});
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!playMode.active());
        if (playbackButton("##Pause",
                           playMode.state() == PlayMode::State::Paused ? "Resume (F6)"
                                                                       : "Pause (F6)",
                           PlaybackIcon::Pause,
                           playMode.state() == PlayMode::State::Paused) ||
            (pauseKey && playMode.active())) {
            editor.finish(world);
            clicking = false;
            playMode.pause();
            if (!playMode.playing()) {
                input.releaseMouse();
            }
        }
        ImGui::SameLine();
        if (playbackButton("##Stop", "Stop (F7)", PlaybackIcon::Stop) ||
            (stopKey && playMode.active())) {
            editor.finish(world);
            clicking = false;
            // Run stop callbacks before restoring, so their edits are discarded too.
            levels.suspended = true;
            levels.cancel();
            LuaConsole::clearEvents();
            audio.clear();
            componentScripts.stop();
            scripts.shutdown();
            LuaConsole::clearEvents();
            if (gameWindow) {
                input.releaseMouse();
                input.window = window;
                gameContext.reset();
                SDL_DestroyWindow(gameWindow);
                gameWindow = nullptr;
            }
            mouseOver = {};
            mousePressedObject = {};
            playMode.stop(world);
            input.releaseMouse();
            physics.reset();
            scripts.resetRuntime();
            scripts.setSource(editSource);
            embeddedLegacySource = editLegacySource;
            browser.openedScript = editScriptPath;
            scripts.setAssetContext(editScriptPath, editSelection);
            camera = editCamera;
            history = std::move(editHistory);
            levels.commit(editLevelName, false);
            levels.suspended = false;
            selectedEntity = world.alive(editSelection) ? editSelection : Entity{};
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(playMode.active());
        ImGui::Checkbox("Separate Game window", &separateGame);
        ImGui::EndDisabled();
        if (gameWindow) {
            ImGui::SameLine();
            const bool fullscreen = (SDL_GetWindowFlags(gameWindow) & SDL_WINDOW_FULLSCREEN) != 0;
            if (ImGui::Button(fullscreen ? "Windowed Game" : "Fullscreen Game")) {
                toggleGameFullscreen();
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Toggle separate Game fullscreen (F11 or Alt+Enter in Game window)");
            }
        }
        ImGui::End();
        bool hierarchyFocused = false;
        int hierarchyCreate = -1;
        const std::string hierarchyLevelName =
            prefabLevel                 ? "Prefab: " + editingPrefab
            : !levels.current().empty() ? levels.current()
            : currentProject.empty()    ? "Untitled"
                                        : std::filesystem::path(currentProject).stem().string();
        if (drawLevelObjects(world,
                             selectedEntity,
                             true,
                             &layout.hierarchy,
                             &hierarchyFocused,
                             &hierarchyCreate,
                             hierarchyLevelName.c_str(),
                             editAllowed && !prefabLevel ? &renameLevelPopup : nullptr)) {
            editor.finish(world);
            clicking = false;
        }
        if (hierarchyCreate >= 0) {
            createObject(hierarchyCreate);
        }
        static Entity lastInspectedEntity;
        if (selectedEntity != lastInspectedEntity ||
            (hierarchyFocused && ImGui::IsMouseClicked(0))) {
            browser.selected.clear();
            lastInspectedEntity = selectedEntity;
        }
        const MeshHandle meshOptions[]{cubeMesh, sphereMesh, capsuleMesh, coneMesh, planeMesh};
        drawInspector(world,
                      selectedEntity,
                      layout.inspector,
                      meshOptions,
                      editor.dragging(),
                      &browser.assets,
                      5,
                      browser.selected);
        scripts.componentOutput = componentScripts.messages();
        scripts.setAssetContext(browser.openedScript, selectedEntity);
        scripts.draw(&layout, playMode.playing(), &browser.assets, &browser.openedScript);
        browser.projectFolder = currentProject.empty()
                                    ? browser.assets.root
                                    : std::filesystem::absolute(currentProject).parent_path();
        browser.draw(window, world, selectedEntity, scripts, editAllowed, &history);
        if (!browser.movedAsset.first.empty()) {
            const auto moved = std::exchange(browser.movedAsset, {});
            animationWindow.remapAssetPaths(moved.first, moved.second);
            modelPreviewWindow.visible = false;
        }

        if (!browser.previewModelRequest.empty()) {
            modelPreviewWindow.open(
                browser.assets, std::exchange(browser.previewModelRequest, {}), editorPreviews);
        }
        modelPreviewWindow.draw(browser.assets, editorPreviews, ImGui::GetIO().DeltaTime);
        if (!browser.editAnimationRequest.empty()) {
            animationWindow.open(browser.assets, std::exchange(browser.editAnimationRequest, {}));
        }
        animationWindow.draw(world,
                             selectedEntity,
                             browser.assets,
                             editorPreviews,
                             history,
                             editAllowed,
                             ImGui::GetIO().DeltaTime);
        if (!browser.editCubemapRequest.empty()) {
            const auto path = std::exchange(browser.editCubemapRequest, {});
            try {
                cubemapDraft = loadCubemap(browser.assets, path);
                editingCubemap = path;
            } catch (const std::exception& e) {
                browser.assets.error = e.what();
            }
        }
        if (!editingCubemap.empty()) {
            bool open = true;
            ImGui::SetNextWindowSize(ImVec2(540, 340), ImGuiCond_FirstUseEver);
            ImGui::Begin("Cubemap Editor", &open);
            ImGui::TextWrapped("%s", editingCubemap.c_str());
            ImGui::TextWrapped(
                "Assign six square textures of matching size. Front is -Z, back is +Z.");
            for (int i = 0; i < 6; ++i) {
                auto& path = cubemapDraft.faces[i];
                assetPicker(CubemapAsset::names[i],
                            path,
                            &browser.assets,
                            [](const std::string& file) { return materialTexturePath(file); });
                if (ImGui::BeginDragDropTarget()) {
                    if (const auto* drop = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                        std::string value = static_cast<const char*>(drop->Data);
                        if (materialTexturePath(value)) {
                            path = value;
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
            }
            if (ImGui::Button("Save Cubemap")) {
                try {
                    saveCubemap(browser.assets, editingCubemap, cubemapDraft);
                } catch (const std::exception& e) {
                    browser.assets.error = e.what();
                }
            }
            ImGui::End();
            if (!open) {
                editingCubemap.clear();
            }
        }
        if (browser.prefabCreation != Entity{} && !playMode.active()) {
            const auto source = browser.prefabCreation;
            browser.prefabCreation = {};
            try {
                const auto* name = world.tryGet<ObjectName>(source);
                std::string stem = name ? name->value : "Prefab";
                for (auto& c : stem) {
                    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '-' && c != '_') {
                        c = '_';
                    }
                }
                if (stem.empty()) {
                    stem = "Prefab";
                }
                auto relative = (std::filesystem::path(browser.currentFolder) / (stem + ".prefab"))
                                    .generic_string();
                for (unsigned n = 2; std::filesystem::exists(browser.assets.resolve(relative));
                     ++n) {
                    relative = (std::filesystem::path(browser.currentFolder) /
                                (stem + "_" + std::to_string(n) + ".prefab"))
                                   .generic_string();
                }
                Prefabs::save(browser.assets, relative, world, source, projectMeshes);
                history.commit(world, selectedEntity);
                world.add(source, PrefabInstance{relative});
                history.commit(world, selectedEntity);
                browser.selected = relative;
            } catch (const std::exception& e) {
                browser.assets.error = e.what();
            }
        }
        if (!browser.spawnModelRequest.empty() && !playMode.active()) {
            const auto path = std::exchange(browser.spawnModelRequest, {});
            try {
                const auto document = modelDocument(browser.assets, path);
                struct Part {
                    std::string name, mesh, material;
                    MeshData data;
                };
                std::vector<Part> parts;
                if (document.at("version") == 2) {
                    if (!document.at("parts").is_array() || document.at("parts").empty() ||
                        document.at("parts").size() > 1024) {
                        throw std::runtime_error("Invalid model parts");
                    }
                    for (const auto& part : document.at("parts")) {
                        const auto mesh = part.at("mesh").get<std::string>();
                        parts.push_back({part.at("name").get<std::string>(),
                                         mesh,
                                         part.at("material").get<std::string>(),
                                         loadModelAsset(browser.assets, mesh)});
                    }
                } else {
                    parts.push_back({"Mesh", path, "", loadModelAsset(browser.assets, path)});
                }
                std::size_t totalVertices = 0;
                for (const auto& part : parts) {
                    totalVertices += part.data.vertices.size();
                    if (part.name.size() > 4096 ||
                        (!part.material.empty() && !materialAsset(part.material))) {
                        throw std::runtime_error("Invalid model metadata");
                    }
                }
                if (totalVertices > maxModelVertices ||
                    world.entities().size() + parts.size() + 1 > ProjectIO::maxObjects) {
                    throw std::runtime_error("Model placement exceeds scene/model limits");
                }
                editor.finish(world);
                const auto root = world.create();
                world.add(root, ObjectName{std::filesystem::path(path).stem().string()});
                world.add(root, Transform{camera.target});
                if (document.contains("animations") && !document.at("animations").empty()) {
                    const auto firstPart = document.at("parts").at(0).at("mesh").get<std::string>();
                    Animation animation;
                    animation.clip =
                        (std::filesystem::path(firstPart).parent_path() / "Animation_0.leanim")
                            .generic_string();
                    world.add(root, animation);
                }
                if (prefabLevel) {
                    setParent(world, root, Prefabs::root(world));
                }
                for (const auto& part : parts) {
                    const auto child = world.create();
                    world.add(child, ObjectName{part.name});
                    world.add(child, Transform{camera.target});
                    setParent(world, child, root);
                    MeshRenderer mesh{cubeMesh};
                    mesh.model = part.mesh;
                    mesh.material = part.material;
                    world.add(child, mesh);
                }
                selectedEntity = root;
                browser.selected.clear();
            } catch (const std::exception& e) {
                browser.assets.error = e.what();
            }
        }
        if (!browser.spawnPrefabRequest.empty() && !playMode.active()) {
            const auto path = std::exchange(browser.spawnPrefabRequest, {});
            try {
                history.commit(world, selectedEntity);
                const auto parent = prefabLevel ? Prefabs::root(world) : Entity{};
                selectedEntity = Prefabs::spawn(browser.assets, path, world, projectMeshes, {}, {});
                if (parent != Entity{}) {
                    setParent(world, selectedEntity, parent);
                }
                history.commit(world, selectedEntity);
            } catch (const std::exception& e) {
                browser.assets.error = e.what();
            }
        }
        if (!browser.editPrefabRequest.empty() && !playMode.active() && !prefabLevel &&
            !levels.busy()) {
            const auto path = std::exchange(browser.editPrefabRequest, {});
            try {
                auto loaded = Prefabs::load(browser.assets, path, projectMeshes);
                editor.finish(world);
                scripts.shutdown();
                prefabLevel = world.snapshot();
                prefabLevelSelection = selectedEntity;
                prefabLevelCamera = camera;
                prefabHistory.emplace(std::move(history));
                levels.suspended = true;
                world.restore(*loaded);
                selectedEntity = Prefabs::root(world);
                history = EditorHistory(world, selectedEntity);
                editingPrefab = path;
                physics.reset();
                scripts.resetRuntime();
                camera.focus(world.get<Transform>(selectedEntity).position, 3);
                ImGui::SetWindowFocus("Scene");
            } catch (const std::exception& e) {
                browser.assets.error = e.what();
            }
        }
        if (prefabLevel) {
            ImGui::SetNextWindowSize(ImVec2(440, 180), ImGuiCond_FirstUseEver);
            ImGui::Begin("Prefab Window");
            ImGui::TextWrapped("Editing %s", editingPrefab.c_str());
            ImGui::TextWrapped(
                "Hierarchy and Scene contain only this prefab. Save updates linked instances; Exit restores the level. Unsaved edits are discarded on Exit.");
            if (ImGui::Button("Save Prefab") ||
                (!ImGui::GetIO().WantTextInput &&
                 ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal))) {
                try {
                    auto root = Prefabs::root(world);
                    Prefabs::save(browser.assets, editingPrefab, world, root, projectMeshes);
                    Prefabs::refresh(browser.assets, editingPrefab, *prefabLevel, projectMeshes);
                    Prefabs::refreshSavedLevels(browser.assets, editingPrefab, projectMeshes);
                    scripts.console().message("Saved prefab and refreshed level instances");
                } catch (const std::exception& e) {
                    browser.assets.error = e.what();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Exit Prefab Mode")) {
                editor.finish(world);
                scripts.shutdown();
                world.restore(*prefabLevel);
                scripts.resetRuntime();
                prefabLevel.reset();
                selectedEntity = prefabLevelSelection;
                camera = prefabLevelCamera;
                history = std::move(*prefabHistory);
                prefabHistory.reset();
                history.commit(world, selectedEntity);
                editingPrefab.clear();
                levels.suspended = false;
                physics.reset();
            }
            ImGui::End();
        }
        const auto settingsBefore = browser.settings;
        browser.settings.draw(browser.projectFolder);
        if (settingsBefore.signature() != browser.settings.signature() && !settingsGesture) {
            settingsGesture = settingsBefore;
        }
        if (!playMode.active() && settingsGesture && !ImGui::IsAnyItemActive()) {
            const auto before = *settingsGesture;
            settingsGesture.reset();
            const auto after = browser.settings;
            browser.settings = before;
            history.perform(world, selectedEntity, [&, before, after](bool forward) {
                const bool visible = browser.settings.visible;
                browser.settings = forward ? after : before;
                browser.settings.visible = visible;
                applyEditorPalette(browser.settings.theme);
                browser.settings.save();
                scripts.console().message(forward ? "Set/Redo editor settings"
                                                  : "Undo editor settings");
            });
        }
        const auto buttonsBefore = input.buttons;
        const auto alternativesBefore = input.alternatives;
        inputSettings.draw(browser.assets, input, editAllowed);
        if (editAllowed &&
            (buttonsBefore != input.buttons || alternativesBefore != input.alternatives)) {
            const auto buttonsAfter = input.buttons;
            const auto alternativesAfter = input.alternatives;
            input.buttons = buttonsBefore;
            input.alternatives = alternativesBefore;
            history.perform(world,
                            selectedEntity,
                            [&, buttonsBefore, alternativesBefore, buttonsAfter, alternativesAfter](
                                bool forward) {
                                input.buttons = forward ? buttonsAfter : buttonsBefore;
                                input.alternatives =
                                    forward ? alternativesAfter : alternativesBefore;
                                inputSettings.save(browser.assets, input);
                                scripts.console().message(forward ? "Set/Redo input settings"
                                                                  : "Undo input settings");
                            });
        }
        terminalErrors.report("Skybox", renderer.skyboxError);
        terminalErrors.report("Material", renderer.materialError);
        terminalErrors.report("Shadow", renderer.shadowError);
        terminalErrors.report("Audio", audio.error);
        terminalErrors.report("Assets", browser.assets.error);
        terminalErrors.report("Asset preview", browser.preview.error);
        terminalErrors.report("Editor settings", browser.settings.error);
        terminalErrors.report("Input settings", inputSettings.error);
        terminalErrors.report("History", history.error);
        terminalErrors.report("Levels", levels.error);
        terminalErrors.report("Rendering settings", renderingSettings.error);
        ImDrawList* sceneDrawList = nullptr;
        bool sceneVisible = false, sceneFocused = false;
        sceneHovered = false;
        sceneRect = {};
        orientationInteracting = false;
        if (panelOpen[2]) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            if (beginEditorPanel("Scene",
                                 0,
                                 ImGuiWindowFlags_NoScrollbar |
                                     ImGuiWindowFlags_NoScrollWithMouse)) {
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 3));
                ImGui::BeginChild("Scene toolbar",
                                  ImVec2(0,
                                         (ImGui::GetContentRegionAvail().x < 620 ? 2.f : 1.f) *
                                                 ImGui::GetFrameHeightWithSpacing() +
                                             6),
                                  ImGuiChildFlags_AlwaysUseWindowPadding,
                                  ImGuiWindowFlags_NoScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse);
                editor.drawToolbar();
                ImGui::EndChild();
                ImGui::PopStyleVar();
                sceneFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
                const auto pos = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
                sceneRect = {pos.x, pos.y, std::max(size.x, 1.0f), std::max(size.y, 1.0f)};
                layout.scene = sceneRect;
                sceneVisible = true;
                sceneDrawList = ImGui::GetWindowDrawList();
                int dw = 0, dh = 0;
                SDL_GetWindowSizeInPixels(window, &dw, &dh);
                auto pixels = scenePixels(sceneRect, windowWidth, windowHeight, dw, dh);
                sceneSurface.prepare(pixels.width, pixels.height);
                ImGui::Image(gui.texture(sceneSurface), ImVec2(sceneRect.width, sceneRect.height));
                if (world.has<Camera>(selectedEntity) && world.has<Transform>(selectedEntity)) {
                    const auto& selectedCamera = world.get<Camera>(selectedEntity);
                    const float aspect = std::clamp(selectedCamera.aspect, .25f, 4.f);
                    float width = std::min(240.f, sceneRect.width * .35f);
                    float height = width / aspect;
                    if (height > sceneRect.height * .4f) {
                        height = sceneRect.height * .4f;
                        width = height * aspect;
                    }
                    cameraPreviewSurface.prepare(std::max(1, int(width)), std::max(1, int(height)));
                    const ImVec2 a(sceneRect.x + sceneRect.width - width - 10,
                                   sceneRect.y + sceneRect.height - height - 10),
                        b(a.x + width, a.y + height);
                    sceneDrawList->AddRectFilled(ImVec2(a.x - 2, a.y - 20),
                                                 ImVec2(b.x + 2, b.y + 2),
                                                 IM_COL32(24, 24, 24, 245));
                    sceneDrawList->AddText(ImVec2(a.x, a.y - 18), IM_COL32_WHITE, "Camera preview");
                    sceneDrawList->AddImage(gui.texture(cameraPreviewSurface), a, b);
                }
                sceneHovered =
                    ImGui::IsWindowHovered(ImGuiHoveredFlags_None) && ImGui::IsItemHovered();
                if (sceneHovered && ImGui::IsMouseClicked(0)) {
                    browser.selected.clear();
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const auto* asset = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                        const std::string path = static_cast<const char*>(asset->Data);
                        if (modelAsset(path)) {
                            browser.spawnModelRequest = path;
                        } else if (Prefabs::asset(path)) {
                            browser.spawnPrefabRequest = path;
                        } else {
                            attachScriptAsset(world, selectedEntity, path);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                if (editAllowed && (sceneFocused || sceneHovered) && !cameraLooking &&
                    !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
                    !cameraModifiersHeld(SDL_GetModState()) &&
                    !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) {
                    if (ImGui::IsKeyPressed(ImGuiKey_W, false)) {
                        editor.setTool(TransformTool::Move);
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
                        editor.setTool(TransformTool::Rotate);
                    }
                    if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
                        editor.setTool(TransformTool::Scale);
                    }
                }
                orientationInteracting = drawSceneOrientation(
                    camera, sceneRect, editAllowed && !cameraLooking && !editor.dragging());
                editor.draw(world,
                            selectedEntity,
                            !playMode.active() && !projectDialog.pending() &&
                                !browser.dialog.pending() && !orientationInteracting &&
                                !cameraLooking && !(SDL_GetModState() & SDL_KMOD_ALT),
                            camera,
                            static_cast<float>(sceneSurface.width()) / sceneSurface.height(),
                            &sceneRect,
                            sceneDrawList,
                            &browser.assets);
                if (editor.capturesMouse() || orientationInteracting) {
                    clicking = false;
                }
            }
            ImGui::End();
            ImGui::PopStyleVar();
        }
        bool gameVisible = false;
        gameHovered = false;
        gameFocused = false;
        if (panelOpen[6] && !gameWindow) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            if (focusGame || (smokeTest && renderedFrames == 0)) {
                focusGame = false;
                ImGui::SetNextWindowFocus();
            }
            if (beginEditorPanel("Game")) {
                gameHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
                gameFocused = ImGui::IsWindowFocused();
                const auto active = activeCamera(world);
                if (active == Entity{}) {
                    const char* message = "No active camera available";
                    const auto size = ImGui::GetContentRegionAvail();
                    const auto textSize = ImGui::CalcTextSize(message);
                    const auto origin = ImGui::GetCursorPos();
                    ImGui::SetCursorPos(
                        ImVec2(origin.x + std::max(0.0f, (size.x - textSize.x) * 0.5f),
                               origin.y + std::max(0.0f, (size.y - textSize.y) * 0.5f)));
                    ImGui::TextUnformatted(message);
                } else {
                    auto size = ImGui::GetContentRegionAvail();
                    size.x = std::max(size.x, 1.0f);
                    size.y = std::max(size.y, 1.0f);
                    int dw = 0, dh = 0;
                    SDL_GetWindowSizeInPixels(window, &dw, &dh);
                    gameSurface.prepare(static_cast<int>(size.x * dw / std::max(windowWidth, 1)),
                                        static_cast<int>(size.y * dh / std::max(windowHeight, 1)));
                    world.each<Camera>([&](Entity, Camera& c) {
                        c.aspect = static_cast<float>(gameSurface.width()) / gameSurface.height();
                    });
                    gameImageOrigin = ImGui::GetCursorScreenPos();
                    gameImageSize = size;
                    ImGui::Image(gui.texture(gameSurface), size);
                    gameVisible = true;
                    if (showFPS) {
                        const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                        char label[64];
                        std::snprintf(label, sizeof(label), "FPS: %.0f", ImGui::GetIO().Framerate);
                        const auto text = ImGui::CalcTextSize(label);
                        const ImVec2 position(a.x + 8, a.y + 8);
                        auto* draw = ImGui::GetWindowDrawList();
                        draw->PushClipRect(a, b, true);
                        draw->AddRectFilled(
                            ImVec2(position.x - 4, position.y - 3),
                            ImVec2(position.x + text.x + 4, position.y + text.y + 3),
                            IM_COL32(0, 0, 0, 180),
                            3);
                        draw->AddText(position, IM_COL32(255, 255, 255, 255), label);
                        draw->PopClipRect();
                    }
                }
            }
            ImGui::End();
            ImGui::PopStyleVar();
        }
        if (!sceneVisible) {
            editor.finish(world);
            layout.scene = {};
        }
        if (editAllowed && sceneVisible && (hierarchyFocused || sceneFocused) && !cameraLooking &&
            !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
            !cameraModifiersHeld(SDL_GetModState()) &&
            !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
            ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            if (const auto* transform = world.tryGet<Transform>(selectedEntity)) {
                editor.finish(world);
                clicking = false;
                const auto& scale = transform->scale;
                float radius =
                    std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z), 1.0f});
                Vec3 centre = transform->position;
                if (const auto* collider = world.tryGet<Collider>(selectedEntity)) {
                    const auto resolved = effectiveCollider(world, selectedEntity, *collider);
                    radius = .25f;
                    if (resolved.shape == ColliderShape::Mesh && !resolved.model.empty()) {
                        try {
                            for (const auto& vertex :
                                 loadModelAsset(browser.assets, resolved.model).vertices) {
                                const auto p = (vertex.position + resolved.center) * scale;
                                radius = std::max(radius, std::sqrt(dot(p, p)));
                            }
                        } catch (const std::exception& e) {
                            browser.assets.error = e.what();
                        }
                    } else {
                        const auto bounds = colliderWireframe(resolved, scale);
                        for (const auto& vertex : bounds.vertices) {
                            radius =
                                std::max(radius, std::sqrt(dot(vertex.position, vertex.position)));
                        }
                    }
                }
                camera.focus(centre, radius, sceneRect.width / sceneRect.height);
            }
        }
        if (editAllowed && (hierarchyFocused || sceneFocused) && world.alive(selectedEntity) &&
            !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
            !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
            ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
            editor.finish(world);
            clicking = false;
            history.commit(world, selectedEntity);
            deleteHierarchySelection(world, selectedEntity);
            selectedEntity = {};
            history.commit(world, selectedEntity);
            physics.reset();
        }
        if (!playMode.active()) {
            updateHierarchy(world);
            history.observe(world,
                            selectedEntity,
                            editor.dragging() || ImGui::IsAnyItemActive() ||
                                ImGui::IsMouseDown(ImGuiMouseButton_Left));
        }
        ImGui::EndDisabled();
        if (renderedFrames == 0) {
            ImGui::SetWindowFocus("Assets");
        }
        ImTextureID gameTexture = ImTextureID_Invalid;
        if (gameWindow && playMode.active()) {
            int gw = 0, gh = 0;
            SDL_GetWindowSizeInPixels(gameWindow, &gw, &gh);
            gameSurface.prepare(std::max(1, gw), std::max(1, gh));
            gameVisible = true;
            const auto active = activeCamera(world);
            if (active != Entity{}) {
                world.get<Camera>(active).aspect = float(std::max(1, gw)) / std::max(1, gh);
            }
        }
        if (gameWindow && gameVisible) {
            gameTexture = gui.texture(gameSurface);
        }
        gui.renderFrame();
        finishEditorTheme(ImGui::GetDrawData());
        profiler.checkpoint(ProfilePart::Editor);

        // The time that has passed since the previous frame.
        const Uint64 ticks = SDL_GetTicksNS();
        const float seconds = static_cast<float>(ticks - lastTicks) * 1e-9f;
        lastTicks = ticks;

        if (cameraLooking &&
            (!sceneVisible || browser.dialog.pending() || projectDialog.pending() ||
             !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS))) {
            cameraLooking = false;
            SDL_SetWindowRelativeMouseMode(window, false);
        }
        if (cameraLooking && !browser.dialog.pending() && !projectDialog.pending() &&
            !cameraModifiersHeld(SDL_GetModState()) && !editor.dragging() &&
            !ImGui::GetIO().WantTextInput &&
            (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)) {
            const bool* keys = SDL_GetKeyboardState(nullptr);
            const float speed =
                cameraSpeed * (SDL_GetModState() & SDL_KMOD_SHIFT ? cameraBoost : 1.0f);
            camera.move(static_cast<float>(keys[SDL_SCANCODE_W]) - keys[SDL_SCANCODE_S],
                        static_cast<float>(keys[SDL_SCANCODE_D]) - keys[SDL_SCANCODE_A],
                        static_cast<float>(keys[SDL_SCANCODE_E]) - keys[SDL_SCANCODE_Q],
                        speed * std::min(seconds, 0.1f));
        }
        profiler.checkpoint(ProfilePart::Other);
        if (playMode.playing()) {
            Prefabs::updateDestroyTimers(world, std::min(seconds, .25f));
            scripts.update(std::min(seconds, 0.25f));
            componentScripts.update(std::min(seconds, 0.25f));
            animations.update(world, browser.assets, std::min(seconds, .25f));
            const auto animationEvents = animations.events;
            for (const auto& event : animationEvents) {
                scripts.console().objectEvent(event.owner, event.name);
                componentScripts.objectEvent(event.owner, event.name);
            }
        } else if (!playMode.active()) {
            animations.clear();
        }
        profiler.checkpoint(ProfilePart::Scripts);
        LuaConsole::dispatchEvents();
        scripts.console().setAssetRoot(browser.assets.root);
        renderer.setShadowSettings(renderingSettings.shadows);
        renderer.setAssetRoot(browser.assets.root);
        renderer.updateSkyboxFades(world, playMode.playing() ? std::min(seconds, .25f) : 0.0f);
        updateHierarchy(world);
        profiler.checkpoint(ProfilePart::Hierarchy);
        physics.setAssetRoot(browser.assets.root);
        physics.update(world, playMode.playing() ? seconds : 0.0f, [&](float dt) {
            scripts.fixedUpdate(dt);
            componentScripts.fixedUpdate(dt);
            updateCharacterControllers(world, physics);
            updateHierarchy(world);
        });
        if (playMode.playing()) {
            const auto triggerEvents = physics.triggerEvents;
            const auto collisionEvents = physics.collisionEvents;
            auto dispatch = [&](const auto& events, const char* const* names) {
                for (const auto& event : events) {
                    scripts.console().objectEvent(
                        event.receiver, names[event.phase], event.other, true);
                    componentScripts.objectEvent(
                        event.receiver, names[event.phase], event.other, true);
                }
            };
            const char* triggerNames[]{"onTriggerEnter", "onTriggerStay", "onTriggerExit"};
            const char* collisionNames[]{"onCollisionEnter", "onCollisionStay", "onCollisionExit"};
            dispatch(triggerEvents, triggerNames);
            dispatch(collisionEvents, collisionNames);
        }
        profiler.checkpoint(ProfilePart::Physics);
        updateHierarchy(world);
        profiler.checkpoint(ProfilePart::Hierarchy);
        if (playMode.playing()) {
            updateFPSCameras(world, std::min(seconds, .25f), input);
        }
        updateCameras(world, playMode.playing() ? std::min(seconds, .25f) : 0);
        updateHierarchy(world);
        if (!(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) && !externalFocus) {
            input.releaseMouse();
        }
        profiler.checkpoint(ProfilePart::Cameras);
        audio.update(world, browser.assets, playMode.playing());
        if (playMode.playing()) {
            std::vector<std::pair<Entity, int>> audioEvents;
            world.each<AudioSource>([&](Entity e, AudioSource& source) {
                for (int event : source.pendingEvents) {
                    audioEvents.emplace_back(e, event);
                }
                source.pendingEvents.clear();
            });
            for (const auto& [entity, event] : audioEvents) {
                componentScripts.objectEvent(entity, AudioSource::eventNames[event]);
                scripts.console().objectEvent(entity, AudioSource::eventNames[event]);
            }
        }
        physics.update(world, 0); // collider poses follow parent motion before picking next frame
        if (playMode.playing()) {
            Entity hovered;
            const auto cameraEntity = activeCamera(world);
            float mx = 0, my = 0;
            SDL_GetMouseState(&mx, &my);
            bool inside = gameHovered && gameImageSize.x > 0 && gameImageSize.y > 0;
            float u = inside ? (mx - gameImageOrigin.x) / gameImageSize.x : 0;
            float v = inside ? (my - gameImageOrigin.y) / gameImageSize.y : 0;
            if (gameWindow && externalFocus) {
                int w = 1, h = 1;
                SDL_GetWindowSize(gameWindow, &w, &h);
                u = mx / std::max(1, w);
                v = my / std::max(1, h);
                inside = true;
            }
            if (input.mouseLocked) {
                u = .5f;
                v = .5f;
                inside = true;
            }
            if (inside && u >= 0 && u <= 1 && v >= 0 && v <= 1 && cameraEntity != Entity{}) {
                const auto& c = world.get<Camera>(cameraEntity);
                const auto& t = world.get<Transform>(cameraEntity);
                if (u >= c.viewportX && u <= c.viewportX + c.viewportWidth && v >= c.viewportY &&
                    v <= c.viewportY + c.viewportHeight) {
                    const Vec3 nearPoint = cameraScreenToWorld(t, c, {u, v, 0}),
                               farPoint = cameraScreenToWorld(t, c, {u, v, 1});
                    RayHit hit;
                    if (physics.raycast(
                            {nearPoint, normalize(farPoint - nearPoint)}, c.farClip, hit) &&
                        isActive(world, hit.entity)) {
                        hovered = hit.entity;
                    }
                }
            }
            auto notify = [&](Entity entity, const char* event) {
                if (entity != Entity{} && isActive(world, entity)) {
                    componentScripts.objectEvent(entity, event);
                    scripts.console().objectEvent(entity, event);
                }
            };
            if (hovered != mouseOver) {
                notify(mouseOver, "onMouseExit");
                notify(hovered, "onMouseEnter");
                mouseOver = hovered;
            }
            notify(hovered, "onMouseOver");
            if (input.mousePressed[1]) {
                mousePressedObject = hovered;
                notify(hovered, "onMouseDown");
            }
            if (input.mouseReleased[1]) {
                notify(mousePressedObject, "onMouseUp");
                mousePressedObject = {};
            }
        }

        profiler.checkpoint(ProfilePart::Audio);
        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        scripts.console().setAssetRoot(browser.assets.root);
        renderer.setShadowSettings(renderingSettings.shadows);
        renderer.setAssetRoot(browser.assets.root);
        if ((width > 0 && height > 0) || gameWindow) {
            overlays.setAssetRoot(browser.assets.root);
            if (sceneVisible) {
                try {
                    const float aspect = float(sceneSurface.width()) / sceneSurface.height();
                    const Mat4 vp = camera.viewProjection(aspect);
                    overlays.begin(vp);
                    overlays.audioIcons(world);
                    overlays.grid(browser.settings.grid);
                    if (browser.settings.alwaysShowColliders) {
                        world.each<Transform, Collider>(
                            [&](Entity e, const Transform& t, const Collider& c) {
                                if (e != selectedEntity) {
                                    overlays.collider(t, effectiveCollider(world, e, c), .25f);
                                }
                            });
                    }
                    if (world.alive(selectedEntity)) {
                        const auto* transform = world.tryGet<Transform>(selectedEntity);
                        const auto* collider = world.tryGet<Collider>(selectedEntity);
                        if (transform && collider) {
                            overlays.collider(*transform,
                                              effectiveCollider(world, selectedEntity, *collider));
                        }
                        if (transform) {
                            if (const auto* query = world.tryGet<Raycast>(selectedEntity)) {
                                overlays.raycast(*transform, *query);
                            }
                            if (const auto* source = world.tryGet<AudioSource>(selectedEntity)) {
                                overlays.audioSource(*transform, *source);
                            }
                            if (const auto* selectedCamera = world.tryGet<Camera>(selectedEntity)) {
                                overlays.camera(*transform, *selectedCamera);
                            }
                        }
                    } else {
                        selectedEntity = {};
                    }
                    renderer.drawLitTo(sceneSurface,
                                       world,
                                       vp,
                                       sceneSurface.width(),
                                       sceneSurface.height(),
                                       camera.position(),
                                       overlays.draws());
                    if (smokeTest) {
                        if (!smokeScene && !hasVisibleGeometry(sceneSurface)) {
                            throw std::runtime_error("Smoke test: Scene image is empty");
                        }
                        ++smokeScene;
                    }
                    terminalErrors.report("Scene rendering", "");
                } catch (const std::exception& error) {
                    terminalErrors.report("Scene rendering", error.what());
                    if (smokeTest) {
                        smokeFailed = true;
                    }
                }
            }
            profiler.checkpoint(ProfilePart::Scene);
            if (sceneVisible && world.has<Camera>(selectedEntity) &&
                world.has<Transform>(selectedEntity)) {
                auto previewCamera = world.get<Camera>(selectedEntity);
                const auto& pose = world.get<Transform>(selectedEntity);
                previewCamera.aspect =
                    float(cameraPreviewSurface.width()) / cameraPreviewSurface.height();
                try {
                    renderer.drawLitTo(cameraPreviewSurface,
                                       world,
                                       cameraMatrix(pose, previewCamera),
                                       cameraPreviewSurface.width(),
                                       cameraPreviewSurface.height(),
                                       cameraEye(pose, previewCamera));
                    if (smokeTest) {
                        ++smokeCamera;
                    }
                    terminalErrors.report("Camera preview", "");
                } catch (const std::exception& error) {
                    terminalErrors.report("Camera preview", error.what());
                    if (smokeTest) {
                        smokeFailed = true;
                    }
                }
            }
            if (gameVisible) {
                try {
                    const auto e = activeCamera(world);
                    if (e == Entity{}) {
                        gameSurface.render(gameSurface.width(), gameSurface.height(), {});
                    } else {
                        const auto& c = world.get<Camera>(e);
                        const auto& t = world.get<Transform>(e);
                        const auto vp = cameraMatrix(t, c);
                        const auto region =
                            vulkanCameraRegion(c, gameSurface.width(), gameSurface.height());
                        overlays.begin(vp);
                        if (showGameColliders) {
                            world.each<Transform, Collider>([&](Entity entity,
                                                                const Transform& transform,
                                                                const Collider& collider) {
                                overlays.collider(transform,
                                                  effectiveCollider(world, entity, collider));
                            });
                        }
                        bool gameUpscaled = false;
#ifdef LEGACY_FSR3_GAME
                        const int quality = renderingSettings.fsr3GameQuality;
                        if (quality != fsrLastQuality) {
                            fsrLastQuality = quality;
                            fsrHardwareUnavailable = false;
                            fsrHistory.invalidate();
                            fsrGame.reset();
                            fsrHasPreviousEye = false;
                        }
                        if (!quality) {
                            fsrStatus = "Off";
                            fsrHistory.invalidate();
                        } else if (c.orthographic || region.x || region.y ||
                                   region.width != gameSurface.width() ||
                                   region.height != gameSurface.height() || showGameColliders) {
                            fsrStatus =
                                "Native fallback: FSR needs a perspective, full-viewport camera without collider overlays.";
                            fsrHistory.invalidate();
                        } else if (fsrHardwareUnavailable) {
                            // Keep the last diagnostic; changing quality retries initialization.
                            fsrHistory.invalidate();
                        } else {
                            bool dispatchAttempted = false;
                            bool initialization = true;
                            try {
                                const TemporalSampling::Extent display{gameSurface.width(),
                                                                       gameSurface.height()};
                                const auto render = TemporalSampling::renderExtent(
                                    display, static_cast<TemporalSampling::Quality>(quality - 1));
                                if (render.width < 64 || render.height < 64) {
                                    throw std::runtime_error("Game viewport is too small for FSR");
                                }
                                if (!fsrInputSurface) {
                                    auto inputs = std::make_unique<VulkanSceneSurface>(
                                        context.physicalDevice(),
                                        context.device(),
                                        context.graphicsQueue(),
                                        context.graphicsFamily(),
                                        shaders.unlitVertex,
                                        shaders.unlitFragment,
                                        &sceneSurface);
                                    inputs->enableLighting(
                                        shaders.litVertex, litFragment, shadowVertex);
                                    inputs->enableMotionVectors(
                                        loadVulkanShader("motion.vert.spv"),
                                        loadVulkanShader("motion.frag.spv"));
                                    fsrInputSurface = std::move(inputs);
                                }
                                if (!fsrGame || !(render == fsrRender) ||
                                    !(display == fsrDisplay)) {
                                    fsrGame.reset();
                                    fsrHistory.invalidate();
                                    fsrRender = render;
                                    fsrDisplay = display;
                                    fsrGame = std::make_unique<VulkanFsr3Game>(
                                        VulkanFsr3Game::Device{context.instance(),
                                                               context.physicalDevice(),
                                                               context.device(),
                                                               context.graphicsQueue(),
                                                               context.graphicsFamily(),
                                                               context.apiVersion()},
                                        render,
                                        display);
                                }
                                initialization = false;
                                const auto hashCombine = [](std::uint64_t a, std::uint64_t b) {
                                    return a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2));
                                };
                                std::uint64_t sceneId =
                                    hashCombine(world.restoreRevision(),
                                                std::hash<std::string>{}(currentProject));
                                sceneId = hashCombine(sceneId,
                                                      std::hash<std::string>{}(levels.current()));
                                sceneId = hashCombine(sceneId, playMode.active());
                                std::uint64_t projection =
                                    hashCombine(std::hash<float>{}(c.fieldOfView),
                                                std::hash<float>{}(c.nearClip));
                                projection = hashCombine(projection, std::hash<float>{}(c.farClip));
                                projection = hashCombine(projection, std::uint64_t(quality));
                                const auto cameraId = (uint64_t(e.generation) << 32) | e.index;
                                const auto eye = cameraEye(t, c);
                                const auto forward =
                                    rotateVector(normalize(t.rotation), {0, 0, -1});
                                const Vec3 delta = eye - fsrPreviousEye;
                                const bool cut = !fsrHasPreviousEye ||
                                                 fsrLastFrame + 1 != fsrFrameNumber ||
                                                 seconds > .25f || dot(delta, delta) > 100.f ||
                                                 dot(forward, fsrPreviousForward) < .5f;
                                TemporalHistory::Configuration temporalConfig{
                                    render, display, sceneId, cameraId, projection};
                                renderer.drawTemporalTo(
                                    *fsrInputSurface,
                                    world,
                                    vp,
                                    temporalConfig,
                                    fsrHistory,
                                    eye,
                                    true,
                                    cut,
                                    [&](const auto& frame) {
                                        dispatchAttempted = true;
                                        fsrGame->dispatch(
                                            {fsrInputSurface->colorImage(),
                                             fsrInputSurface->motionDepthImage(),
                                             fsrInputSurface->motionImage(),
                                             gameSurface.colorImage(),
                                             frame.frame.jitter,
                                             frame.frame.reset || frame.missingObjectHistory,
                                             std::clamp(seconds * 1000.f, 1.f, 250.f),
                                             c.nearClip,
                                             c.farClip,
                                             radians(c.fieldOfView)});
                                    });
                                fsrPreviousEye = eye;
                                fsrPreviousForward = forward;
                                fsrHasPreviousEye = true;
                                fsrLastFrame = fsrFrameNumber;
                                fsrStatus = "Active (Game view only)";
                                gameUpscaled = true;
                            } catch (const std::exception& fsrError) {
                                fsrHistory.invalidate();
                                fsrHasPreviousEye = false;
                                fsrStatus = std::string("Native fallback: ") + fsrError.what();
                                if (dispatchAttempted || initialization) {
                                    fsrGame.reset();
                                    // A tiny window can grow without changing quality.
                                    if (gameSurface.width() >= 192 && gameSurface.height() >= 192) {
                                        fsrHardwareUnavailable = true;
                                    }
                                }
                            }
                        }
#endif
                        if (!gameUpscaled) {
                            renderer.drawLitTo(gameSurface,
                                               world,
                                               vp,
                                               gameSurface.width(),
                                               gameSurface.height(),
                                               cameraEye(t, c),
                                               overlays.draws(),
                                               &region);
                        }
#ifdef LEGACY_FSR3_GAME
                        if (smokeFsr) {
                            if (gameUpscaled) {
                                if (gameWindow) {
                                    ++smokeFsrDetached;
                                } else {
                                    ++smokeFsrEmbedded;
                                }
                            } else if (renderingSettings.fsr3GameQuality && c.orthographic) {
                                ++smokeFsrFallback;
                            } else if (!renderingSettings.fsr3GameQuality && renderedFrames >= 90) {
                                ++smokeFsrOff;
                            }
                        }
#endif
                        if (smokeTest) {
                            if (!smokeGame && !hasVisibleGeometry(gameSurface)) {
                                throw std::runtime_error("Smoke test: Game image is empty");
                            }
                            ++smokeGame;
                            if (!gameWindow) {
                                ++smokeEmbeddedGame;
                            }
                        }
                    }
                    terminalErrors.report("Game rendering", "");
                } catch (const std::exception& error) {
                    terminalErrors.report("Game rendering", error.what());
                    if (smokeTest) {
                        smokeFailed = true;
                    }
                }
            }
            profiler.checkpoint(ProfilePart::Game);
        }
        gui.endFrame();
        if (gameWindow && gameContext && gameVisible && gameTexture != ImTextureID_Invalid) {
            try {
                gameContext->setVsync(renderingSettings.vsync);
                char label[64]{};
                if (showFPS) {
                    std::snprintf(label, sizeof(label), "FPS: %.0f", ImGui::GetIO().Framerate);
                }
                if (gui.presentSecondary(*gameContext, gameTexture, label) ==
                        VulkanContext::FrameResult::Presented &&
                    smokeTest) {
                    ++smokeSecondary;
                }
                terminalErrors.report("Game window", "");
            } catch (const std::exception& error) {
                terminalErrors.report("Game window", error.what());
                if (smokeTest) {
                    smokeFailed = true;
                }
            }
        }
        profiler.checkpoint(ProfilePart::UI);

        // Compare persisted content, not history depth: Undo back to the saved
        // state clears the marker, while Lua/camera edits also mark it dirty.
        if (!prefabLevel && !playMode.active() && !editor.dragging()) {
            projectDirty =
                currentProject.empty() || scripts.hasUnsavedChanges() ||
                animationWindow.hasUnsavedChanges() ||
                projectContentState(world, embeddedLegacySource, camera) != savedProjectState;
        }
        if (projectEnabled && !currentProject.empty() && projectDirty &&
            browser.settings.autosaveSeconds > 0 &&
            std::chrono::steady_clock::now() - lastAutosave >=
                std::chrono::seconds(browser.settings.autosaveSeconds)) {
            lastAutosave = std::chrono::steady_clock::now();
            saveTo(currentProject);
        }
        if (exitRequested) {
            exitRequested = false;
            input.releaseMouse();
            cameraLooking = false;
            SDL_SetWindowRelativeMouseMode(window, false);
            SDL_SetWindowMouseGrab(window, false);
            SDL_SetWindowMouseRect(window, nullptr);
            if (playMode.active()) {
                // Run stop callbacks before restoring, so their edits are discarded too.
                levels.suspended = true;
                levels.cancel();
                LuaConsole::clearEvents();
                audio.clear();
                componentScripts.stop();
                scripts.shutdown();
                LuaConsole::clearEvents();
                if (gameWindow) {
                    input.releaseMouse();
                    input.window = window;
                    gameContext.reset();
                    SDL_DestroyWindow(gameWindow);
                    gameWindow = nullptr;
                }
                mouseOver = {};
                mousePressedObject = {};
                playMode.stop(world);
                input.releaseMouse();
                physics.reset();
                scripts.resetRuntime();
                scripts.setSource(editSource);
                embeddedLegacySource = editLegacySource;
                browser.openedScript = editScriptPath;
                scripts.setAssetContext(editScriptPath, editSelection);
                camera = editCamera;
                history = std::move(editHistory);
                levels.commit(editLevelName, false);
                levels.suspended = false;
                selectedEntity = world.alive(editSelection) ? editSelection : Entity{};
                projectDirty =
                    currentProject.empty() || scripts.hasUnsavedChanges() ||
                    animationWindow.hasUnsavedChanges() ||
                    projectContentState(world, embeddedLegacySource, camera) != savedProjectState;
            }
            if (browser.importing() || prefabLevel || levels.busy() || projectDialog.pending()) {
                projectError =
                    "Wait for imports, stop Play mode, finish prefab editing, and close pending dialogs before exiting.";
            } else if (!projectDirty) {
                running = false;
            } else {
                const SDL_MessageBoxButtonData buttons[]{
                    {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel"},
                    {0, 1, "Discard"},
                    {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 2, "Save"}};
                const SDL_MessageBoxData box{SDL_MESSAGEBOX_WARNING,
                                             window,
                                             "Unsaved project",
                                             "Save project changes before exiting?",
                                             3,
                                             buttons,
                                             nullptr};
                int choice = 0;
                if (SDL_ShowMessageBox(&box, &choice)) {
                    if (choice == 1) {
                        running = false;
                    }
                    if (choice == 2) {
                        exitAfterSave = true;
                        if (currentProject.empty()) {
                            projectDialog.show(window, true, currentProject);
                        } else {
                            saveTo(currentProject);
                        }
                    }
                }
            }
        }
        if (cameraLooking || input.mouseLocked) {
            SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_MODE_CENTER, "0");
            if (input.mouseLocked && input.window == gameWindow && gameWindow) {
                SDL_SetWindowMouseRect(window, nullptr);
                SDL_SetWindowMouseGrab(window, false);
                int w = 0, h = 0;
                SDL_GetWindowSize(gameWindow, &w, &h);
                const SDL_Rect rect{0, 0, w, h};
                if (w > 0 && h > 0) {
                    SDL_SetWindowMouseRect(gameWindow, &rect);
                    SDL_SetWindowMouseGrab(gameWindow, true);
                } else {
                    input.releaseMouse();
                }
            } else {
                const auto origin = ImGui::GetMainViewport()->Pos;
                SDL_Rect rect = !input.mouseLocked ? SDL_Rect{int(sceneRect.x - origin.x),
                                                              int(sceneRect.y - origin.y),
                                                              int(sceneRect.width),
                                                              int(sceneRect.height)}
                                                   : SDL_Rect{int(gameImageOrigin.x - origin.x),
                                                              int(gameImageOrigin.y - origin.y),
                                                              int(gameImageSize.x),
                                                              int(gameImageSize.y)};
                int w = 0, h = 0;
                SDL_GetWindowSize(window, &w, &h);
                int right = std::min(w, rect.x + rect.w), bottom = std::min(h, rect.y + rect.h);
                rect.x = std::max(0, rect.x);
                rect.y = std::max(0, rect.y);
                rect.w = right - rect.x;
                rect.h = bottom - rect.y;
                if (rect.w > 0 && rect.h > 0) {
                    if (!SDL_SetWindowMouseRect(window, &rect) ||
                        !SDL_SetWindowMouseGrab(window, true)) {
                        input.releaseMouse();
                        cameraLooking = false;
                    } else {
                        float x = 0, y = 0;
                        SDL_GetMouseState(&x, &y);
                        // Relative mode owns its virtual cursor. Repeated warps
                        // inject synthetic motion and make Scene freelook jump.
                        if (!SDL_GetWindowRelativeMouseMode(window) &&
                            (x < rect.x || x >= rect.x + rect.w || y < rect.y ||
                             y >= rect.y + rect.h)) {
                            SDL_WarpMouseInWindow(
                                window, rect.x + rect.w * .5f, rect.y + rect.h * .5f);
                        }
                    }
                } else {
                    input.releaseMouse();
                    cameraLooking = false;
                    SDL_SetWindowRelativeMouseMode(window, false);
                }
            }
        } else {
            SDL_SetWindowMouseRect(window, nullptr);
            SDL_SetWindowMouseGrab(window, false);
            if (gameWindow) {
                SDL_SetWindowMouseRect(gameWindow, nullptr);
                SDL_SetWindowMouseGrab(gameWindow, false);
            }
        }
        const std::string title =
            projectWindowTitle(currentProject, projectDirty, levels.current());
        if (title != lastWindowTitle) {
            SDL_SetWindowTitle(window, title.c_str());
            lastWindowTitle = title;
        }
        profiler.checkpoint(ProfilePart::Other);
        profiler.checkpoint(ProfilePart::Present);
        profiler.end();
        if (width > 0 && height > 0) {
            ++renderedFrames;
            if (smokeTest && renderedFrames >= smokeFrames) {
                SDL_Log("Shutdown requested: completed %u automated frames", renderedFrames);
                running = false;
            }
        }
    }
    levels.suspended = true;
    activeProfiler = nullptr;
    editor.finish(world);
    audio.clear();
    componentScripts.stop();
    if (gameWindow) {
        input.releaseMouse();
        input.window = window;
        gameContext.reset();
        SDL_DestroyWindow(gameWindow);
        gameWindow = nullptr;
    }
    scripts.shutdown();
    input.releaseMouse();
    if (smokeTest) {
        SDL_Log("Vulkan smoke coverage: scene=%u game=%u embedded=%u camera=%u secondary=%u",
                smokeScene,
                smokeGame,
                smokeEmbeddedGame,
                smokeCamera,
                smokeSecondary);
    }
#ifdef LEGACY_FSR3_GAME
    if (smokeFsr) {
        SDL_Log("Game FSR smoke: embedded=%u detached=%u fallback=%u off=%u status=%s",
                smokeFsrEmbedded,
                smokeFsrDetached,
                smokeFsrFallback,
                smokeFsrOff,
                fsrStatus.c_str());
        if (!smokeFsrEmbedded || !smokeFsrDetached || !smokeFsrFallback || !smokeFsrOff) {
            smokeFailed = true;
        }
    }
#endif
    return !smokeTest || (renderedFrames >= smokeFrames && !smokeFailed && smokeScene &&
                          smokeGame && smokeEmbeddedGame && smokeCamera && smokeSecondary);
}

int engineMain(int argc, char** argv) {
    bool smokeTest = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke-test") == 0) {
            smokeTest = true;
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unknown argument: %s", argv[i]);
            return 1;
        }
    }
    SDL_Log("Run mode: %s", smokeTest ? "smoke test (120 frames)" : "interactive editor");
    SDL_Log("Initializing SDL video");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not start SDL: %s", SDL_GetError());
        return 1;
    }

    struct SDLGuard {
        ~SDLGuard() {
            SDL_Quit();
        }
    } sdlGuard;
    auto window = std::unique_ptr<SDL_Window, void (*)(SDL_Window*)>(
        SDL_CreateWindow("LegacyEngine",
                         1280,
                         720,
                         SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED |
                             SDL_WINDOW_HIGH_PIXEL_DENSITY),
        SDL_DestroyWindow);
    if (!window) {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION, "Could not create Vulkan window: %s", SDL_GetError());
        return 1;
    }
    SDL_Log("Initializing Vulkan renderer");
    VulkanContext context(window.get());
    VulkanEditorPreviewBackend::Shaders shaders{loadVulkanShader("unlit.vert.spv"),
                                                loadVulkanShader("unlit.frag.spv"),
                                                loadVulkanShader("lit.vert.spv"),
                                                loadVulkanShader("lit_noshadow.frag.spv"),
                                                loadVulkanShader("skybox.vert.spv"),
                                                loadVulkanShader("skybox.frag.spv")};
    const auto litFragment = loadVulkanShader("lit.frag.spv");
    const auto shadowVertex = loadVulkanShader("shadow.vert.spv");
    VulkanSceneSurface scene(context.physicalDevice(),
                             context.device(),
                             context.graphicsQueue(),
                             context.graphicsFamily(),
                             shaders.unlitVertex,
                             shaders.unlitFragment);
    scene.enableLighting(shaders.litVertex, litFragment, shadowVertex);
    scene.enableSkybox(shaders.skyVertex, shaders.skyFragment);
    VulkanSceneRenderer renderer(scene);
    IMGUI_CHECKVERSION();
    auto imgui = std::unique_ptr<ImGuiContext, void (*)(ImGuiContext*)>(ImGui::CreateContext(),
                                                                        ImGui::DestroyContext);
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::GetIO().ConfigDebugHighlightIdConflicts = false;
    GImGui->ErrorCallback = +[](ImGuiContext*, void*, const char* message) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ImGui: %s", message);
    };
    ImGui::GetIO().ConfigErrorRecoveryEnableTooltip = false;
    ImGui::GetIO().ConfigErrorRecoveryEnableAssert = false;
    ImGui::GetIO().ConfigErrorRecoveryEnableDebugLog = true;
    dockedEditor = true;
    ImGui::GetIO().IniFilename = smokeTest ? nullptr : "legacy-layout.ini";
    applyEditorTheme();
    VulkanImGui gui(window.get(), context);
    SDL_Log("Editor started");
    const bool runSucceeded = run(
        window.get(), context, scene, renderer, gui, shaders, litFragment, shadowVertex, smokeTest);
    if (smokeTest && runSucceeded) {
        SDL_Log("Smoke test passed: rendered 120 frames and shut down cleanly");
    }
    return runSucceeded ? 0 : 1;
}

int main(int argc, char** argv) {
    EngineLog::initialize();
    SDL_Log("Starting LegacyEngine");
    int result = 1;
    try {
        result = engineMain(argc, argv);
    } catch (const std::exception& error) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Unhandled exception: %s", error.what());
    } catch (...) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Unhandled non-standard exception");
    }
    if (result == 0) {
        SDL_Log("LegacyEngine shut down cleanly");
    } else {
        SDL_LogError(
            SDL_LOG_CATEGORY_APPLICATION, "LegacyEngine exited with error code %d", result);
    }
    return result;
}
