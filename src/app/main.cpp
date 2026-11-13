#include <cctype>
// LegacyEngine - Scene editing and runtime orchestration.
//
// World conventions:
//   * 1 unit = 1 metre
//   * +Y is up, the ground is the XZ plane (y = 0)
//   * right-handed coordinates (the OpenGL convention)

#include "audio/audio_system.hpp"
#include "core/components.hpp"
#include "core/ecs.hpp"
#include "editor/asset_browser.hpp"
#include "editor/create_menu.hpp"
#include "editor/dock_workspace.hpp"
#include "editor/editor_history.hpp"
#include "editor/editor_input.hpp"
#include "editor/editor_layout.hpp"
#include "editor/editor_theme.hpp"
#include "editor/input_settings.hpp"
#include "editor/inspector.hpp"
#include "editor/level_objects.hpp"
#include "editor/play_mode.hpp"
#include "editor/playback_controls.hpp"
#include "editor/profiler_window.hpp"
#include "editor/scene_orientation.hpp"
#include "editor/script_editor.hpp"
#include "editor/transform_editor.hpp"
#include "levels/level_system.hpp"
#include "physics/collider_debug.hpp"
#include "physics/physics.hpp"
#include "platform/engine_log.hpp"
#include "platform/project_dialog.hpp"
#include "prefabs/prefab.hpp"
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
    world.add(cube, MeshRenderer{cubeMesh, {0.90f, 0.47f, 0.13f}});
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
    bool freeCamera = true;
    float cameraBoost = 3.0f;
    float cameraSpeed = 5.0f;
    Entity selectedEntity;
    ScriptEditor scripts;
    scripts.bindWorld(world, cubeMesh, sphereMesh, capsuleMesh, coneMesh, planeMesh);
    EditorHistory history(world);
    PlayMode playMode;
    ObjectClipboard clipboard;
    AssetBrowser browser;
    ScriptComponents componentScripts;
    AudioSystem audio;
    SceneSurface sceneSurface, gameSurface;
    EditorRect sceneRect{};
    bool sceneHovered = false, resetWorkspace = false;
    Entity editSelection;
    TransformEditor editor;
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
    std::string currentProject, projectError, savedProjectState, lastWindowTitle;
    bool projectDirty = true;
    InputState input;
    input.window = window;
    InputSettings inputSettings;
    EngineLog::Errors terminalErrors;
    bool gameHovered = false, gameFocused = false, focusGame = false;
    std::optional<EditorSettings> settingsGesture;
    bool separateGame = false;
    SDL_Window* gameWindow = nullptr;
    Entity mouseOver, mousePressedObject;
    ImVec2 gameImageOrigin{}, gameImageSize{};
    bool showFPS = false, showGameColliders = false, showProfiler = false;
    FrameProfiler profiler;
    activeProfiler = &profiler;
    Uint64 lastTicks = SDL_GetTicksNS();
    bool running = true;

    // The left mouse button does two things. Pressed and released in one
    // place, it is a click, which selects. Moving away cancels
    // the click; camera navigation uses the right button and keyboard.
    bool clicking = false; // the button is down and this may still be a click
    float pressX = 0.0f;   // where the button went down
    float pressY = 0.0f;

    bool cameraLooking = false, orientationInteracting = false;
    unsigned renderedFrames = 0;
    constexpr unsigned smokeFrames = 120;
    while (running) {
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
        input.enabled = ((SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) || externalFocus) &&
                        playMode.playing() && (gameFocused || externalFocus || input.mouseLocked) &&
                        !ImGui::GetIO().WantTextInput && !inputSettings.visible;
        input.mouseEnabled = externalFocus || gameHovered || sceneHovered || input.mouseLocked;
        input.beginFrame();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (gameWindow && event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                event.window.windowID == SDL_GetWindowID(gameWindow)) {
                input.releaseMouse();
                input.window = window;
                SDL_DestroyWindow(gameWindow);
                gameWindow = nullptr;
                continue;
            }
            if (gameWindow && SDL_GetWindowFromEvent(&event) == gameWindow) {
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
            }
            ImGui_ImplSDL3_ProcessEvent(&event);
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
                 (editor.capturesMouse() && !(SDL_GetModState() & SDL_KMOD_ALT)))) {
                clicking = false;
                continue;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && ImGui::GetIO().WantCaptureKeyboard) {
                continue;
            }
            switch (event.type) {
            case SDL_EVENT_DROP_FILE:
                if (!playMode.active() && event.drop.data) {
                    browser.importFile(event.drop.data);
                }
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                editor.finish(world);
                clicking = false;
                break;
            case SDL_EVENT_QUIT:
                SDL_Log("Shutdown requested: SDL quit event");
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                break; // Escape is reserved for UI cancellation, never application exit.
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_RIGHT && !playMode.active() && freeCamera &&
                    sceneHovered && layout.scene.contains(event.button.x, event.button.y) &&
                    !ImGui::GetIO().WantTextInput && !cameraModifiersHeld(SDL_GetModState())) {
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
                if (!playMode.active() && freeCamera && (sceneHovered || cameraLooking) &&
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

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();
        bool openProject = false, saveProject = false, saveAsProject = false;
        const bool projectEnabled = !prefabLevel && !levels.busy() && !playMode.active() &&
                                    !editor.dragging() && !projectDialog.pending() &&
                                    !browser.dialog.pending();
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
                        scripts.saveFile(browser.assets);
                        levels.saveCurrent(world, embeddedLegacySource, camera);
                    } catch (const std::exception& e) {
                        levels.error = e.what();
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Quit")) {
                    SDL_Log("Shutdown requested: File > Quit");
                    running = false;
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
            auto assetRoot = projectAssetsPath(path);
            try {
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
                return;
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
                savedProjectState = projectContentState(world, {}, camera);
                projectDirty = false;
            }
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
                        savedProjectState = projectContentState(world, {}, camera);
                        projectDirty = false;
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
            if (created < 0 || created > 11) {
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
                if (ImGui::MenuItem("Input Settings")) {
                    inputSettings.visible = true;
                }
                if (ImGui::BeginMenu("Project Settings")) {
                    if (ImGui::MenuItem("Rendering")) {
                        renderingSettings.visible = true;
                    }
                    ImGui::EndMenu();
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
            if (ImGui::Begin("Project Settings", &renderingSettings.visible)) {
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
                const auto shadowsBefore = renderingSettings.shadows;
                bool shadowChanged =
                    ImGui::Combo("Shadow mapping",
                                 &renderingSettings.shadows.mode,
                                 "Cascaded (CSM) - primary\0Single-map PCF (PCM)\0");
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
        if (showAbout) {
            ImGui::OpenPopup("About LegacyEngine");
        }
        if (ImGui::BeginPopupModal(
                "About LegacyEngine", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("LegacyEngine");
            ImGui::TextUnformatted("3D scene editor with LegacyScript (Lua 5.4)");
            ImGui::TextUnformatted("SDL3 / OpenGL / Dear ImGui / ReactPhysics3D");
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
                     fixedPanelFlags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDocking);
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
        ImGui::SameLine();
        const bool compactToolbar = layout.toolbar.width < 1000;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
                                      layout.toolbar.width - (compactToolbar ? 125.0f : 370.0f)));
        if (compactToolbar && ImGui::Button("Scene options")) {
            ImGui::OpenPopup("Scene options popup");
        }
        if (!compactToolbar || ImGui::BeginPopup("Scene options popup")) {
            if (ImGui::Checkbox("Freecam", &freeCamera)) {
                clicking = false;
            }
            if (compactToolbar) {
                ImGui::EndPopup();
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
        const MeshHandle meshOptions[]{cubeMesh, sphereMesh, capsuleMesh, coneMesh, planeMesh};
        drawInspector(world,
                      selectedEntity,
                      layout.inspector,
                      meshOptions,
                      editor.dragging(),
                      &browser.assets,
                      5);
        scripts.componentOutput = componentScripts.messages();
        scripts.setAssetContext(browser.openedScript, selectedEntity);
        scripts.draw(&layout, playMode.playing(), &browser.assets, &browser.openedScript);
        browser.projectFolder = currentProject.empty()
                                    ? browser.assets.root
                                    : std::filesystem::absolute(currentProject).parent_path();
        browser.draw(window, world, selectedEntity, scripts, editAllowed, &history);
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
                if (ImGui::BeginCombo(CubemapAsset::names[i],
                                      path.empty() ? "None (drop texture)" : path.c_str())) {
                    if (ImGui::Selectable("None", path.empty())) {
                        path.clear();
                    }
                    for (const auto& file : browser.assets.files()) {
                        if (materialTexturePath(file) &&
                            ImGui::Selectable(file.c_str(), file == path)) {
                            path = file;
                        }
                    }
                    ImGui::EndCombo();
                }
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
        terminalErrors.report("Skybox", renderer.skyboxError());
        terminalErrors.report("Material", renderer.materialError);
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
            if (beginEditorPanel("Scene")) {
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 3));
                ImGui::BeginChild(
                    "Scene toolbar", ImVec2(0, 30), ImGuiChildFlags_AlwaysUseWindowPadding);
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
                sceneSurface.resize(pixels.width, pixels.height);
                ImGui::Image(static_cast<ImTextureID>(sceneSurface.texture),
                             ImVec2(sceneRect.width, sceneRect.height),
                             ImVec2(0, 1),
                             ImVec2(1, 0));
                sceneHovered =
                    ImGui::IsWindowHovered(ImGuiHoveredFlags_None) && ImGui::IsItemHovered();
                if (ImGui::BeginDragDropTarget()) {
                    if (const auto* asset = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                        const std::string path = static_cast<const char*>(asset->Data);
                        if (Prefabs::asset(path)) {
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
                            !orientationInteracting && !cameraLooking &&
                                !(SDL_GetModState() & SDL_KMOD_ALT),
                            camera,
                            static_cast<float>(sceneSurface.width) / sceneSurface.height,
                            &sceneRect,
                            sceneDrawList);
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
        if (panelOpen[6]) {
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
                    const auto bounds = colliderWireframe(*collider, scale);
                    radius = .25f;
                    for (const auto& vertex : bounds.vertices) {
                        radius = std::max(radius, std::sqrt(dot(vertex.position, vertex.position)));
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
            deleteLevelObject(world, selectedEntity);
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

        // The time that has passed since the previous frame.
        const Uint64 ticks = SDL_GetTicksNS();
        const float seconds = static_cast<float>(ticks - lastTicks) * 1e-9f;
        lastTicks = ticks;

        if (cameraLooking &&
            (!freeCamera || !sceneVisible || browser.dialog.pending() || projectDialog.pending() ||
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
            Prefabs::updateDestroyTimers(world, std::min(seconds, .25f));
            scripts.update(std::min(seconds, 0.25f));
            componentScripts.update(std::min(seconds, 0.25f));
        }
        profiler.checkpoint(ProfilePart::Scripts);
        LuaConsole::dispatchEvents();
        scripts.console().setAssetRoot(browser.assets.root);
        renderer.setShadowSettings(renderingSettings.shadows);
        renderer.setAssetRoot(browser.assets.root);
        renderer.updateSkyboxFades(world, playMode.playing() ? std::min(seconds, .25f) : 0.0f);
        updateHierarchy(world);
        profiler.checkpoint(ProfilePart::Hierarchy);
        physics.update(world, playMode.playing() ? seconds : 0.0f, [&](float dt) {
            scripts.fixedUpdate(dt);
            componentScripts.fixedUpdate(dt);
            updateHierarchy(world);
        });
        profiler.checkpoint(ProfilePart::Physics);
        updateHierarchy(world);
        profiler.checkpoint(ProfilePart::Hierarchy);
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
                    Vec3 near = cameraScreenToWorld(t, c, {u, v, 0}),
                         far = cameraScreenToWorld(t, c, {u, v, 1});
                    RayHit hit;
                    if (physics.raycast({near, normalize(far - near)}, c.farClip, hit) &&
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
        if (width > 0 && height > 0) {
            if (sceneVisible) {
                glBindFramebuffer(GL_FRAMEBUFFER, sceneSurface.framebuffer);
                const float aspect = static_cast<float>(sceneSurface.width) / sceneSurface.height;
                const Mat4 viewProjection = camera.viewProjection(aspect);
                renderer.draw(world,
                              viewProjection,
                              sceneSurface.width,
                              sceneSurface.height,
                              camera.position());
                renderer.drawEditorGrid(browser.settings.grid, viewProjection);
                if (browser.settings.alwaysShowColliders) {
                    world.each<Transform, Collider>(
                        [&](Entity e, const Transform& t, const Collider& c) {
                            if (e != selectedEntity) {
                                renderer.drawCollider(t, c, viewProjection, .25f);
                            }
                        });
                }
                if (world.alive(selectedEntity)) {
                    const auto* transform = world.tryGet<Transform>(selectedEntity);
                    const auto* collider = world.tryGet<Collider>(selectedEntity);
                    if (transform && collider) {
                        renderer.drawCollider(*transform, *collider, viewProjection);
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
            profiler.checkpoint(ProfilePart::Scene);
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
                        false);
                    if (showGameColliders) {
                        world.each<Transform, Collider>(
                            [&](Entity, const Transform& transform, const Collider& collider) {
                                renderer.drawCollider(transform, collider, gameVP);
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
                    SDL_GL_SwapWindow(gameWindow);
                    SDL_GL_MakeCurrent(window, context);
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
        if (!prefabLevel && !playMode.active() && !editor.dragging()) {
            projectDirty = currentProject.empty() ||
                           projectContentState(world, {}, camera) != savedProjectState;
        }
        const std::string title =
            projectWindowTitle(currentProject, projectDirty, levels.current());
        if (title != lastWindowTitle) {
            SDL_SetWindowTitle(window, title.c_str());
            lastWindowTitle = title;
        }
        profiler.checkpoint(ProfilePart::Other);
        SDL_GL_SwapWindow(window);
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
