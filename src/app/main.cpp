#include "editor/recent_projects.hpp"
#include "editor/scene_picking.hpp"
#include "effects/laser_update.hpp"
#include "engine_version.hpp"
#include "ui/game_ui.hpp"
#include "ui/script_gui.hpp"
#include <cctype>
#include <imgui_internal.h>
// LegacyEngine - Scene editing and runtime orchestration.
//
// World conventions:
//   * 1 unit = 1 metre
//   * +Y is up, the ground is the XZ plane (y = 0)
//   * right-handed coordinates (the OpenGL convention)

#include "animation/animation_system.hpp"
#include "animation/bone_attachment_system.hpp"
#include "audio/audio_system.hpp"
#include "core/components.hpp"
#include "core/ecs.hpp"
#include "editor/animation_window.hpp"
#include "editor/asset_browser.hpp"
#include "editor/asset_preview_renderer.hpp"
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
#include "editor/lightmapping_window.hpp"
#include "editor/model_preview_window.hpp"
#include "editor/navigation_window.hpp"
#include "editor/occlusion_window.hpp"
#include "editor/particle_playback.hpp"
#include "editor/play_mode.hpp"
#include "editor/playback_controls.hpp"
#include "editor/profiler_window.hpp"
#include "editor/river_editor.hpp"
#include "editor/scene_orientation.hpp"
#include "editor/script_editor.hpp"
#include "editor/transform_editor.hpp"
#include "gameplay/first_person.hpp"
#include "gameplay/raycast.hpp"
#include "gameplay/third_person.hpp"
#include "levels/level_system.hpp"
#include "models/collider_source.hpp"
#include "particles/particle_update.hpp"
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
#include "rendering/renderer.hpp"
#include "rendering/scene_surface.hpp"
#include "scene/camera.hpp"
#include "scene/scene_camera.hpp"
#include "scene/skybox_transition.hpp"
#include "scripting/script_components.hpp"
#include "terrain/terrain_editor.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iterator>

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
constexpr int kRoomSteps = 50; // Stay within one metre of the nearby creation point.

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

// Creates the window together with an OpenGL 3.3 core context. On failure both
// outputs are null and SDL_GetError() describes the problem.
static void createWindow(int multisamples, SDL_Window*& window, SDL_GLContext& context) {
    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, multisamples > 0 ? 1 : 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, multisamples);

    window = SDL_CreateWindow("LegacyEngine",
                              1280,
                              720,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED |
                                  SDL_WINDOW_HIGH_PIXEL_DENSITY);
    context = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!context && window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
}

// ---------------------------------------------------------------------------
// The scene and the main loop
// ---------------------------------------------------------------------------

static bool run(SDL_Window* window, Renderer& renderer, bool smokeTest) {
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
    ScriptEditor scripts;
    scripts.bindWorld(world, cubeMesh, sphereMesh, capsuleMesh, coneMesh, planeMesh);
    EditorHistory history(world);
    Terrains::Editor terrainEditor;
    PlayMode playMode;
    ObjectClipboard clipboard;
    float particlePreviewSpeed = 1;
    AssetBrowser browser;
    ScriptComponents componentScripts;
    AudioSystem audio;
    AnimationSystem animations;
    AnimationWindow animationWindow;
    LightmappingWindow lightmappingWindow;
    OcclusionWindow occlusionWindow;
    NavigationWindow navigationWindow;
    ModelPreviewWindow modelPreviewWindow;
    SceneSurface sceneSurface, gameSurface, cameraPreviewSurface;
    GameUI gameUI;
    ScriptGUI scriptGUI;
    gameUI.clicked = [](Entity owner, const std::string& id) {
        LuaConsole::postEvent(GameUI::clickEvent(owner, id));
    };
    auto lastSceneRender = std::chrono::steady_clock::time_point{};
    int lastSceneWidth = 0, lastSceneHeight = 0;
    AssetPreviewRenderer assetPreviews(
        renderer, {gridMesh, planeMesh, cubeMesh, sphereMesh, capsuleMesh, coneMesh});
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
    bool separateGame = false;
    SDL_Window* gameWindow = nullptr;
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
    bool tpsMouseOwned = false;
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
    constexpr unsigned smokeFrames = 120;
    std::optional<Vec3> orbitPivot;
    bool orbitStarted = false;
    bool selectModelRoots = false;
    bool focusSuspended = false;
    while (running) {
        if (!smokeTest && !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) &&
            !(gameWindow && (SDL_GetWindowFlags(gameWindow) & SDL_WINDOW_INPUT_FOCUS))) {
            input.releaseMouse();
            input.clear();
            cameraLooking = false;
            orbitPivot.reset();
            if (!focusSuspended) {
                audio.suspend(true);
                focusSuspended = true;
            }
            // Do not poll project transitions/import results or run editor work
            // while backgrounded. Only pump OS events until focus or a close request.
            while (!(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) &&
                   !(gameWindow && (SDL_GetWindowFlags(gameWindow) & SDL_WINDOW_INPUT_FOCUS))) {
                SDL_Event wake;
                if (!SDL_WaitEvent(&wake)) {
                    SDL_Delay(50);
                    continue;
                }
                if (wake.type == SDL_EVENT_QUIT || wake.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
                    SDL_PushEvent(&wake);
                    break;
                }
                ImGui_ImplSDL3_ProcessEvent(&wake);
            }
            lastTicks = SDL_GetTicksNS();
            lastAutosave = std::chrono::steady_clock::now();
        }
        const Uint64 frameStarted = SDL_GetTicksNS();
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
        TextureQuality::lifecycle(playMode.active());
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
            scriptGUI.shutdown();
            gameUI.sync(world, false, browser.assets.root, window);
            world.restore(transition->level->world);
            resetHierarchyExpansion();
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
        if (input.lockedWindow && input.lockedWindow != input.window) {
            input.releaseMouse();
        }
        if (input.mouseLocked && !playMode.active()) {
            input.releaseMouse();
        }
        input.enabled = ((SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) || externalFocus) &&
                        playMode.playing() && !cameraLooking &&
                        (gameFocused || externalFocus || input.mouseLocked) &&
                        (!ImGui::GetIO().WantTextInput || input.mouseLocked) &&
                        !inputSettings.visible && !gameUI.capturesKeyboard() &&
                        !scriptGUI.capturesKeyboard();
        input.mouseEnabled = externalFocus || gameHovered || sceneHovered || input.mouseLocked;
        input.beginFrame();
        try {
            if (!playMode.active()) {
                scriptGUI.shutdown();
            }
            gameUI.sync(world, playMode.active(), browser.assets.root, input.window);
        } catch (const std::exception& error) {
            terminalErrors.report("Game UI", error.what());
        }
        const auto uiOrigin = ImGui::GetMainViewport()->Pos;
        int uiW = 1, uiH = 1;
        if (gameWindow) {
            SDL_GetWindowSize(gameWindow, &uiW, &uiH);
        }
        gameUI.viewport(gameWindow ? 0.f : gameImageOrigin.x - uiOrigin.x,
                        gameWindow ? 0.f : gameImageOrigin.y - uiOrigin.y,
                        gameWindow ? float(uiW) : gameImageSize.x,
                        gameWindow ? float(uiH) : gameImageSize.y,
                        gameSurface.width,
                        gameSurface.height);
        scriptGUI.viewport(gameWindow ? 0.f : gameImageOrigin.x - uiOrigin.x,
                           gameWindow ? 0.f : gameImageOrigin.y - uiOrigin.y,
                           gameWindow ? float(uiW) : gameImageSize.x,
                           gameWindow ? float(uiH) : gameImageSize.y,
                           gameSurface.width,
                           gameSurface.height);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (gameWindow && event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(gameWindow)) {
                input.releaseMouse();
                input.window = window;
                scriptGUI.shutdown();
                gameUI.sync(world, false, browser.assets.root, window);
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
                if (!(scriptGUI.event(event, gameWindow, externalFocus, input.mouseLocked) |
                      gameUI.event(event, gameWindow, externalFocus, input.mouseLocked)) ||
                    event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                    input.event(event);
                }
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
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                orbitPivot.reset();
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST ||
                (cameraLooking && event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
                 event.button.button == SDL_BUTTON_RIGHT)) {
                cameraLooking = false;
                input.releaseMouse();
                SDL_SetWindowRelativeMouseMode(window, false);
                SDL_SetWindowMouseGrab(window, false);
                SDL_SetWindowMouseRect(window, nullptr);
            }
            if ((cameraLooking || orbitPivot) && event.type == SDL_EVENT_KEY_DOWN &&
                event.key.key == SDLK_ESCAPE) {
                cameraLooking = false;
                orbitPivot.reset();
                input.releaseMouse();
                clicking = false;
                continue;
            }
            const bool navigationMouse =
                (cameraLooking || orbitPivot.has_value()) &&
                (event.type == SDL_EVENT_MOUSE_MOTION ||
                 event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                 event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_WHEEL);
            // Keep key-up events flowing to ImGui so a key held when RMB starts
            // cannot remain stuck in the editor. Scene RMB clears the text focus.
            if (!navigationMouse) {
                ImGui_ImplSDL3_ProcessEvent(&event);
            }
            if (!(scriptGUI.event(
                      event, window, gameFocused && playMode.active(), input.mouseLocked) |
                  gameUI.event(
                      event, window, gameFocused && playMode.active(), input.mouseLocked)) ||
                event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                input.event(event);
            }
            const bool mouseEvent = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                                    event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
                                    event.type == SDL_EVENT_MOUSE_MOTION ||
                                    event.type == SDL_EVENT_MOUSE_WHEEL;
            float pointerX = 0, pointerY = 0;
            SDL_GetMouseState(&pointerX, &pointerY);
            if (mouseEvent && !cameraLooking && !orbitPivot &&
                (orientationInteracting ||
                 (sceneHovered && layout.scene.width >= 100 && layout.scene.height >= 100 &&
                  orientationRect(layout.scene).contains(pointerX, pointerY)))) {
                clicking = false;
                continue;
            }
            if (mouseEvent && (projectDialog.pending() || browser.dialog.pending() ||
                               (ImGui::GetIO().WantCaptureMouse && !sceneHovered &&
                                !cameraLooking && !orbitPivot) ||
                               (editor.capturesMouse() && !cameraLooking && !orbitPivot &&
                                !(event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
                                  event.button.button == SDL_BUTTON_RIGHT && sceneHovered &&
                                  !editor.dragging()) &&
                                !(SDL_GetModState() & SDL_KMOD_ALT)))) {
                clicking = false;
                continue;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && ImGui::GetIO().WantCaptureKeyboard &&
                !cameraLooking && !orbitPivot) {
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
                orbitPivot.reset();
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
                // Additional buttons during navigation must not select objects,
                // start gizmo gestures or hand capture to another navigation mode.
                if (cameraLooking || orbitPivot) {
                    clicking = false;
                    break;
                }
                if (event.button.button == SDL_BUTTON_RIGHT && sceneHovered &&
                    !riverCurveOwnsPointer(world,
                                           selectedEntity,
                                           camera,
                                           layout.scene,
                                           event.button.x,
                                           event.button.y) &&
                    layout.scene.contains(event.button.x, event.button.y) && !editor.dragging() &&
                    !cameraModifiersHeld(SDL_GetModState())) {
                    // A deliberate Scene RMB press transfers keyboard focus away
                    // from an Inspector/script text edit before starting WASD.
                    ImGui::ClearActiveID();
                    ImGui::SetWindowFocus("Scene");
                    cameraLooking = input.lockSceneCursor();
                    clicking = false;
                }
                if (event.button.button == SDL_BUTTON_LEFT) {
                    orbitPivot.reset();
                    orbitStarted = false;
                    if (sceneHovered && layout.scene.contains(event.button.x, event.button.y) &&
                        (SDL_GetModState() & SDL_KMOD_ALT) &&
                        !(SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) {
                        // A deliberate Scene orbit gesture takes focus from text
                        // fields just like RMB navigation does.
                        ImGui::ClearActiveID();
                        ImGui::SetWindowFocus("Scene");
                        editor.finish(world);
                        if (const auto* pose = world.tryGet<Transform>(selectedEntity)) {
                            orbitPivot = pose->position;
                        } else {
                            orbitPivot = camera.target;
                        }
                        // Re-aim only once movement starts: Alt-click alone must
                        // not jump the view or appear to move an empty origin.
                        if (!input.lockSceneCursor()) {
                            orbitPivot.reset();
                        }
                    }
                    clicking = !(SDL_GetModState() & SDL_KMOD_ALT) &&
                               !terrainEditor.active(world, selectedEntity);
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
                // Relative motion on Windows need not carry a button mask.
                // The explicit button-down/up gesture owns orbit capture.
                if (orbitPivot && (SDL_GetModState() & SDL_KMOD_ALT) &&
                    !(SDL_GetModState() & (SDL_KMOD_CTRL | SDL_KMOD_GUI))) {
                    clicking = false;
                    const auto motion = input.lockedMotion(event.motion);
                    if (!orbitStarted && (motion.first != 0 || motion.second != 0)) {
                        camera.beginOrbit(*orbitPivot);
                        orbitStarted = true;
                    }
                    if (orbitStarted) {
                        camera.orbit(*orbitPivot, motion.first, motion.second);
                    }
                }
                if ((event.motion.state & SDL_BUTTON_MMASK) && sceneHovered &&
                    !ImGui::GetIO().WantTextInput) {
                    clicking = false;
                    if (SDL_GetModState() & SDL_KMOD_SHIFT) {
                        camera.pan(-event.motion.xrel * .08f);
                        camera.zoom(-event.motion.yrel * .025f);
                    } else {
                        const float scale = std::max(camera.distance, .1f) * .002f;
                        camera.target = camera.target +
                                        camera.right() * (-event.motion.xrel * scale) +
                                        camera.up() * (event.motion.yrel * scale);
                    }
                }
                if (cameraLooking) {
                    clicking = false;
                    // Windows relative motion need not carry a button-state mask.
                    if (!cameraModifiersHeld(SDL_GetModState())) {
                        const auto motion = input.lockedMotion(event.motion);
                        camera.look(motion.first, motion.second);
                    }
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (cameraLooking) {
                    clicking = false;
                    break;
                }
                if (event.button.button == SDL_BUTTON_LEFT && orbitPivot) {
                    orbitPivot.reset();
                    orbitStarted = false;
                    input.releaseMouse();
                    clicking = false;
                }
                if (event.button.button == SDL_BUTTON_LEFT && clicking) {
                    clicking = false;

                    // Scene clicks only select; object creation belongs to Create menus.
                    Ray ray;
                    if (mouseRay(
                            window, camera, event.button.x, event.button.y, ray, layout.scene)) {
                        {
                            RayHit hit;
                            const bool found = physics.raycast(ray, kClickReach, hit);
                            // Pick visible geometry before collider proxies. A
                            // model's enclosing root collider must not hide parts.
                            float closest = kClickReach;
                            selectedEntity =
                                ScenePicking::pick(world, browser.assets, ray, closest);
                            if (selectedEntity == Entity{} && found) {
                                selectedEntity = hit.entity;
                                closest = hit.distance;
                            }
                            if (selectModelRoots && world.has<MeshRenderer>(selectedEntity) &&
                                !(SDL_GetModState() & SDL_KMOD_CTRL)) {
                                selectedEntity = ScenePicking::modelRoot(world, selectedEntity);
                            }
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
                if ((sceneHovered || cameraLooking) && !cameraModifiersHeld(SDL_GetModState()) &&
                    !ImGui::GetIO().WantTextInput) {
                    if (cameraLooking) {
                        if (SDL_GetModState() & SDL_KMOD_SHIFT) {
                            cameraBoost = adjustSceneCameraBoost(cameraBoost, event.wheel.y);
                        } else {
                            cameraSpeed = std::clamp(
                                cameraSpeed * std::pow(1.2f, event.wheel.y), .1f, 500.0f);
                        }
                    } else {
                        camera.zoom(event.wheel.y * browser.settings.scrollZoomSpeed);
                        camera.pan(-event.wheel.x);
                    }
                }
                break;
            }
            default:
                break;
            }
        }

        if (!smokeTest && !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) &&
            !(gameWindow && (SDL_GetWindowFlags(gameWindow) & SDL_WINDOW_INPUT_FOCUS))) {
            input.releaseMouse();
            input.clear();
            cameraLooking = false;
            orbitPivot.reset();
            if (!focusSuspended) {
                audio.suspend(true);
                focusSuspended = true;
            }
            profiler.end();
            // Block instead of rendering/simulating at a background frame rate.
            // Preserve the wake event for the normal event handler (including Quit).
            SDL_Event wake;
            if (running && SDL_WaitEvent(&wake)) {
                SDL_PushEvent(&wake);
            }
            lastTicks = SDL_GetTicksNS();
            lastAutosave = std::chrono::steady_clock::now();
            continue;
        }
        if (focusSuspended) {
            audio.suspend(false);
            focusSuspended = false;
            lastTicks = SDL_GetTicksNS();
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        if (cameraLooking || orbitPivot) {
            for (int button = 0; button < 5; ++button) {
                ImGui::GetIO().AddMouseButtonEvent(button, false);
            }
            ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
        } else {
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        }
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
        static RecentProjects recentProjects;
        static bool confirmOpen = false, confirmExit = false;
        static std::string pendingRecent;
        static int continueAfterSave = 0;
        std::string selectedRecent;
        bool openProject = false, saveProject = false, saveAsProject = false;
        const bool projectEnabled = !browser.importing() && !prefabLevel && !levels.busy() &&
                                    !playMode.active() && !editor.dragging() &&
                                    !projectDialog.pending() && !browser.dialog.pending();
        profiler.checkpoint(ProfilePart::Events);
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                openProject = ImGui::MenuItem("Open Project...", "Ctrl+O", false, projectEnabled);
                if (ImGui::BeginMenu("Open Recent", projectEnabled)) {
                    if (recentProjects.paths().empty()) {
                        ImGui::TextDisabled("No recent projects");
                    }
                    for (const auto& path : recentProjects.paths()) {
                        if (ImGui::MenuItem(path.c_str())) {
                            selectedRecent = path;
                            openProject = true;
                        }
                    }
                    ImGui::EndMenu();
                }
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
            if (playMode.active()) {
                projectError = "Stop Play before saving; runtime edits are temporary.";
                return saved;
            }
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
                Terrains::saveAll(world, destination);
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
                recentProjects.remember(path);
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
                if (openProject) {
                    projectDirty = currentProject.empty() || scripts.hasUnsavedChanges() ||
                                   animationWindow.hasUnsavedChanges() ||
                                   projectContentState(world, embeddedLegacySource, camera) !=
                                       savedProjectState;
                }
                if (openProject && projectDirty) {
                    pendingRecent = selectedRecent;
                    confirmOpen = true;
                } else if (openProject && !selectedRecent.empty()) {
                    projectDialog.openRecent(selectedRecent);
                } else {
                    projectDialog.show(window, !openProject, currentProject);
                }
            }
        }
        if (confirmOpen || confirmExit) {
            ImGui::OpenPopup("Unsaved project changes");
        }
        ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Appearing);
        if (ImGui::BeginPopupModal(
                "Unsaved project changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("You have unsaved changes");
            ImGui::Separator();
            ImGui::TextWrapped(
                confirmExit
                    ? "Save your work before closing LegacyEngine?"
                    : "Save your work before opening another project? Discarded changes cannot be recovered.");
            auto proceed = [&]() {
                if (confirmExit) {
                    running = false;
                } else if (!pendingRecent.empty()) {
                    projectDialog.openRecent(pendingRecent);
                } else {
                    projectDialog.show(window, false, currentProject);
                }
                confirmOpen = confirmExit = false;
                pendingRecent.clear();
                ImGui::CloseCurrentPopup();
            };
            if (ImGui::Button("Save and continue", ImVec2(160, 0))) {
                if (!currentProject.empty()) {
                    if (saveTo(currentProject)) {
                        proceed();
                    }
                } else {
                    continueAfterSave = confirmExit ? 2 : 1;
                    confirmOpen = confirmExit = false;
                    ImGui::CloseCurrentPopup();
                    projectDialog.show(window, true, currentProject);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Discard", ImVec2(120, 0))) {
                proceed();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                confirmOpen = confirmExit = false;
                pendingRecent.clear();
                ImGui::CloseCurrentPopup();
            }
            if (!projectError.empty()) {
                ImGui::Spacing();
                ImGui::TextWrapped("%s", projectError.c_str());
            }
            ImGui::EndPopup();
        }
        if (auto result = projectDialog.poll()) {
            if (result->path.empty() || !result->error.empty()) {
                exitAfterSave = false;
                continueAfterSave = 0;
                pendingRecent.clear();
            }
            if (!result->error.empty()) {
                projectError = result->error;
            } else if (!result->path.empty()) {
                if (result->saving) {
                    auto path = std::filesystem::path(result->path);
                    if (!path.has_extension()) {
                        path += ".legacy";
                    }
                    if (saveTo(path.string()) && continueAfterSave) {
                        if (continueAfterSave == 2) {
                            running = false;
                        } else if (!pendingRecent.empty()) {
                            projectDialog.openRecent(pendingRecent);
                        } else {
                            projectDialog.show(window, false, currentProject);
                        }
                        continueAfterSave = 0;
                        pendingRecent.clear();
                    } else if (continueAfterSave) {
                        confirmExit = continueAfterSave == 2;
                        confirmOpen = !confirmExit;
                        continueAfterSave = 0;
                    }
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
                        resetHierarchyExpansion();
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
                        recentProjects.remember(currentProject);
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
            Vec3 position = camera.position() + normalize(camera.target - camera.position()) * 3.f;
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
        const bool editAllowed =
            !editor.dragging() && !projectDialog.pending() && !browser.dialog.pending();
        const bool objectSelected = world.alive(selectedEntity);
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("Edit")) {
                undoRequested =
                    ImGui::MenuItem("Undo",
                                    "Ctrl+Z",
                                    false,
                                    editAllowed && !playMode.active() && history.canUndo());
                redoRequested =
                    ImGui::MenuItem("Redo",
                                    "Ctrl+Y / Ctrl+Shift+Z",
                                    false,
                                    editAllowed && !playMode.active() && history.canRedo());
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
                ImGui::MenuItem("Lighting", nullptr, &lightmappingWindow.visible);
                ImGui::MenuItem("Occlusion Culling", nullptr, &occlusionWindow.visible);
                ImGui::MenuItem("Navigation", nullptr, &navigationWindow.visible);
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
                        scripts.saveFile(browser.assets);
                        const auto migration = fixProjectAssets(browser.assets, projectMeshes);
                        remapLegacyScriptWorld(world, migration);
                        embeddedLegacySource =
                            remapLegacyScriptLiterals(embeddedLegacySource, migration);
                        remapLegacyScriptPath(browser.openedScript, migration);
                        remapLegacyScriptPath(browser.selected, migration);
                        scripts.migrateScriptPaths(migration, browser.assets);
                        if (browser.openedScript.empty()) {
                            scripts.setSource(remapLegacyScriptLiterals(
                                std::string(scripts.source()), migration));
                        }
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
                ImGui::BeginDisabled(playMode.active());
                int textureQuality = renderingSettings.textureQuality;
                if (ImGui::Combo(
                        "Texture quality",
                        &textureQuality,
                        "Full resolution\0Half resolution\0Quarter resolution\0Eighth resolution\0")) {
                    const int before = renderingSettings.textureQuality;
                    history.perform(
                        world, selectedEntity, [&, before, textureQuality](bool forward) {
                            renderingSettings.textureQuality = forward ? textureQuality : before;
                            TextureQuality::project = renderingSettings.textureQuality;
                            renderingSettings.save(browser.assets);
                        });
                }
                ImGui::EndDisabled();
                ImGui::TextWrapped(
                    "Material and terrain textures only. Baked lightmaps, shadow maps, cookies and UI textures retain their authored resolution. Changes stream asynchronously.");
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
                int interval = 0;
                if (SDL_GL_GetSwapInterval(&interval)) {
                    ImGui::Text("Driver swap interval: %d", interval);
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
                ImGui::BeginDisabled(playMode.active());
                ImGui::TextUnformatted("Default (built-in)");
                for (int i = 1; i < 32; ++i) {
                    if (!LayerSettings::names[i].empty()) {
                        ImGui::PushID(i);
                        ImGui::TextUnformatted(LayerSettings::names[i].c_str());
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Remove")) {
                            LayerSettings::resetCollision(i);
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
                            LayerSettings::resetCollision(slot);
                            LayerSettings::names[slot] = newLayer;
                            newLayer[0] = 0;
                            layerSettings.save(browser.assets);
                        }
                    }
                }
                if (ImGui::CollapsingHeader("Layer Collision Matrix",
                                            ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::TextWrapped(
                        "Checked pairs collide and generate trigger/contact events. Uncheck to ignore. Query masks are separate.");
                    std::vector<int> named;
                    for (int i = 0; i < 32; ++i) {
                        if (!LayerSettings::names[i].empty()) {
                            named.push_back(i);
                        }
                    }
                    ImGui::BeginDisabled(playMode.active());
                    if (ImGui::BeginTable("CollisionMatrix",
                                          int(named.size()) + 1,
                                          ImGuiTableFlags_ScrollX | ImGuiTableFlags_BordersInnerV,
                                          ImVec2(0, 250))) {
                        ImGui::TableSetupColumn("Layer", ImGuiTableColumnFlags_WidthFixed, 150);
                        for (int i : named) {
                            ImGui::TableSetupColumn(LayerSettings::names[i].c_str(),
                                                    ImGuiTableColumnFlags_WidthFixed,
                                                    90);
                        }
                        ImGui::TableSetupScrollFreeze(1, 1);
                        ImGui::TableHeadersRow();
                        for (std::size_t row = 0; row < named.size(); ++row) {
                            int a = named[row];
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::TextUnformatted(LayerSettings::names[a].c_str());
                            for (std::size_t col = 0; col < named.size(); ++col) {
                                ImGui::TableNextColumn();
                                if (col > row) {
                                    continue;
                                }
                                int b = named[col];
                                ImGui::PushID(a * 32 + b);
                                bool enabled = LayerSettings::collide(a, b);
                                if (ImGui::Checkbox("##Pair", &enabled)) {
                                    LayerSettings::setCollision(a, b, enabled);
                                    layerSettings.save(browser.assets);
                                    physics.reset();
                                }
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("%s / %s",
                                                      LayerSettings::names[a].c_str(),
                                                      LayerSettings::names[b].c_str());
                                }
                                ImGui::PopID();
                            }
                        }
                        ImGui::EndTable();
                    }
                    ImGui::EndDisabled();
                }
                ImGui::EndDisabled();
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
            ImGui::Text("LegacyEngine %s", EngineVersion::version);
            ImGui::TextUnformatted("3D scene editor with LegacyScript (Lua 5.4)");
            ImGui::TextUnformatted("SDL3 / OpenGL / Dear ImGui / Jolt");
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
            playbackKeys && ImGui::Shortcut(ImGuiKey_F5, ImGuiInputFlags_RouteGlobal);
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
                TextureQuality::lifecycle(true);
                Particles::reset(world);
                lightmappingWindow.cancelBake(); // do not compete with gameplay for CPU time
                focusGame = true;
                if (separateGame && !gameWindow) {
                    gameWindow = SDL_CreateWindow(
                        "Game", 960, 540, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
                    if (!gameWindow) {
                        projectError = SDL_GetError();
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
                scriptGUI.shutdown();
                gameUI.sync(world, false, browser.assets.root, window);
                SDL_DestroyWindow(gameWindow);
                gameWindow = nullptr;
            }
            mouseOver = {};
            mousePressedObject = {};
            scriptGUI.shutdown();
            TextureQuality::lifecycle(false);
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
        Entity hierarchyCreateParent, vehicleWheelsRequest;
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
                             editAllowed ? &hierarchyCreate : nullptr,
                             hierarchyLevelName.c_str(),
                             editAllowed && !playMode.active() && !prefabLevel ? &renameLevelPopup
                                                                               : nullptr,
                             &hierarchyCreateParent,
                             editAllowed ? &vehicleWheelsRequest : nullptr)) {
            editor.finish(world);
            clicking = false;
        }
        if (world.alive(vehicleWheelsRequest)) {
            history.commit(world, selectedEntity);
            Vehicles::createWheelMeshes(world, vehicleWheelsRequest, sphereMesh);
            history.commit(world, selectedEntity);
        }
        if (hierarchyCreate >= 0) {
            createObject(hierarchyCreate);
            if (world.alive(hierarchyCreateParent) && world.alive(selectedEntity)) {
                setParent(world, selectedEntity, hierarchyCreateParent);
                setLocalTransform(world, selectedEntity, Transform{});
            }
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
                      browser.selected,
                      playMode.active());
        terrainEditor.draw(
            world, selectedEntity, browser.assets, history, editAllowed, !playMode.active());
        scripts.componentOutput = componentScripts.messages();
        scripts.setAssetContext(browser.openedScript, selectedEntity);
        ImGui::BeginDisabled(!editAllowed || playMode.active());
        scripts.draw(&layout,
                     playMode.playing(),
                     &browser.assets,
                     &browser.openedScript,
                     &browser,
                     editAllowed && !playMode.active(),
                     &history);
        ImGui::EndDisabled();
        browser.projectFolder = currentProject.empty()
                                    ? browser.assets.root
                                    : std::filesystem::absolute(currentProject).parent_path();
        browser.draw(
            window, world, selectedEntity, scripts, editAllowed && !playMode.active(), &history);
        if (!browser.movedAsset.first.empty()) {
            const auto moved = std::exchange(browser.movedAsset, {});
            animationWindow.remapAssetPaths(moved.first, moved.second);
            modelPreviewWindow.visible = false;
        }

        if (!browser.previewModelRequest.empty()) {
            modelPreviewWindow.open(
                browser.assets, std::exchange(browser.previewModelRequest, {}), renderer);
        }
        modelPreviewWindow.draw(browser.assets, renderer, ImGui::GetIO().DeltaTime);
        navigationWindow.draw(world,
                              browser.assets,
                              projectMeshes,
                              levels.current(),
                              editAllowed && !playMode.active() && !prefabLevel);
        occlusionWindow.draw(world,
                             renderer,
                             browser.assets,
                             projectMeshes,
                             levels.current(),
                             editAllowed && !playMode.active() && !prefabLevel);
        lightmappingWindow.draw(world,
                                selectedEntity,
                                browser.assets,
                                projectMeshes,
                                history,
                                editAllowed && !playMode.active() && !prefabLevel,
                                levels.current());
        if (!browser.editAnimationRequest.empty()) {
            animationWindow.open(browser.assets, std::exchange(browser.editAnimationRequest, {}));
        }
        animationWindow.draw(world,
                             selectedEntity,
                             browser.assets,
                             renderer,
                             history,
                             editAllowed && !playMode.active(),
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
            ImGui::BeginDisabled(playMode.active());
            if (ImGui::Button("Save Cubemap")) {
                try {
                    saveCubemap(browser.assets, editingCubemap, cubemapDraft);
                } catch (const std::exception& e) {
                    browser.assets.error = e.what();
                }
            }
            ImGui::EndDisabled();
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
        if (!browser.spawnModelRequest.empty()) {
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
                world.add(root,
                          Transform{camera.position() +
                                    normalize(camera.target - camera.position()) * 3.f});
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
                    world.add(child,
                              Transform{camera.position() +
                                        normalize(camera.target - camera.position()) * 3.f});
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
        if (!browser.spawnPrefabRequest.empty()) {
            const auto path = std::exchange(browser.spawnPrefabRequest, {});
            try {
                history.commit(world, selectedEntity);
                const auto parent = prefabLevel ? Prefabs::root(world) : Entity{};
                selectedEntity = Prefabs::spawn(
                    browser.assets,
                    path,
                    world,
                    projectMeshes,
                    camera.position() + normalize(camera.target - camera.position()) * 3.f,
                    {});
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
        ImGui::BeginDisabled(playMode.active());
        browser.settings.draw(browser.projectFolder);
        ImGui::EndDisabled();
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
        inputSettings.draw(browser.assets, input, editAllowed && !playMode.active());
        if (editAllowed && !playMode.active() &&
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
        terminalErrors.report("Skybox", renderer.skyboxError());
        terminalErrors.report("Material", renderer.materialError);
        terminalErrors.report("Model", renderer.modelError);
        terminalErrors.report("Terrain foliage", renderer.foliageError());
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
                ImGui::SetNextWindowContentSize(
                    ImVec2(std::max(480.f, ImGui::GetContentRegionAvail().x - 16), 0));
                ImGui::BeginChild("Scene toolbar",
                                  ImVec2(0,
                                         (ImGui::GetContentRegionAvail().x < 900 ? 2.f : 1.f) *
                                                 ImGui::GetFrameHeightWithSpacing() +
                                             6),
                                  ImGuiChildFlags_AlwaysUseWindowPadding,
                                  ImGuiWindowFlags_HorizontalScrollbar |
                                      ImGuiWindowFlags_NoScrollWithMouse);
                editor.drawToolbar();
                ImGui::SameLine();
                ImGui::Checkbox("Select model roots", &selectModelRoots);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Off: select the clicked child/mesh part. On: select its assembly root. Ctrl-click always selects the part. Use Hierarchy for hidden children.");
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100);
                if (ImGui::SliderFloat(
                        "Scroll zoom", &browser.settings.scrollZoomSpeed, .1f, 5.f, "%.1fx")) {
                    browser.settings.save();
                }
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
                sceneSurface.resize(pixels.width, pixels.height);
                ImGui::Image(static_cast<ImTextureID>(sceneSurface.texture),
                             ImVec2(sceneRect.width, sceneRect.height),
                             ImVec2(0, 1),
                             ImVec2(1, 0));
                if (world.has<Camera>(selectedEntity) && world.has<Transform>(selectedEntity)) {
                    const auto& selectedCamera = world.get<Camera>(selectedEntity);
                    const float aspect = std::clamp(selectedCamera.aspect, .25f, 4.f);
                    float width = std::min(240.f, sceneRect.width * .35f);
                    float height = width / aspect;
                    if (height > sceneRect.height * .4f) {
                        height = sceneRect.height * .4f;
                        width = height * aspect;
                    }
                    cameraPreviewSurface.resize(std::max(1, int(width)), std::max(1, int(height)));
                    const ImVec2 a(sceneRect.x + sceneRect.width - width - 10,
                                   sceneRect.y + sceneRect.height - height - 10),
                        b(a.x + width, a.y + height);
                    sceneDrawList->AddRectFilled(ImVec2(a.x - 2, a.y - 20),
                                                 ImVec2(b.x + 2, b.y + 2),
                                                 IM_COL32(24, 24, 24, 245));
                    sceneDrawList->AddText(ImVec2(a.x, a.y - 18), IM_COL32_WHITE, "Camera preview");
                    sceneDrawList->AddImage(static_cast<ImTextureID>(cameraPreviewSurface.texture),
                                            a,
                                            b,
                                            ImVec2(0, 1),
                                            ImVec2(1, 0));
                }
                const bool imageHovered = ImGui::IsItemHovered();
                sceneHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_None) && imageHovered;
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
                const bool playbackHovered = drawParticlePlayback(
                    world, selectedEntity, sceneRect, playMode.active(), particlePreviewSpeed);
                sceneHovered &= !playbackHovered;
                if (sceneHovered && ImGui::IsMouseClicked(0)) {
                    browser.selected.clear();
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
                    camera,
                    sceneRect,
                    editAllowed && !playbackHovered && !cameraLooking && !editor.dragging());
                const auto* sceneRiver = world.tryGet<River>(selectedEntity);
                bool curveEditing = sceneRiver && sceneRiver->editCurve;
                bool curveInteracting =
                    drawRiverCurve(world,
                                   selectedEntity,
                                   camera,
                                   sceneRect,
                                   history,
                                   editAllowed && !cameraLooking && !orientationInteracting &&
                                       !(SDL_GetModState() & SDL_KMOD_ALT));
                curveEditing = sceneRiver && sceneRiver->editCurve;
                if (curveInteracting) {
                    clicking = false;
                }
                editor.draw(world,
                            selectedEntity,
                            !projectDialog.pending() && !browser.dialog.pending() &&
                                !orientationInteracting && !playbackHovered && !cameraLooking &&
                                !terrainEditor.active(world, selectedEntity) && !curveEditing &&
                                (!world.has<BoneAttachment>(selectedEntity) ||
                                 !world.get<BoneAttachment>(selectedEntity).enabled) &&
                                (!(SDL_GetModState() & SDL_KMOD_ALT) || orbitPivot.has_value()),
                            camera,
                            static_cast<float>(sceneSurface.width) / sceneSurface.height,
                            &sceneRect,
                            sceneDrawList,
                            &browser.assets,
                            orbitPivot.has_value());
                if (editor.capturesMouse() || orientationInteracting) {
                    clicking = false;
                }
            }
            ImGui::End();
            ImGui::PopStyleVar();
        }
        {
            Ray terrainRay;
            const auto mouse = ImGui::GetIO().MousePos;
            const auto* curveRiver = world.tryGet<River>(selectedEntity);
            const bool brushHover = !(curveRiver && curveRiver->editCurve) && editAllowed &&
                                    sceneHovered && !cameraLooking &&
                                    !(SDL_GetModState() & SDL_KMOD_ALT);
            const bool brushDown = brushHover && ImGui::IsMouseDown(0);
            bool rayValid =
                brushHover && mouseRay(window, camera, mouse.x, mouse.y, terrainRay, layout.scene);
            if (rayValid && sceneSurface.width > 0 && sceneSurface.height > 0) {
                terrainEditor.preview(
                    world,
                    selectedEntity,
                    rayValid ? &terrainRay : nullptr,
                    camera.viewProjection(float(sceneSurface.width) / sceneSurface.height),
                    layout.scene,
                    sceneDrawList);
            }
            terrainEditor.stroke(world,
                                 selectedEntity,
                                 history,
                                 rayValid ? &terrainRay : nullptr,
                                 brushDown,
                                 ImGui::GetIO().DeltaTime);
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
                gameFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
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
                    gameSurface.resize(static_cast<int>(size.x * dw / std::max(windowWidth, 1)),
                                       static_cast<int>(size.y * dh / std::max(windowHeight, 1)));
                    world.each<Camera>([&](Entity, Camera& c) {
                        c.aspect = static_cast<float>(gameSurface.width) / gameSurface.height;
                    });
                    gameImageOrigin = ImGui::GetCursorScreenPos();
                    gameImageSize = size;
                    ImGui::Image(static_cast<ImTextureID>(gameSurface.texture),
                                 size,
                                 ImVec2(0, 1),
                                 ImVec2(1, 0));
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
        ImGui::Render();
        finishEditorTheme(ImGui::GetDrawData());
        profiler.checkpoint(ProfilePart::Editor);

        // Resolve gameplay focus using this frame's UI, not yesterday's tab state.
        input.enabled = ((SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) || externalFocus) &&
                        playMode.playing() && !cameraLooking &&
                        (gameFocused || externalFocus || input.mouseLocked) &&
                        (!ImGui::GetIO().WantTextInput || gameFocused || input.mouseLocked) &&
                        !inputSettings.visible && !gameUI.capturesKeyboard() &&
                        !scriptGUI.capturesKeyboard();
        input.synchronizeKeyboard();
        // The time that has passed since the previous frame.
        const Uint64 ticks = SDL_GetTicksNS();
        const float seconds = static_cast<float>(ticks - lastTicks) * 1e-9f;
        lastTicks = ticks;

        if (cameraLooking &&
            (!sceneVisible || browser.dialog.pending() || projectDialog.pending() ||
             !(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS))) {
            cameraLooking = false;
            input.releaseMouse();
        }
        if (cameraLooking && !browser.dialog.pending() && !projectDialog.pending() &&
            !cameraModifiersHeld(SDL_GetModState()) && !editor.dragging() &&
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
        if (gameWindow && playMode.active()) {
            int gw = 0, gh = 0;
            SDL_GetWindowSizeInPixels(gameWindow, &gw, &gh);
            gameSurface.resize(std::max(1, gw), std::max(1, gh));
            gameVisible = true;
            const auto active = activeCamera(world);
            if (active != Entity{}) {
                world.get<Camera>(active).aspect = float(std::max(1, gw)) / std::max(1, gh);
            }
        }
        if (playMode.playing()) {
            world.each<CameraEffects>([&](Entity, CameraEffects& fx) {
                float step = fx.fadeSpeed * std::min(seconds, .25f);
                if (fx.fadeAmount < fx.fadeTarget) {
                    fx.fadeAmount = std::min(fx.fadeTarget, fx.fadeAmount + step);
                } else {
                    fx.fadeAmount = std::max(fx.fadeTarget, fx.fadeAmount - step);
                }
            });
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
        if (playMode.playing()) {
            navigationWindow.system.update(world, seconds);
            updateHierarchy(world);
        }
        profiler.checkpoint(ProfilePart::Hierarchy);
        physics.setAssetRoot(browser.assets.root);
        Terrains::update(world, browser.assets);
        physics.update(world, playMode.playing() ? seconds : 0.0f, [&](float dt) {
            input.beginFixedInput();
            scripts.fixedUpdate(dt);
            componentScripts.fixedUpdate(dt);
            input.endFixedInput();
            updateCharacterControllers(world, physics);
            updateHierarchy(world);
            BoneAttachments::update(world, browser.assets);
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
        if (playMode.playing()) {
            Vehicles::updateWheelMeshes(world, seconds);
            updateHierarchy(world);
        }
        profiler.checkpoint(ProfilePart::Hierarchy);
        bool wantsTPSLock = false;
        if (playMode.playing() && input.enabled && input.mouseEnabled && !cameraLooking) {
            const auto active = activeCamera(world);
            if (const auto* c = world.tryGet<TPSCamera>(active)) {
                const Entity target = c->target != Entity{} ? c->target : getParent(world, active);
                wantsTPSLock =
                    c->enabled && c->lockMouse && target != active && world.has<Transform>(target);
            }
        }
        if (wantsTPSLock && !input.mouseLocked) {
            tpsMouseOwned = input.lockCursor(true);
        } else if (!wantsTPSLock && tpsMouseOwned) {
            // Focus/event processing may already have handed capture to Scene.
            // Release only the gameplay lock we owned, never the editor's lock.
            if (input.mouseLocked && !input.sceneLocked) {
                input.releaseMouse();
            }
            tpsMouseOwned = false;
        }
        if (playMode.playing()) {
            updateFPSCameras(world, std::min(seconds, .25f), input);
            updateTPSCameras(world, physics, std::min(seconds, .25f), input);
        }
        BoneAttachments::update(world, browser.assets);
        updateHierarchy(world);
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
            if (inside && !gameUI.capturesMouse() && !scriptGUI.capturesMouse() && u >= 0 &&
                u <= 1 && v >= 0 && v <= 1 && cameraEntity != Entity{}) {
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

        Lasers::update(world, physics);
        Particles::update(world,
                          float(std::min(seconds, .1f)),
                          playMode.playing(),
                          playMode.active() && !playMode.playing(),
                          selectedEntity,
                          particlePreviewSpeed,
                          [&](Entity emitter, const Ray& ray, float distance, RayHit& hit) {
                              return physics.raycast(ray, distance, hit, [&](Entity other) {
                                  return other != emitter && isActive(world, other);
                              });
                          });
        profiler.checkpoint(ProfilePart::Audio);
        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        scripts.console().setAssetRoot(browser.assets.root);
        renderer.setShadowSettings(renderingSettings.shadows);
        renderer.setAssetRoot(browser.assets.root);
        if (width > 0 && height > 0) {
            // Keep the unfocused editor view live at 15 Hz during gameplay,
            // rather than rebuilding its full shadow atlas at the Game frame rate.
            const auto renderTime = std::chrono::steady_clock::now();
            const bool backgroundScene = playMode.playing() && gameVisible &&
                                         (gameWindow || (!sceneFocused && !sceneHovered));
            const bool renderScene =
                sceneVisible && (!backgroundScene || sceneSurface.width != lastSceneWidth ||
                                 sceneSurface.height != lastSceneHeight ||
                                 renderTime - lastSceneRender >= std::chrono::milliseconds(66));
            if (renderScene) {
                lastSceneRender = renderTime;
                lastSceneWidth = sceneSurface.width;
                lastSceneHeight = sceneSurface.height;
                glBindFramebuffer(GL_FRAMEBUFFER, sceneSurface.framebuffer);
                const float aspect = static_cast<float>(sceneSurface.width) / sceneSurface.height;
                const Mat4 viewProjection = camera.viewProjection(aspect);
                const bool visibilityPreview =
                    occlusionWindow.visible && occlusionWindow.visualizeSelectedCamera &&
                    world.has<Camera>(selectedEntity) && world.has<Transform>(selectedEntity) &&
                    validCamera(world.get<Camera>(selectedEntity));
                renderer.draw(world,
                              viewProjection,
                              sceneSurface.width,
                              sceneSurface.height,
                              camera.position(),
                              0,
                              0,
                              true,
                              nullptr,
                              0,
                              visibilityPreview ? selectedEntity : Entity{});
                if (visibilityPreview) {
                    occlusionWindow.previewValid = true;
                    occlusionWindow.previewCulled = renderer.occlusion.culled;
                    occlusionWindow.previewTested = renderer.occlusion.tested;
                }
                if (!visibilityPreview) {
                    renderer.drawEditorGrid(browser.settings.grid, viewProjection);
                    if (navigationWindow.showMesh && navigationWindow.system.valid) {
                        renderer.drawNavigation(navigationWindow.system.debugMesh, viewProjection);
                    }
                    if (browser.settings.alwaysShowColliders) {
                        world.each<Transform, Collider>(
                            [&](Entity e, const Transform& t, const Collider& c) {
                                if (e != selectedEntity) {
                                    renderer.drawCollider(
                                        t, effectiveCollider(world, e, c), viewProjection, .25f);
                                }
                            });
                    }
                    if (world.alive(selectedEntity)) {
                        const auto* transform = world.tryGet<Transform>(selectedEntity);
                        const auto* collider = world.tryGet<Collider>(selectedEntity);
                        if (transform) {
                            if (const auto* vehicle = world.tryGet<Vehicle>(selectedEntity)) {
                                renderer.drawVehicle(*transform, *vehicle, viewProjection);
                            }
                        }
                        if (transform && collider) {
                            renderer.drawCollider(
                                *transform,
                                effectiveCollider(world, selectedEntity, *collider),
                                viewProjection);
                        }
                        if (transform) {
                            if (const auto* query = world.tryGet<Raycast>(selectedEntity)) {
                                renderer.drawRaycast(*transform,
                                                     *query,
                                                     viewProjection,
                                                     browser.settings.raycastThickness);
                            }
                        }
                        const auto* audioSource = world.tryGet<AudioSource>(selectedEntity);
                        if (transform && audioSource) {
                            renderer.drawAudioSource(*transform, *audioSource, viewProjection);
                        }
                        if (transform) {
                            if (const auto* selectedCamera = world.tryGet<Camera>(selectedEntity)) {
                                renderer.drawCamera(*transform, *selectedCamera, viewProjection);
                            }
                        }
                    } else {
                        selectedEntity = {};
                    }
                }
            }
            profiler.checkpoint(ProfilePart::Scene);
            if (renderScene && world.has<Camera>(selectedEntity) &&
                world.has<Transform>(selectedEntity)) {
                auto previewCamera = world.get<Camera>(selectedEntity);
                const auto& pose = world.get<Transform>(selectedEntity);
                previewCamera.aspect =
                    float(cameraPreviewSurface.width) / std::max(1, cameraPreviewSurface.height);
                glBindFramebuffer(GL_FRAMEBUFFER, cameraPreviewSurface.framebuffer);
                try {
                    renderer.draw(world,
                                  cameraMatrix(pose, previewCamera),
                                  cameraPreviewSurface.width,
                                  cameraPreviewSurface.height,
                                  cameraEye(pose, previewCamera),
                                  0,
                                  0,
                                  false,
                                  world.tryGet<CameraEffects>(selectedEntity),
                                  (std::uint64_t(selectedEntity.generation) << 32) |
                                      selectedEntity.index | (std::uint64_t(1) << 63));
                } catch (const std::exception& error) {
                    terminalErrors.report("Camera preview", error.what());
                }
            }
            if (gameVisible && activeCamera(world) != Entity{}) {
                const auto e = activeCamera(world);
                const auto& c = world.get<Camera>(e);
                const auto& t = world.get<Transform>(e);
                glBindFramebuffer(GL_FRAMEBUFFER, gameSurface.framebuffer);
                try {
                    const int x = static_cast<int>(c.viewportX * gameSurface.width),
                              y = static_cast<int>((1 - c.viewportY - c.viewportHeight) *
                                                   gameSurface.height);
                    const auto gameVP = cameraMatrix(t, c);
                    renderer.draw(
                        world,
                        gameVP,
                        std::max(1, static_cast<int>(c.viewportWidth * gameSurface.width)),
                        std::max(1, static_cast<int>(c.viewportHeight * gameSurface.height)),
                        cameraEye(t, c),
                        x,
                        y,
                        false,
                        world.tryGet<CameraEffects>(e),
                        (std::uint64_t(e.generation) << 32) | e.index);
                    if (showGameColliders) {
                        world.each<Transform, Collider>([&](Entity entity,
                                                            const Transform& transform,
                                                            const Collider& collider) {
                            renderer.drawCollider(
                                transform, effectiveCollider(world, entity, collider), gameVP);
                        });
                    }
                    terminalErrors.report("Game rendering", "");
                } catch (const std::exception& error) {
                    terminalErrors.report("Game rendering", error.what());
                    glClearColor(.1f, .1f, .1f, 1);
                    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                }
            }
            if (gameVisible && activeCamera(world) == Entity{}) {
                glBindFramebuffer(GL_FRAMEBUFFER, gameSurface.framebuffer);
                glDisable(GL_SCISSOR_TEST);
                glClearColor(.1f, .1f, .1f, 1);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            }
            if (gameVisible && playMode.active()) {
                glBindFramebuffer(GL_FRAMEBUFFER, gameSurface.framebuffer);
                gameUI.render(gameSurface.width, gameSurface.height, playMode.playing());
                scriptGUI.render(input.window,
                                 gameSurface.width,
                                 gameSurface.height,
                                 playMode.playing(),
                                 browser.assets.root);
            }
            if (gameWindow && gameVisible) {
                auto context = SDL_GL_GetCurrentContext();
                if (SDL_GL_MakeCurrent(gameWindow, context)) {
                    int w = 1, h = 1;
                    SDL_GetWindowSizeInPixels(gameWindow, &w, &h);
                    glDisable(GL_SCISSOR_TEST);
                    glBindFramebuffer(GL_READ_FRAMEBUFFER, gameSurface.framebuffer);
                    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
                    glBlitFramebuffer(0,
                                      0,
                                      gameSurface.width,
                                      gameSurface.height,
                                      0,
                                      0,
                                      w,
                                      h,
                                      GL_COLOR_BUFFER_BIT,
                                      GL_LINEAR);
                    if (showFPS) {
                        glBindFramebuffer(GL_FRAMEBUFFER, 0);
                        ImDrawList overlay(ImGui::GetDrawListSharedData());
                        overlay._ResetForNewFrame();
                        overlay.PushClipRect(ImVec2(0, 0), ImVec2(float(w), float(h)));
                        overlay.PushTexture(ImGui::GetIO().Fonts->TexRef);
                        char label[64];
                        std::snprintf(label, sizeof(label), "FPS: %.0f", ImGui::GetIO().Framerate);
                        const auto text = ImGui::CalcTextSize(label);
                        overlay.AddRectFilled(
                            ImVec2(4, 4), ImVec2(text.x + 16, text.y + 16), IM_COL32(0, 0, 0, 180));
                        overlay.AddText(ImVec2(8, 8), IM_COL32_WHITE, label);
                        overlay.PopTexture();
                        overlay.PopClipRect();
                        ImDrawData data;
                        data.Valid = true;
                        data.DisplayPos = ImVec2(0, 0);
                        data.DisplaySize = ImVec2(float(w), float(h));
                        data.FramebufferScale = ImVec2(1, 1);
                        data.Textures = ImGui::GetDrawData()->Textures;
                        data.AddDrawList(&overlay);
                        ImGui_ImplOpenGL3_RenderDrawData(&data);
                    }
                    // Present only once with VSync per frame. Waiting for both
                    // windows can halve the frame rate on multi-window drivers.
                    int interval = 0;
                    const bool restoreInterval = SDL_GL_GetSwapInterval(&interval) && interval != 0;
                    const bool changedInterval = restoreInterval && SDL_GL_SetSwapInterval(0);
                    SDL_GL_SwapWindow(gameWindow);
                    SDL_GL_MakeCurrent(window, context);
                    if (changedInterval) {
                        SDL_GL_SetSwapInterval(interval);
                    }
                } else {
                    terminalErrors.report("Game window", SDL_GetError());
                }
            }
            profiler.checkpoint(ProfilePart::Game);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, width, height);
            glDisable(GL_SCISSOR_TEST);
            glClearColor(.07f, .08f, .10f, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            profiler.checkpoint(ProfilePart::UI);
        }

        // Compare persisted content, not history depth: Undo back to the saved
        // state clears the marker, while Lua/camera edits also mark it dirty.
        static auto lastDirtyCheck = std::chrono::steady_clock::time_point{};
        if (!prefabLevel && !playMode.active() && !editor.dragging() &&
            std::chrono::steady_clock::now() - lastDirtyCheck >= std::chrono::milliseconds(200)) {
            lastDirtyCheck = std::chrono::steady_clock::now();
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
                    scriptGUI.shutdown();
                    gameUI.sync(world, false, browser.assets.root, window);
                    SDL_DestroyWindow(gameWindow);
                    gameWindow = nullptr;
                }
                mouseOver = {};
                mousePressedObject = {};
                scriptGUI.shutdown();
                TextureQuality::lifecycle(false);
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
                confirmExit = true;
            }
        }
        if (cameraLooking || orbitPivot || input.mouseLocked) {
            SDL_Rect rect{};
            if (input.lockedWindow == gameWindow && gameWindow) {
                SDL_GetWindowSize(gameWindow, &rect.w, &rect.h);
            } else {
                const auto origin = ImGui::GetMainViewport()->Pos;
                rect = (cameraLooking || orbitPivot) ? SDL_Rect{int(sceneRect.x - origin.x),
                                                                int(sceneRect.y - origin.y),
                                                                int(sceneRect.width),
                                                                int(sceneRect.height)}
                                                     : SDL_Rect{int(gameImageOrigin.x - origin.x),
                                                                int(gameImageOrigin.y - origin.y),
                                                                int(gameImageSize.x),
                                                                int(gameImageSize.y)};
                int w, h;
                SDL_GetWindowSize(window, &w, &h);
                const int right = std::min(w, rect.x + rect.w),
                          bottom = std::min(h, rect.y + rect.h);
                rect.x = std::max(0, rect.x);
                rect.y = std::max(0, rect.y);
                rect.w = right - rect.x;
                rect.h = bottom - rect.y;
            }
            if (!input.constrain(rect)) {
                input.releaseMouse();
                cameraLooking = false;
                orbitPivot.reset();
            }
        } else if (input.lockedWindow) {
            input.releaseMouse();
        }
        const std::string title =
            projectWindowTitle(currentProject, projectDirty, levels.current());
        if (title != lastWindowTitle) {
            SDL_SetWindowTitle(window, title.c_str());
            lastWindowTitle = title;
        }
        profiler.checkpoint(ProfilePart::Other);
        SDL_GL_SwapWindow(window);
        const auto flags = SDL_GetWindowFlags(window);
        const bool foreground =
            (!(flags & SDL_WINDOW_MINIMIZED) && (flags & SDL_WINDOW_INPUT_FOCUS)) ||
            (gameWindow && (SDL_GetWindowFlags(gameWindow) & SDL_WINDOW_INPUT_FOCUS));
        const int limit = foreground ? browser.settings.maxFps : 30;
        int swapInterval = 0;
        const bool vsyncActive = SDL_GL_GetSwapInterval(&swapInterval) && swapInterval != 0;
        // Do not stack a fractional-refresh sleep over VSync: it can turn a
        // 144 Hz display into 72 FPS. Let VSync pace focused windows itself.
        if (!smokeTest && limit > 0 && (!foreground || !vsyncActive)) {
            const Uint64 budget = 1000000000ull / Uint64(limit),
                         elapsed = SDL_GetTicksNS() - frameStarted;
            if (elapsed < budget) {
                SDL_DelayNS(budget - elapsed); // sleep; never busy-spin
            }
        }
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
        scriptGUI.shutdown();
        gameUI.sync(world, false, browser.assets.root, window);
        SDL_DestroyWindow(gameWindow);
    }
    scripts.shutdown();
    input.releaseMouse();
    return (!smokeTest || renderedFrames >= smokeFrames);
}

int engineMain(int argc, char** argv) {
    bool smokeTest = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--smoke-test") == 0) {
            smokeTest = true;
        } else if (std::strcmp(argv[i], "-debug") == 0) {
            // Console setup happens before logging is initialized in main.
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

    // Prefer 4x anti-aliasing so the grid lines look smooth, but not every
    // driver offers it: fall back to a plain framebuffer.
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    createWindow(4, window, context);
    if (!context) {
        createWindow(0, window, context);
    }
    if (!context) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not create an OpenGL 3.3 window: %s",
                     SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GL_SetSwapInterval(1); // vsync, where the driver allows it

    // If the renderer cannot start, the reason is already logged: skip the
    // loop and exit with an error.
    SDL_Log("Initializing renderer");
    Renderer renderer;
    const bool rendererReady = renderer.init();
    bool runSucceeded = false;
    if (rendererReady) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        ImGui::GetIO().ConfigDebugHighlightIdConflicts = false;
        GImGui->ErrorCallback = +[](ImGuiContext*, void*, const char* message) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ImGui: %s", message);
        };
        ImGui::GetIO().ConfigErrorRecoveryEnableTooltip = false;
        ImGui::GetIO().ConfigErrorRecoveryEnableAssert = false;
        ImGui::GetIO().ConfigErrorRecoveryEnableDebugLog = true;
        dockedEditor = true;
        ImGui::GetIO().IniFilename = "legacy-layout.ini";
        applyEditorTheme();
        if (!ImGui_ImplSDL3_InitForOpenGL(window, context) ||
            !ImGui_ImplOpenGL3_Init("#version 330 core")) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not initialise Dear ImGui");
            ImGui::DestroyContext();
            renderer.shutdown();
            SDL_GL_DestroyContext(context);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        SDL_Log("Editor started");
        runSucceeded = run(window, renderer, smokeTest);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        renderer.shutdown();
    }

    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (smokeTest && runSucceeded) {
        SDL_Log("Smoke test passed: rendered 120 frames and shut down cleanly");
    }
    return rendererReady && runSucceeded ? 0 : 1;
}

int main(int argc, char** argv) {
#ifdef _WIN32
    bool debugConsole = false;
    for (int i = 1; i < argc; ++i) {
        debugConsole |= std::strcmp(argv[i], "-debug") == 0;
    }
    if (debugConsole &&
        (GetConsoleWindow() || AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole())) {
        std::freopen("CONOUT$", "w", stdout);
        std::freopen("CONOUT$", "w", stderr);
        std::freopen("CONIN$", "r", stdin);
    }
#endif
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
