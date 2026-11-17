#include "editor/recent_projects.hpp"
#include "effects/laser_update.hpp"
#include <cctype>
// LegacyEngine - Scene editing and runtime orchestration.
//
// World conventions:
//   * 1 unit = 1 metre
//   * +Y is up, the ground is the XZ plane (y = 0)
//   * right-handed coordinates (the OpenGL convention)

#include "animation/animation_system.hpp"
#include "audio/audio_system.hpp"
#include "core/components.hpp"
#include "core/ecs.hpp"
#include "editor/animation_window.hpp"
#include "editor/asset_browser.hpp"
#include "editor/asset_preview_renderer.hpp"
#include "editor/create_menu.hpp"
#include "editor/editor_geometry.hpp"
#include "editor/editor_input.hpp"
#include "editor/editor_ui.hpp"
#include "editor/editor_history.hpp"
#include "editor/fix_project.hpp"
#include "editor/input_settings.hpp"
#include "editor/level_objects.hpp"
#include "editor/lightmapping_window.hpp"
#include "editor/model_preview_window.hpp"
#include "editor/play_mode.hpp"
#include "editor/script_editor.hpp"
#include "editor/transform_editor.hpp"
#include "gameplay/first_person.hpp"
#include "gameplay/raycast.hpp"
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

#include "ui/qt/qt_application.hpp"
#include "ui/qt/qt_input_bridge.hpp"
#include "ui/qt/qt_editor_window.hpp"
#include "ui/qt/qt_framebuffer.hpp"
#include "ui/qt/qt_panels.hpp"
#include "ui/qt/qt_viewport.hpp"

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

// The ray from the editor camera through a point of the Scene picture. The Qt
// widget reports the point in picture coordinates (0..1 from the top left), so
// no window-size bookkeeping is needed here.
static Ray sceneRay(const OrbitCamera& camera, float u, float v, float aspect) {
    return camera.rayThrough(u, v, aspect);
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

#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
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
    scripts.bindConsoleMeshes(world, cubeMesh, sphereMesh, capsuleMesh, coneMesh, planeMesh);
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
    ModelPreviewWindow modelPreviewWindow;
    SceneSurface sceneSurface, gameSurface, cameraPreviewSurface;
    auto lastSceneRender = std::chrono::steady_clock::time_point{};
    int lastSceneWidth = 0, lastSceneHeight = 0;
    AssetPreviewRenderer assetPreviews(
        renderer, {gridMesh, planeMesh, cubeMesh, sphereMesh, capsuleMesh, coneMesh});
    EditorRect sceneRect{};
    bool sceneHovered = false;
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
    ProjectDialog projectDialog;
    std::string currentProject, projectError, savedProjectState, lastWindowTitle, backupStatus;
    bool projectDirty = true;
    InputState input;
    input.window = window;
    InputSettings inputSettings;
    EngineLog::Errors terminalErrors;
    bool gameHovered = false, gameFocused = false;
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
    float gameU = 0.5f, gameV = 0.5f;
    bool showFPS = false, showGameColliders = false, showProfiler = false;
    // The Qt shell. Widgets only record what the user asked for; the engine
    // consumes those actions at a fixed point in every frame.
    EditorUI shell(window);
    if (!shell.ready()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "The Qt editor shell is not available");
        return false;
    }
    // Attach the shell to the engine's state once, before the first frame. The
    // panels read and write these objects directly; nothing is copied.
    shell.shell()->bind(world,
                        browser,
                        history,
                        renderer,
                        scripts,
                        animationWindow,
                        lightmappingWindow,
                        playMode,
                        terrainEditor,
                        browser.settings,
                        inputSettings,
                        input,
                        renderingSettings,
                        editor,
                        assetPreviews,
                        modelPreviewWindow,
                        projectMeshes,
                        camera);
    FrameProfiler profiler;
    activeProfiler = &profiler;
    Uint64 lastTicks = SDL_GetTicksNS();
    bool running = true, exitRequested = false, exitAfterSave = false;
    auto lastAutosave = std::chrono::steady_clock::now();


    // The left mouse button does two things. Pressed and released in one place,
    // it is a click, which selects. Moving away cancels the click; camera
    // navigation uses the right button and the keyboard.
    bool clicking = false;
    float pressU = 0.0f, pressV = 0.0f;

    bool cameraLooking = false;
    unsigned renderedFrames = 0;
    constexpr unsigned smokeFrames = 120;
    std::optional<Vec3> lastOrbitPivot;

    static RecentProjects recentProjects;
    // Viewport/focus facts the engine reads throughout the frame.
    bool sceneVisible = true, gameVisible = false;
    bool sceneFocused = false;
    EditorRect gameRect{};
    std::string selectedRecent;
    bool confirmOpen = false, confirmExit = false;
    int continueAfterSave = 0;
    std::string pendingRecent;
    // One reusable snapshot of what the shell shows; refilled every frame.
    EditorView view;
    EditorUIState uiState;
    EditorUI::ActionSink actionSink;
    auto editGuard = [&]() {
        return !editor.dragging() && !projectDialog.pending() && !browser.dialog.pending();
    };
    // ---- Actions: everything a widget can ask the engine to do -----------
    // Each body is the old menu or panel handler. They run from inside the
    // frame, after Qt's events were drained, so undo and physics see a stable
    // world.
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
                // Old Trash receipts belong to the previous project root.
                history = EditorHistory(world, selectedEntity);
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
    auto loadProject = [&](const std::string& path) {
        std::string source, assetDirectory;
        World loadedWorld;
        OrbitCamera loadedCamera;
        if (ProjectIO::load(path,
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
            browser.setRoot(assetDirectory.empty()
                                ? existingProjectAssetsPath(path)
                                : std::filesystem::absolute(path).parent_path() / assetDirectory);
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
            currentProject = path;
            recentProjects.remember(currentProject);
            savedProjectState = projectContentState(world, embeddedLegacySource, camera);
            projectDirty = false;
            lastAutosave = std::chrono::steady_clock::now();
            uiState.levelName = std::filesystem::path(currentProject).stem().string();
        }
    };
    auto saveCurrentLevel = [&]() {
        try {
            if (!animationWindow.saveIfDirty(browser.assets)) {
                throw std::runtime_error(animationWindow.error());
            }
            scripts.saveFile(browser.assets);
            levels.saveCurrent(world, embeddedLegacySource, camera);
        } catch (const std::exception& e) {
            levels.error = e.what();
        }
    };
    auto startPlay = [&]() {
        if (prefabLevel || playMode.active() || levels.busy()) {
            return;
        }
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
            Particles::reset(world);
            lightmappingWindow.cancelBake(); // do not compete with gameplay for CPU time
            shell.shell()->focusGameView();
            if (separateGame && !gameWindow) {
                gameWindow =
                    SDL_CreateWindow("Game", 960, 540, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
                if (!gameWindow) {
                    projectError = SDL_GetError();
                }
            }
            const MeshHandle runtimeMeshes[]{cubeMesh, sphereMesh, capsuleMesh, coneMesh};
            componentScripts.start(world, browser.assets, runtimeMeshes, planeMesh);
            scripts.console().setAssetRoot(browser.assets.root);
            scripts.startPlay();
            audio.start(world);
        }
    };
    auto pausePlay = [&]() {
        if (!playMode.active()) {
            return;
        }
        editor.finish(world);
        clicking = false;
        playMode.pause();
        if (!playMode.playing()) {
            input.releaseMouse();
        }
    };
    auto stopPlay = [&]() {
        if (!playMode.active()) {
            return;
        }
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
    };
    auto deleteSelection = [&]() {
        if (!editGuard() || !world.alive(selectedEntity)) {
            return;
        }
        editor.finish(world);
        clicking = false;
        history.commit(world, selectedEntity);
        deleteHierarchySelection(world, selectedEntity);
        selectedEntity = {};
        history.commit(world, selectedEntity);
        physics.reset();
    };
    auto focusSelection = [&]() {
        const auto* transform = world.tryGet<Transform>(selectedEntity);
        if (!transform || !editGuard()) {
            return;
        }
        editor.finish(world);
        clicking = false;
        const auto& scale = transform->scale;
        float radius = std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z), 1.0f});
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
                    radius = std::max(radius, std::sqrt(dot(vertex.position, vertex.position)));
                }
            }
        }
        const int w = std::max(1, view.sceneSurface.width());
        const int h = std::max(1, view.sceneSurface.height());
        camera.focus(centre, radius, float(w) / h);
    };
    auto undoEdit = [&]() {
        if (playMode.active() || !editGuard()) {
            return;
        }
        editor.finish(world);
        clicking = false;
        history.commit(world, selectedEntity);
        if (history.undo(world, selectedEntity)) {
            scripts.console().message("Undo editor change");
            physics.reset();
        } else if (!history.error.empty()) {
            projectError = history.error;
            history.error.clear();
        }
    };
    auto redoEdit = [&]() {
        if (playMode.active() || !editGuard()) {
            return;
        }
        editor.finish(world);
        clicking = false;
        history.commit(world, selectedEntity);
        if (history.redo(world, selectedEntity)) {
            scripts.console().message("Redo editor change");
            physics.reset();
        } else if (!history.error.empty()) {
            projectError = history.error;
            history.error.clear();
        }
    };
    auto duplicateSelection = [&]() {
        if (!editGuard() || !world.alive(selectedEntity)) {
            return;
        }
        editor.finish(world);
        clicking = false;
        history.commit(world, selectedEntity);
        selectedEntity = duplicateLevelObject(world, selectedEntity);
        history.commit(world, selectedEntity);
    };
    auto copySelection = [&]() {
        if (editGuard() && world.alive(selectedEntity)) {
            clipboard.copy(world, selectedEntity);
        }
    };
    auto pasteSelection = [&]() {
        if (!editGuard() || !clipboard.available()) {
            return;
        }
        editor.finish(world);
        clicking = false;
        history.commit(world, selectedEntity);
        selectedEntity = clipboard.paste(world);
        history.commit(world, selectedEntity);
    };
    auto toggleParent = [&]() {
        if (!editGuard() || !world.alive(selectedEntity)) {
            return;
        }
        editor.finish(world);
        clicking = false;
        if (getParent(world, selectedEntity) != Entity{}) {
            history.commit(world, selectedEntity);
            setParent(world, selectedEntity, Entity{});
            history.commit(world, selectedEntity);
        } else {
            shell.shell()->showParentChooser(world);
        }
    };
    auto spawnModelAsset = [&](const std::string& path) {
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
    };
    auto spawnPrefabAsset = [&](const std::string& path) {
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
    };
    auto enterPrefab = [&](const std::string& path) {
        if (playMode.active() || prefabLevel || levels.busy()) {
            return;
        }
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
            shell.shell()->focusScene();
        } catch (const std::exception& e) {
            browser.assets.error = e.what();
        }
    };
    auto savePrefab = [&]() {
        if (!prefabLevel) {
            return;
        }
        try {
            auto root = Prefabs::root(world);
            Prefabs::save(browser.assets, editingPrefab, world, root, projectMeshes);
            Prefabs::refresh(browser.assets, editingPrefab, *prefabLevel, projectMeshes);
            Prefabs::refreshSavedLevels(browser.assets, editingPrefab, projectMeshes);
            scripts.console().message("Saved prefab and refreshed level instances");
        } catch (const std::exception& e) {
            browser.assets.error = e.what();
        }
    };
    auto exitPrefab = [&]() {
        if (!prefabLevel) {
            return;
        }
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
        shell.shell()->clearPrefabMode();
    };

    while (running) {

        const Uint64 frameStarted = SDL_GetTicksNS();
        const float seconds = std::clamp(static_cast<float>(frameStarted - lastTicks) * 1e-9f,
                                         1.0f / 1000.0f,
                                         0.25f);
        lastTicks = frameStarted;
        if (!playMode.active()) {
            if (const auto* pose = world.tryGet<Transform>(selectedEntity)) {
                lastOrbitPivot = pose->position;
            }
        }
        profiler.begin(showProfiler);
        int windowWidth = 1, windowHeight = 1;
        SDL_GetWindowSize(window, &windowWidth, &windowHeight);
        scripts.console().setAssetRoot(browser.assets.root);
        inputSettings.load(browser.assets, input);
        renderingSettings.load(browser.assets);
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
        // ---- Qt <-> engine boundary -------------------------------------
        // Take what the widgets recorded, report the viewport, then run every
        // requested action through the sink above.
        const bool externalFocus =
            gameWindow && (SDL_GetWindowFlags(gameWindow) & SDL_WINDOW_INPUT_FOCUS);
        // Viewport visibility: the dock is shown and the picture has a size.
        sceneVisible = !view.sceneSurface.isEmpty();
        view.sceneSurface = shell.shell()->scenePixelSize();
        view.gameSurface = shell.shell()->gamePixelSize();
        view.scenePointer = shell.shell()->scenePointer();
        view.gamePointer = shell.shell()->gamePointer();
        view.sceneFocused = shell.shell()->scenePanelFocused();
        sceneFocused = view.sceneFocused;
        sceneHovered = view.scenePointer.hovered;
        gameHovered = view.gamePointer.hovered;
        gameFocused = gameWindow ? externalFocus : gameHovered;
        gameRect = {0, 0, float(std::max(1, view.gameSurface.width())),
                    float(std::max(1, view.gameSurface.height()))};
        gameU = view.gamePointer.u;
        gameV = view.gamePointer.v;
        const bool editAllowed = editGuard();
        const bool objectSelected = world.alive(selectedEntity);
        shell.shell()->setObjectActionsEnabled(objectSelected);
        const bool projectEnabled = !browser.importing() && !prefabLevel && !levels.busy() &&
                                    !playMode.active() && !editor.dragging() &&
                                    !projectDialog.pending() && !browser.dialog.pending();

        // Fill in the state snapshot for the shell before its events are used.
        uiState.playActive = playMode.active();
        uiState.playing = playMode.playing();
        uiState.paused = playMode.state() == PlayMode::State::Paused;
        uiState.editAllowed = editAllowed && !playMode.active();
        uiState.separateGame = separateGame;
        uiState.gameWindowActive = gameWindow != nullptr;
        // Debug overlays are drawn by the engine, so the shell's toggles are
        // read back here rather than pushed from it on every change.
        showFPS = shell.shell()->fpsOverlayVisible();
        showGameColliders = shell.shell()->collidersVisible();
        showProfiler = shell.shell()->profilerVisible();
        uiState.showFps = showFPS;
        uiState.showColliders = showGameColliders;
        uiState.showProfiler = showProfiler;
        uiState.levelsEnabled = !playMode.active() && !levels.busy() && !projectDialog.pending();
        uiState.prefabMode = prefabLevel != nullptr;
        uiState.prefabPath = editingPrefab;
        uiState.terrainSelected = world.alive(selectedEntity) && world.has<Terrain>(selectedEntity);
        uiState.selection = selectedEntity;
        uiState.fps = 1000.0f / std::max(1.0f, float(profiler.stats(int(ProfilePart::Present)).average));
        uiState.dt = seconds;
        uiState.frameMs = profiler.stats(int(ProfilePart::Present)).average;
        uiState.levelName = prefabLevel ? "Prefab: " + editingPrefab
                             : !levels.current().empty() ? levels.current()
                             : currentProject.empty()
                                 ? "Untitled"
                                 : std::filesystem::path(currentProject).stem().string();
        uiState.projectName = currentProject.empty() ? std::string{} : currentProject;
        uiState.currentLevel = levels.current();
        uiState.prefabPath = editingPrefab;
        uiState.levelNames = std::vector<std::string>(levels.names().begin(), levels.names().end());
        uiState.recentProjects = recentProjects.paths();
        uiState.profiler = &profiler;
        uiState.status = projectError.empty() ? std::string("Ready") : projectError;
        uiState.detail = levels.current().empty() ? std::string{} : "Level " + levels.current();
        shell.beginFrame(view, uiState);

        // The engine renders at the size of the Qt picture, so the result maps
        // one-to-one onto the widget and picking uses the same geometry.
        sceneSurface.resize(view.sceneSurface.width(), view.sceneSurface.height());
        cameraPreviewSurface.resize(320, 180);
        if (!gameVisible) {
            gameSurface.resize(view.gameSurface.width(), view.gameSurface.height());
        }

        const auto& pointer = view.scenePointer;
        const int surfaceWidth = std::max(1, view.sceneSurface.width());
        const int surfaceHeight = std::max(1, view.sceneSurface.height());
        const float sceneAspect = float(surfaceWidth) / surfaceHeight;
        sceneRect = {0, 0, float(view.sceneSurface.width()), float(view.sceneSurface.height())};

        // Camera navigation shortcuts the old Scene window handled with keys.
        if (editAllowed && view.sceneFocused && !cameraLooking && pointer.hovered &&
            !cameraModifiersHeld(SDL_GetModState())) {
            const bool* keys = SDL_GetKeyboardState(nullptr);
            if (keys[SDL_SCANCODE_W]) {
                editor.setTool(TransformTool::Move);
            }
            if (keys[SDL_SCANCODE_E]) {
                editor.setTool(TransformTool::Rotate);
            }
            if (keys[SDL_SCANCODE_R]) {
                editor.setTool(TransformTool::Scale);
            }
        }

        // Scene picking: a click that did not move is a selection, exactly as
        // before; the ray now comes from the Qt picture coordinates.
        if (pointer.pressed && pointer.button == int(Qt::LeftButton) &&
            !cameraModifiersHeld(SDL_GetModState()) && !terrainEditor.active(world, selectedEntity)) {
            clicking = true;
            pressU = pointer.u;
            pressV = pointer.v;
        }
        if (clicking && pointer.hovered &&
            std::hypot(pointer.u - pressU, pointer.v - pressV) >
                kClickRadius / float(std::max(1, view.sceneSurface.width()))) {
            clicking = false; // too far for a click: a drag from here on
        }
        if (pointer.pressed && pointer.button == int(Qt::RightButton) && pointer.hovered &&
            editAllowed && !cameraModifiersHeld(SDL_GetModState())) {
            cameraLooking = input.lockSceneCursor();
            clicking = false;
        }
        if (pointer.pressed && pointer.button == int(Qt::MiddleButton)) {
            clicking = false;
        }
        if (pointer.released && clicking) {
            clicking = false;
            // Scene clicks only select; object creation belongs to Create menus.
            const Ray ray = sceneRay(camera, pointer.u, pointer.v, sceneAspect);
            RayHit hit;
            const bool found = physics.raycast(ray, kClickReach, hit);
            selectedEntity = found ? hit.entity : Entity{};
            float closest = found ? hit.distance : kClickReach;
            world.each<Camera, Transform>([&](Entity e, Camera&, Transform& t) {
                const auto offset = ray.origin - t.position;
                const float radius =
                    1.1f * std::max({std::abs(t.scale.x), std::abs(t.scale.y), std::abs(t.scale.z)});
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
            browser.selected.clear();
        }
        // Alt+left drag orbits around the selection; the Qt view keeps its own
        // camera motion, but the engine must not treat the drag as a click.
        if (pointer.down && pointer.alt && !editor.dragging() && pointer.hovered) {
            clicking = false;
        }
        if (pointer.wheel != 0.0f && !cameraModifiersHeld(SDL_GetModState()) &&
            pointer.hovered) {
            camera.zoom(pointer.wheel * browser.settings.scrollZoomSpeed);
        }

        // Terrain sculpting: the Qt ray and a held button drive the brush, the
        // editor owns the undo gesture and the collision refresh.
        {
            const bool brushHover = editAllowed && sceneHovered && !cameraLooking &&
                                    !(SDL_GetModState() & SDL_KMOD_ALT);
            const bool brushDown = brushHover && pointer.down;
            Ray terrainRay;
            const bool rayValid = brushHover;
            if (rayValid) {
                terrainRay = sceneRay(camera, pointer.u, pointer.v, sceneAspect);
            }
            if (brushHover && terrainEditor.active(world, selectedEntity)) {
                // The overlay is painted by the Qt Scene view from brushRing().
                (void)terrainEditor.brushRing(world,
                                              selectedEntity,
                                              rayValid ? &terrainRay : nullptr,
                                              camera.viewProjection(sceneAspect),
                                              sceneRect);
            }
            terrainEditor.stroke(world,
                                 selectedEntity,
                                 history,
                                 rayValid ? &terrainRay : nullptr,
                                 brushDown,
                                 seconds);
        }

        // Focus / delete shortcuts the Qt panels do not own.
        if (editAllowed && (shell.shell()->scenePanelFocused() || shell.shell()->hierarchyPanelFocused()) &&
            !cameraLooking && !cameraModifiersHeld(SDL_GetModState())) {
            const bool* keys = SDL_GetKeyboardState(nullptr);
            if (keys[SDL_SCANCODE_F]) {
                focusSelection();
            }
            if (keys[SDL_SCANCODE_DELETE]) {
                deleteSelection();
            }
            if (keys[SDL_SCANCODE_P] && (SDL_GetModState() & SDL_KMOD_CTRL)) {
                toggleParent();
            }
        }
        {
            Entity parent{};
            if (shell.shell()->takeParentRequest(parent) && world.alive(selectedEntity) &&
                parent != Entity{} && canParent(world, selectedEntity, parent)) {
                editor.finish(world);
                clicking = false;
                history.commit(world, selectedEntity);
                setParent(world, selectedEntity, parent);
                history.commit(world, selectedEntity);
            }
        }

        // Now hand the widget actions to the engine.
        actionSink.createObject = createObject;
        actionSink.undo = undoEdit;
        actionSink.redo = redoEdit;
        actionSink.duplicate = duplicateSelection;
        actionSink.copy = copySelection;
        actionSink.paste = pasteSelection;
        actionSink.parentToggle = toggleParent;
        actionSink.play = startPlay;
        actionSink.pause = pausePlay;
        actionSink.stop = stopPlay;
        actionSink.separateGameWindow = [&]() {
            if (!playMode.active()) {
                separateGame = !separateGame;
                if (!separateGame && gameWindow) {
                    input.releaseMouse();
                    input.window = window;
                    SDL_DestroyWindow(gameWindow);
                    gameWindow = nullptr;
                }
            }
        };
        actionSink.toggleGameFullscreen = [&]() { toggleGameFullscreen(); };
        actionSink.openProject = [&]() {
            projectDirty = currentProject.empty() || scripts.hasUnsavedChanges() ||
                           animationWindow.hasUnsavedChanges() ||
                           projectContentState(world, embeddedLegacySource, camera) !=
                               savedProjectState;
            if (projectDirty) {
                confirmOpen = true;
                pendingRecent.clear();
            } else {
                projectDialog.show(window, false, currentProject);
            }
        };
        actionSink.saveProject = [&]() {
            if (!projectEnabled) {
                return;
            }
            if (!currentProject.empty()) {
                saveTo(currentProject);
            } else {
                projectDialog.show(window, true, currentProject);
            }
        };
        actionSink.saveProjectAs = [&]() {
            if (projectEnabled) {
                projectDialog.show(window, true, currentProject);
            }
        };
        actionSink.openRecent = [&](const std::string& path) {
            projectDirty = currentProject.empty() || scripts.hasUnsavedChanges() ||
                           animationWindow.hasUnsavedChanges() ||
                           projectContentState(world, embeddedLegacySource, camera) !=
                               savedProjectState;
            if (projectDirty) {
                confirmOpen = true;
                pendingRecent = path;
            } else {
                projectDialog.openRecent(path);
            }
        };
        actionSink.saveLevel = [&]() {
            if (projectEnabled) {
                saveCurrentLevel();
            }
        };
        actionSink.newLevel = [&]() {
            if (!projectEnabled) {
                return;
            }
            // The name comes from Qt, so the engine only needs the operation.
            if (auto* shellWindow = shell.shell()) {
                const QString name = shellWindow->askText(
                    QStringLiteral("New Level"), QStringLiteral("Level name"), QStringLiteral("New Level"));
                if (name.isEmpty()) {
                    return;
                }
                try {
                    scripts.saveFile(browser.assets);
                    levels.saveCurrent(world, embeddedLegacySource, camera);
                    levels.create(name.toStdString());
                    levels.load(name.toStdString());
                } catch (const std::exception& e) {
                    levels.error = e.what();
                }
            }
        };
        actionSink.renameLevel = [&]() {
            if (!projectEnabled || !shell.shell()) {
                return;
            }
            const QString current = QString::fromStdString(levels.current());
            const QString name = shell.shell()->askText(QStringLiteral("Rename Level"),
                                                        QStringLiteral("Level name"),
                                                        current.isEmpty()
                                                            ? QStringLiteral("Main")
                                                            : current);
            if (name.isEmpty() || name == current) {
                return;
            }
            try {
                scripts.saveFile(browser.assets);
                levels.saveCurrent(world, embeddedLegacySource, camera);
                levels.renameCurrent(name.toStdString());
            } catch (const std::exception& e) {
                levels.error = e.what();
            }
        };
        actionSink.openLevel = [&](const std::string& name) {
            try {
                scripts.saveFile(browser.assets);
                levels.saveCurrent(world, embeddedLegacySource, camera);
                levels.load(name);
            } catch (const std::exception& e) {
                levels.error = e.what();
            }
        };
        actionSink.exit = [&]() { exitRequested = true; };
        actionSink.theme = [&](int theme) {
            browser.settings.theme = theme;
            browser.settings.save();
            shell.shell()->applyTheme(theme);
        };
        actionSink.backup = [&]() {
            try {
                if (saveTo(currentProject)) {
                    backupStatus = "Backup created: " + backupProject(currentProject).u8string();
                }
            } catch (const std::exception& e) {
                projectError = e.what();
            }
        };
        actionSink.fixProject = [&]() {
            if (projectEnabled && !currentProject.empty()) {
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
        };
        actionSink.openInVSCode = [&]() {
            const auto folder = currentProject.empty()
                                    ? browser.assets.root
                                    : std::filesystem::absolute(currentProject).parent_path();
            if (!browser.settings.openExternal(folder)) {
                projectError = browser.settings.error;
            }
        };
        actionSink.openSettingsFolder = [&]() {
            auto folder = browser.settings.file().parent_path();
            if (!browser.settings.openExternal(folder)) {
                projectError = browser.settings.error;
            }
        };
        actionSink.importFiles = [&]() {
            if (!playMode.active() && !projectDialog.pending() && !levels.busy()) {
                projectDialog.showImport(window);
            }
        };
        actionSink.saveAnimation = [&]() {
            if (!animationWindow.saveIfDirty(browser.assets)) {
                projectError = animationWindow.error();
            }
        };
        actionSink.bakeLighting = [&]() {
            lightmappingWindow.bake(world,
                                    browser.assets,
                                    projectMeshes,
                                    levels.current());
        };
        actionSink.prefabSave = savePrefab;
        actionSink.prefabExit = exitPrefab;
        actionSink.spawnModel = spawnModelAsset;
        actionSink.spawnPrefab = spawnPrefabAsset;
        actionSink.editPrefab = enterPrefab;
        actionSink.editCubemap = [&](const std::string& path) { editingCubemap = path; };
        actionSink.editAnimation = [&](const std::string& path) {
            animationWindow.open(browser.assets, path);
        };
        actionSink.previewModel = [&](const std::string& path) {
            modelPreviewWindow.open(browser.assets, path, renderer);
        };
        actionSink.openScript = [&](const std::string& path) {
            browser.openedScript = path;
            scripts.setAssetContext(path, selectedEntity);
        };
        shell.handleActions(actionSink);
        if (shell.shell()->takeExitConfirmed()) {
            running = false;
        }
#include <QMessageBox>
        // Unsaved-changes confirmation. The old modal popup is a Qt question
        // box now; the answer drives exactly the same three outcomes.
        if (confirmOpen || confirmExit) {
            const bool closing = confirmExit;
            const QString question =
                closing ? QStringLiteral("Save your work before closing LegacyEngine?")
                        : QStringLiteral(
                              "Save your work before opening another project? Discarded "
                              "changes cannot be recovered.");
            QMessageBox box(shell.shell());
            box.setWindowTitle(QStringLiteral("Unsaved project changes"));
            box.setIcon(QMessageBox::Warning);
            box.setText(QStringLiteral("You have unsaved changes"));
            box.setInformativeText(question);
            QPushButton* save =
                box.addButton(QStringLiteral("Save and continue"), QMessageBox::AcceptRole);
            QAbstractButton* discard =
                box.addButton(QStringLiteral("Discard"), QMessageBox::DestructiveRole);
            QAbstractButton* cancel = box.addButton(QMessageBox::Cancel);
            box.setDefaultButton(save);
            box.exec();
            auto proceed = [&]() {
                if (closing) {
                    running = false;
                } else if (!pendingRecent.empty()) {
                    projectDialog.openRecent(pendingRecent);
                } else {
                    projectDialog.show(window, false, currentProject);
                }
                confirmOpen = confirmExit = false;
                pendingRecent.clear();
            };
            if (box.clickedButton() == save) {
                if (!currentProject.empty()) {
                    if (saveTo(currentProject)) {
                        proceed();
                    }
                } else {
                    continueAfterSave = closing ? 2 : 1;
                    confirmOpen = confirmExit = false;
                    projectDialog.show(window, true, currentProject);
                }
            } else if (box.clickedButton() == discard) {
                proceed();
            } else if (box.clickedButton() == cancel) {
                confirmOpen = confirmExit = false;
                pendingRecent.clear();
            }
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
                    loadProject(result->path);
                }
            }
        }
        if (!projectError.empty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Project: %s", projectError.c_str());
            shell.showError("Project error", projectError);
            projectError.clear();
        }
        // Model and cubemap dialogs the engine opened this frame follow the
        // assets they belong to; a saved cubemap carries the new face paths
        // back into the material that uses it.
        if (shell.shell() && shell.shell()->cubemapSaved()) {
            const QString path = shell.shell()->takeCubemapSaved();
            if (!path.isEmpty()) {
                shell.consoleMessage(
                    QStringLiteral("Saved cubemap %1").arg(QFileInfo(path).fileName()).toStdString());
            }
        }
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
                        (input.mouseLocked || !shell.shell()->gamePointerLocked()) && !inputSettings.visible;
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
                input.releaseMouse();
                SDL_SetWindowRelativeMouseMode(window, false);
                SDL_SetWindowMouseGrab(window, false);
                SDL_SetWindowMouseRect(window, nullptr);
            }
            input.event(event);
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
            case SDL_EVENT_MOUSE_MOTION:
                // Camera look while the pointer is grabbed: the Qt widget cannot
                // see these deltas because SDL owns the relative mouse mode.
                if (event.motion.state & SDL_BUTTON_RMASK) {
                    if (cameraLooking && !cameraModifiersHeld(SDL_GetModState())) {
                        const auto motion = input.lockedMotion(event.motion);
                        camera.look(motion.first, motion.second);
                    }
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL: {
                if (cameraLooking) {
                    if (SDL_GetModState() & SDL_KMOD_SHIFT) {
                        cameraBoost = adjustSceneCameraBoost(cameraBoost, event.wheel.y);
                    } else {
                        cameraSpeed = std::clamp(
                            cameraSpeed * std::pow(1.2f, event.wheel.y), .1f, 500.0f);
                    }
                }
                break;
            }
            default:
                break;
            }
        }


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
        profiler.checkpoint(ProfilePart::Hierarchy);
        physics.setAssetRoot(browser.assets.root);
        Terrains::update(world, browser.assets);
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
            bool inside = gameHovered;
            float u = inside ? gameU : 0;
            float v = inside ? gameV : 0;
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
        bool sceneDrawn = false;
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
            sceneDrawn = renderScene;
            if (renderScene) {
                lastSceneRender = renderTime;
                lastSceneWidth = sceneSurface.width;
                lastSceneHeight = sceneSurface.height;
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
                                renderer.drawCollider(
                                    t, effectiveCollider(world, e, c), viewProjection, .25f);
                            }
                        });
                }
                if (world.alive(selectedEntity)) {
                    const auto* transform = world.tryGet<Transform>(selectedEntity);
                    const auto* collider = world.tryGet<Collider>(selectedEntity);
                    if (transform && collider) {
                        renderer.drawCollider(*transform,
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
                    // No overlay is drawn into the separate Game window; the Qt
                    // status bar keeps showing the frame time beside it.
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
            // The Qt shell paints the finished pictures; nothing to draw here.
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
                confirmExit = true;
            }
        }
        if (cameraLooking || input.mouseLocked) {
            SDL_Rect rect{};
            if (input.lockedWindow == gameWindow && gameWindow) {
                SDL_GetWindowSize(gameWindow, &rect.w, &rect.h);
            } else {
                // Caret is grabbed over the Scene picture (or the Game view).
                rect = cameraLooking ? SDL_Rect{int(sceneRect.x),
                                                int(sceneRect.y),
                                                int(sceneRect.width),
                                                int(sceneRect.height)}
                                     : SDL_Rect{int(gameRect.x),
                                                int(gameRect.y),
                                                int(gameRect.width),
                                                int(gameRect.height)};
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
        profiler.checkpoint(ProfilePart::UI);
        // Hand the freshly drawn pictures to the widgets. Reading happens here,
        // inside the frame, while the SDL context is current.
        if (sceneDrawn) {
            view.sceneImage =
                ui::qt::readFramebuffer(sceneSurface.framebuffer,
                                        sceneSurface.width,
                                        sceneSurface.height);
        }
        if (gameVisible) {
            view.gameImage =
                ui::qt::readFramebuffer(gameSurface.framebuffer,
                                        gameSurface.width,
                                        gameSurface.height);
        }
        // The inspector shows what the selected camera sees, as it did when the
        // preview was drawn into the ImGui panel.
        if (world.has<Camera>(selectedEntity) && world.has<Transform>(selectedEntity)) {
            shell.shell()->inspectorPanel()->setCameraPreview(
                ui::qt::readFramebuffer(cameraPreviewSurface.framebuffer,
                                        cameraPreviewSurface.width,
                                        cameraPreviewSurface.height));
        }
        shell.presentFrame(view, uiState);
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
        if (!ui::qt::Application::create(window)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not initialise the Qt editor shell");
            renderer.shutdown();
            SDL_GL_DestroyContext(context);
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        ui::qt::installInputBridge();
        SDL_Log("Editor started with Qt");
        runSucceeded = run(window, renderer, smokeTest);
        ui::qt::removeInputBridge();
        ui::qt::Application::destroy();
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
