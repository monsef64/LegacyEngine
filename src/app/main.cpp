// LegacyEngine - a small 3D editor with an ordinary 1 m cube above a
// 16 x 16 m plane, on a 32 x 32 m grid.
// A click in spawn mode puts a new shape under the mouse pointer.
//
// World conventions:
//   * 1 unit = 1 metre
//   * +Y is up, the ground is the XZ plane (y = 0)
//   * right-handed coordinates (the OpenGL convention)

#include "scene/camera.hpp"
#include "core/components.hpp"
#include "core/ecs.hpp"
#include "rendering/mesh.hpp"
#include "physics/physics.hpp"
#include "rendering/renderer.hpp"
#include "editor/transform_editor.hpp"
#include "editor/script_editor.hpp"
#include "editor/level_objects.hpp"
#include "editor/create_menu.hpp"
#include "editor/editor_layout.hpp"
#include "editor/inspector.hpp"
#include "editor/play_mode.hpp"
#include "project/project_io.hpp"
#include "platform/project_dialog.hpp"
#include "project/project_document.hpp"
#include "editor/asset_browser.hpp"
#include "audio/audio_system.hpp"
#include "scripting/script_components.hpp"
#include "editor/dock_workspace.hpp"
#include "rendering/scene_surface.hpp"
#include "scene/scene_camera.hpp"
#include "editor/editor_input.hpp"
#include "editor/editor_history.hpp"
#include "editor/playback_controls.hpp"
#include "editor/editor_theme.hpp"
#include "editor/input_settings.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iterator>

// ---------------------------------------------------------------------------
// Scene description
// ---------------------------------------------------------------------------

constexpr int   kGridCells = 32;    // grid blocks per side
constexpr float kCellSize  = 1.0f; // metres per grid block
constexpr float kPlaneSize = 16.0f; // metres per side of the plane
constexpr float kCubeSize  = 1.0f; // metres per edge of a cube

// Where the centre of the first cube is before it falls.
constexpr Vec3 kCubeStart = {0.0f, 1.0f, 0.0f};

// The cubes that clicks create take these colours in turn.
constexpr Vec3 kClickColors[] = {
    {0.20f, 0.45f, 0.90f}, // blue
    {0.25f, 0.70f, 0.35f}, // green
    {0.95f, 0.80f, 0.25f}, // yellow
    {0.85f, 0.25f, 0.25f}, // red
    {0.60f, 0.40f, 0.90f}, // purple
    {0.20f, 0.75f, 0.75f}, // turquoise
};

// A press of the left mouse button is a click as long as the pointer stays
// within this distance of where it went down. Further away it is a drag.
constexpr float kClickRadius = 5.0f;

// How far from the camera a click reaches, in metres.
constexpr float kClickReach = 100.0f;

// Where there is no room for a new cube, room is looked for beside the spot
// and above it: this many metres further away at each try, for this many
// tries.
constexpr float kRoomStep  = 0.02f;
constexpr int   kRoomSteps = 250;

// ---------------------------------------------------------------------------
// The cubes that clicks create
// ---------------------------------------------------------------------------

// The ray from the camera through a mouse position. Returns false while the
// window has no size (it is minimised, say).
static bool mouseRay(SDL_Window* window, const OrbitCamera& camera, float x, float y, Ray& ray,const EditorRect& scene)
{
    // Mouse positions are counted in the units of the window's size, which are
    // not pixels on a high-density display. The shape of the picture comes
    // from its size in pixels, as it does for drawing.
    int windowWidth  = 0;
    int windowHeight = 0;
    int width        = 0;
    int height       = 0;
    SDL_GetWindowSize(window, &windowWidth, &windowHeight);
    SDL_GetWindowSizeInPixels(window, &width, &height);
    if (windowWidth <= 0 || windowHeight <= 0 || width <= 0 || height <= 0) {
        return false;
    }
    if(!scene.contains(x,y) || scene.width<=0 || scene.height<=0)return false;
    const auto pixels=scenePixels(scene,windowWidth,windowHeight,width,height);
    ray = camera.rayThrough((x*width/windowWidth-pixels.x)/pixels.width,
                           (y*height/windowHeight-pixels.top)/pixels.height,pixels.aspect());
    return true;
}

// The place nearest to `center` where a cube has room: `center` itself if
// nothing is in the way there. If something is, the nearest place where
// nothing is: beside `center`, no further away than a cube is wide, or above
// it.
static Vec3 findRoom(const Physics& physics, Vec3 center, ColliderShape shape = ColliderShape::Box)
{
    // Things that rest on each other touch, and reach into each other by up to
    // a centimetre. Neither is in the way, so room is looked for with a cube
    // that is that much smaller on every side.
    const float inner = kCubeSize - 2.0f * 0.01f;
    const Vec3  size  = {inner, shape == ColliderShape::Capsule ? inner + 1.0f : inner, inner};
    if (!physics.checkBox(center, size)) {
        return center;
    }

    static const Vec3 kWays[] = {
        {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f},
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

// Where the centre of a new cube goes, given the ray from the camera through
// the mouse pointer: the cube rests against the spot under the pointer if it
// has room there, and as near to it as it has room if not.
static Vec3 placeForCube(const Physics& physics, const OrbitCamera& camera, const Ray& ray,
                         ColliderShape shape = ColliderShape::Box)
{
    // The spot under the pointer. With nothing else under it, it is the point
    // of the ray that is as far from the camera as the point the camera looks
    // at...
    float reach    = kClickReach;
    float distance = std::min(camera.distance, kClickReach);
    Vec3  normal   = {0.0f, 1.0f, 0.0f}; // which way the surface faces there

    // ...but usually the ray comes down to the ground (y = 0)...
    if (ray.origin.y > 0.0f && ray.direction.y < 0.0f) {
        const float toGround = ray.origin.y / -ray.direction.y;
        if (toGround <= kClickReach) {
            reach    = toGround;
            distance = toGround;
        }
    }

    // ...or meets something solid before it does.
    RayHit hit;
    if (physics.raycast(ray, reach, hit)) {
        distance = hit.distance;
        normal   = hit.normal;
    }

    // A cube that is not turned reaches this far from its centre towards a
    // surface that faces `normal`. With its centre that far out, it touches
    // the surface without reaching into it.
    const float half      = kCubeSize * 0.5f;
    const float toSurface = shape == ColliderShape::Box
        ? (std::abs(normal.x) + std::abs(normal.y) + std::abs(normal.z)) * half
        : half + (shape == ColliderShape::Capsule ? 0.5f * std::abs(normal.y) : 0.0f);
    Vec3        center    = ray.origin + ray.direction * distance + normal * toSurface;
    center.y              = std::max(center.y, shape == ColliderShape::Capsule ? 1.0f : half); // on the ground, not in it

    return findRoom(physics, center, shape);
}

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

// Creates the window together with an OpenGL 3.3 core context. On failure both
// outputs are null and SDL_GetError() describes the problem.
static void createWindow(int multisamples, SDL_Window*& window, SDL_GLContext& context)
{
    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, multisamples > 0 ? 1 : 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, multisamples);

    window = SDL_CreateWindow("LegacyEngine", 1280, 720,
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

static bool run(SDL_Window* window, Renderer& renderer, bool smokeTest,const char* screenshotPath=nullptr)
{
    World   world;
    Physics physics;

    // Meshes are created once and can be shared by any number of entities.
    const MeshHandle gridMesh  = renderer.createMesh(makeGrid(kGridCells, kCellSize));
    const MeshHandle planeMesh = renderer.createMesh(makePlane(kPlaneSize, kPlaneSize));
    const MeshHandle cubeMesh  = renderer.createMesh(makeBox({kCubeSize, kCubeSize, kCubeSize}));

    // The grid is only drawn.
    const Entity grid = world.create();
    world.add(grid, ObjectName{"Grid"});
    world.add(grid, Transform{});
    world.add(grid, MeshRenderer{gridMesh, {0.55f, 0.58f, 0.62f}});

    // The plane is drawn and is solid. It has no thickness, and a collider
    // that thin would let fast objects slip through between two simulation
    // steps, so its collider is a 1 m thick slab whose top is the plane.
    const Entity plane = world.create();
    world.add(plane, ObjectName{"Plane"});
    world.add(plane, Transform{});
    world.add(plane, MeshRenderer{planeMesh, {1.0f, 1.0f, 1.0f}, true, 0.65f});
    world.add(plane, BoxCollider{{kPlaneSize, 1.0f, kPlaneSize}, {0.0f, -0.5f, 0.0f}});

    // An ordinary initial cube, with the same physics as other spawned shapes.
    const Entity cube = world.create();
    world.add(cube, ObjectName{"Cube"});
    world.add(cube, Transform{kCubeStart});
    world.add(cube, MeshRenderer{cubeMesh, {0.90f, 0.47f, 0.13f}});
    world.add(cube, BoxCollider{{kCubeSize, kCubeSize, kCubeSize}});
    world.add(cube,RigidBody{});

    const MeshHandle sphereMesh = renderer.createMesh(makeSphere(0.5f));
    const MeshHandle capsuleMesh = renderer.createMesh(makeCapsule(0.5f, 1.0f));
    const MeshHandle coneMesh = renderer.createMesh(makeCone(0.5f,1.0f));
    // Exercise both the editor-only camera model and active Game render path in CI.
    if(smokeTest) {
        const Entity testCamera=world.create();Transform pose{{3,2,6}};aimCamera(pose,{0,.5f,0});
        world.add(testCamera,ObjectName{"Smoke Camera"});world.add(testCamera,pose);
        Camera c;c.active=true;world.add(testCamera,c);
    }
    ColliderShape selected = ColliderShape::Box;
    bool spawnMode = false;
    bool freeCamera = true;
    float cameraBoost=3.0f;
    Entity selectedEntity;
    ScriptEditor scripts;
    scripts.bindWorld(world,cubeMesh,sphereMesh,capsuleMesh,coneMesh);
    EditorHistory history(world);
    PlayMode playMode;
    ObjectClipboard clipboard;
    AssetBrowser browser;
    ScriptComponents componentScripts;
    AudioSystem audio;
    SceneSurface sceneSurface,gameSurface;
    EditorRect sceneRect{};
    bool sceneHovered=false,resetWorkspace=false;
    Entity editSelection;
    TransformEditor editor;
    OrbitCamera camera;
    const ProjectIO::Meshes projectMeshes{gridMesh,planeMesh,cubeMesh,sphereMesh,capsuleMesh,coneMesh};
    ProjectDialog projectDialog;
    std::string currentProject,projectError,savedProjectState,lastWindowTitle;
    bool projectDirty=true;
    InputState input;InputSettings inputSettings;
    bool gameHovered=false,gameFocused=false;
    Uint64 lastTicks = SDL_GetTicksNS();
    bool   running   = true;

    // The left mouse button does two things. Pressed and released in one
    // place, it is a click, which spawns or selects. Moving away cancels
    // the click; camera navigation uses the right button and keyboard.
    bool        clicking   = false; // the button is down and this may still be a click
    float       pressX     = 0.0f;  // where the button went down
    float       pressY     = 0.0f;
    std::size_t clickCubes  = 0;     // how many cubes clicks have created

    bool cameraLooking=false;
    bool screenshotSaved=false;
    unsigned renderedFrames = 0;
    constexpr unsigned smokeFrames = 120;
    while (running) {
        int windowWidth=1,windowHeight=1;
        SDL_GetWindowSize(window,&windowWidth,&windowHeight);
        EditorLayout layout=EditorLayout::calculate(static_cast<float>(windowWidth),static_cast<float>(windowHeight),std::max(20.0f,ImGui::GetFrameHeight()));
        layout.scene=sceneRect;
        inputSettings.load(browser.assets,input);
        input.enabled=(SDL_GetWindowFlags(window)&SDL_WINDOW_INPUT_FOCUS) && playMode.playing() && (gameFocused || sceneHovered) && !ImGui::GetIO().WantTextInput && !inputSettings.visible;
        input.mouseEnabled=gameHovered || sceneHovered;input.beginFrame();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if(inputSettings.capture(event,browser.assets,input))continue;
            if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST ||
                (event.type==SDL_EVENT_MOUSE_BUTTON_UP && event.button.button==SDL_BUTTON_RIGHT)) {
                cameraLooking=false;SDL_SetWindowRelativeMouseMode(window,false);
            }
            ImGui_ImplSDL3_ProcessEvent(&event);
            input.event(event);
            const bool mouseEvent = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_MOTION ||
                event.type == SDL_EVENT_MOUSE_WHEEL;
            if (mouseEvent && (projectDialog.pending() || browser.dialog.pending() || (ImGui::GetIO().WantCaptureMouse && !sceneHovered && !cameraLooking) ||
                (!spawnMode && editor.capturesMouse()))) {
                clicking = false;
                continue;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && ImGui::GetIO().WantCaptureKeyboard) continue;
            switch (event.type) {
            case SDL_EVENT_DROP_FILE:
                if(!playMode.active() && event.drop.data)browser.importFile(event.drop.data);
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                editor.finish(world);
                clicking = false;
                break;
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                break; // Escape is reserved for UI cancellation, never application exit.
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if(event.button.button==SDL_BUTTON_RIGHT && freeCamera && sceneHovered &&
                    layout.scene.contains(event.button.x,event.button.y) && !ImGui::GetIO().WantTextInput && !cameraModifiersHeld(SDL_GetModState())) {
                    cameraLooking=SDL_SetWindowRelativeMouseMode(window,true);clicking=false;
                }
                if (event.button.button == SDL_BUTTON_LEFT) {
                    clicking = true;
                    pressX   = event.button.x;
                    pressY   = event.button.y;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (clicking &&
                    std::hypot(event.motion.x - pressX, event.motion.y - pressY) > kClickRadius) {
                    clicking = false; // too far for a click: a drag from here on
                }
                // Right-drag looks around from the camera's position. Left-drag
                // cancels a click but never moves the camera or selects/spawns.
                if (event.motion.state & SDL_BUTTON_RMASK) {
                    clicking = false;
                    if (cameraLooking && !cameraModifiersHeld(SDL_GetModState())) camera.look(event.motion.xrel, event.motion.yrel);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_LEFT && clicking) {
                    clicking = false;

                    // A scene click either spawns or selects, never both.
                    Ray ray;
                    if (mouseRay(window, camera, event.button.x, event.button.y, ray,layout.scene)) {
                        if (!spawnMode) {
                            RayHit hit;
                            const bool found=physics.raycast(ray,kClickReach,hit);
                            selectedEntity=found?hit.entity:Entity{};
                            float closest=found?hit.distance:kClickReach;
                            world.each<Camera,Transform>([&](Entity e,Camera&,Transform& t){
                                const auto offset=ray.origin-t.position;
                                const float radius=1.1f*std::max({std::abs(t.scale.x),std::abs(t.scale.y),std::abs(t.scale.z)});
                                const float b=dot(offset,ray.direction),disc=b*b-dot(offset,offset)+radius*radius;
                                if(disc>=0){float distance=-b-std::sqrt(disc);if(distance<0)distance=-b+std::sqrt(disc);
                                    if(distance>=0 && distance<closest){closest=distance;selectedEntity=e;}}
                            });
                            break;
                        }
                        const MeshHandle mesh = selected == ColliderShape::Box ? cubeMesh :
                            (selected == ColliderShape::Sphere ? sphereMesh : selected==ColliderShape::Capsule?capsuleMesh:coneMesh);
                        Vec3 center = placeForCube(physics, camera, ray, selected);
                        const Entity object = world.create();
                        const char* shapeName=selected==ColliderShape::Box ? "Cube" :
                            (selected==ColliderShape::Sphere ? "Sphere" : selected==ColliderShape::Capsule?"Capsule":"Cone");
                        world.add(object,ObjectName{std::string(shapeName)+" "+std::to_string(clickCubes+1)});
                        world.add(object, Transform{center});
                        world.add(object, MeshRenderer{mesh, kClickColors[clickCubes % std::size(kClickColors)]});
                        Collider collider;
                        collider.shape = selected;
                        world.add(object, collider);
                        world.add(object, RigidBody{});
                        ++clickCubes;
                    }
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL: {
                // Dolly in freecam; the static view ignores all navigation input.
                if (cameraLooking && !cameraModifiersHeld(SDL_GetModState())) {
                    if(SDL_GetModState()&SDL_KMOD_SHIFT)cameraBoost=adjustSceneCameraBoost(cameraBoost,event.wheel.y);
                    else camera.move(event.wheel.y,0,0,std::abs(event.wheel.y));
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
        bool openProject=false,saveProject=false,saveAsProject=false;
        const bool projectEnabled=!playMode.active() && !editor.dragging() && !projectDialog.pending() && !browser.dialog.pending();
        if(ImGui::BeginMainMenuBar()) {
            if(ImGui::BeginMenu("File")) {
                openProject=ImGui::MenuItem("Open Project...","Ctrl+O",false,projectEnabled);
                saveProject=ImGui::MenuItem("Save Project","Ctrl+S",false,projectEnabled);
                saveAsProject=ImGui::MenuItem("Save Project As...","Ctrl+Shift+S",false,projectEnabled);
                ImGui::Separator();if(ImGui::MenuItem("Quit"))running=false;
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        if(projectEnabled && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive()) {
            openProject |= ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_O,ImGuiInputFlags_RouteGlobal);
            saveProject |= ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S,ImGuiInputFlags_RouteGlobal);
            saveAsProject |= ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_S,ImGuiInputFlags_RouteGlobal);
        }
        auto saveTo=[&](const std::string& path) {
            auto assetRoot=projectAssetsPath(path);
            try { browser.assets.copyTo(assetRoot);
                AssetManager destination;destination.root=assetRoot;
                inputSettings.save(destination,input);if(!inputSettings.error.empty())throw std::runtime_error(inputSettings.error);
            }
            catch(const std::exception& e){projectError=e.what();return;}
            if(ProjectIO::save(path,world,scripts.source(),camera,projectMeshes,projectError)) {
                if(std::filesystem::weakly_canonical(browser.assets.root)!=std::filesystem::weakly_canonical(assetRoot))
                    history=EditorHistory(world,selectedEntity); // Old Trash receipts belong to the previous project root.
                browser.assets.root=assetRoot;
                currentProject=path;
                savedProjectState=projectContentState(world,scripts.source(),camera);
                projectDirty=false;
            }
        };
        if(projectEnabled) {
            if(saveProject && !currentProject.empty())saveTo(currentProject);
            else if(openProject || saveProject || saveAsProject) {
                bool proceed=true;
                if(openProject && projectDirty) {
                    const SDL_MessageBoxButtonData buttons[]{
                        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,0,"Cancel"},
                        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,1,"Open project"}};
                    const SDL_MessageBoxData box{SDL_MESSAGEBOX_WARNING,window,"Unsaved project",
                        "Opening another project replaces unsaved changes. Continue?",2,buttons,nullptr};
                    int button=0;
                    proceed=SDL_ShowMessageBox(&box,&button) && button==1;
                }
                if(proceed)projectDialog.show(window,!openProject,currentProject);
            }
        }
        if(auto result=projectDialog.poll()) {
            if(!result->error.empty())projectError=result->error;
            else if(!result->path.empty()) {
                if(result->saving) {
                    auto path=std::filesystem::path(result->path);
                    if(!path.has_extension())path+=".legacy";
                    saveTo(path.string());
                } else {
                    std::string source,assetDirectory;World loadedWorld;OrbitCamera loadedCamera;
                    if(ProjectIO::load(result->path,loadedWorld,source,loadedCamera,projectMeshes,projectError,&assetDirectory)) {
                        audio.clear();componentScripts.stop();scripts.shutdown();world.restore(loadedWorld);camera=loadedCamera;
                        browser.setRoot(assetDirectory.empty()?existingProjectAssetsPath(result->path):std::filesystem::absolute(result->path).parent_path()/assetDirectory);
                        scripts.resetRuntime();scripts.setSource(source);
                        selectedEntity={};history=EditorHistory(world);physics.reset();
                        clicking=false;spawnMode=false;currentProject=result->path;
                        savedProjectState=projectContentState(world,scripts.source(),camera);
                        projectDirty=false;
                    }
                }
            }
        }
        if(!projectError.empty()) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR,"Project error",projectError.c_str(),window);
            projectError.clear();
        }
        drawWorkspace(layout.toolbar,resetWorkspace);
        ImGui::BeginDisabled(projectDialog.pending() || browser.dialog.pending());
        const int created=drawCreateMenu();
        auto createObject=[&](int created) {
            editor.finish(world);spawnMode=false;clicking=false;
            const char* labels[]{"Empty GameObject","Capsule","Cone","Cube","Sphere","Camera","Audio Source"};
            const MeshHandle meshes[]{MeshHandle{},capsuleMesh,coneMesh,cubeMesh,sphereMesh,MeshHandle{},MeshHandle{}};
            const ColliderShape shapes[]{ColliderShape::Box,ColliderShape::Capsule,ColliderShape::Cone,ColliderShape::Box,ColliderShape::Sphere,ColliderShape::Box,ColliderShape::Box};
            Vec3 position=camera.target;
            position.y=std::max(position.y,created==1?1.0f:created==0?0.0f:0.5f);
            if(created>0 && created<5)position=findRoom(physics,position,shapes[created]);
            selectedEntity=createLevelObject(world,labels[created],position,meshes[created],shapes[created],created==0 || created>=5);
            if(created==6)world.add(selectedEntity,AudioSource{});
            if(created==5){Camera c;c.active=activeCamera(world)==Entity{};world.add(selectedEntity,c);}
        };
        if(created>=0)createObject(created);
        bool undoRequested=false,redoRequested=false;
        bool duplicateRequested=false,copyRequested=false,pasteRequested=false,parentRequested=false;
        const bool editAllowed=!playMode.active() && !editor.dragging() && !projectDialog.pending() && !browser.dialog.pending();
        const bool objectSelected=world.alive(selectedEntity);
        if(ImGui::BeginMainMenuBar()) {
            if(ImGui::BeginMenu("Edit")) {
                undoRequested=ImGui::MenuItem("Undo","Ctrl+Z",false,editAllowed && history.canUndo());
                redoRequested=ImGui::MenuItem("Redo","Ctrl+Y / Ctrl+Shift+Z",false,editAllowed && history.canRedo());
                ImGui::Separator();
                duplicateRequested=ImGui::MenuItem("Duplicate","Ctrl+D",false,editAllowed && objectSelected);
                copyRequested=ImGui::MenuItem("Copy","Ctrl+C",false,editAllowed && objectSelected);
                pasteRequested=ImGui::MenuItem("Paste","Ctrl+V",false,editAllowed && clipboard.available());
                parentRequested=ImGui::MenuItem("Parent / Unparent","Ctrl+P",false,editAllowed && objectSelected);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        if(ImGui::BeginMainMenuBar()) {
            if(ImGui::BeginMenu("Windows")) {
                for(int i=0;i<7;++i)ImGui::MenuItem(panelNames[i],nullptr,&panelOpen[i]);
                ImGui::Separator();if(ImGui::MenuItem("Reset layout"))resetWorkspace=true;
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        bool showAbout=false;
        if(ImGui::BeginMainMenuBar()) {
            if(ImGui::BeginMenu("Project")){if(ImGui::MenuItem("Input Settings"))inputSettings.visible=true;ImGui::EndMenu();}
            if(ImGui::BeginMenu("Settings")){if(ImGui::MenuItem("Editor Settings"))browser.settings.visible=true;ImGui::EndMenu();}
            if(ImGui::BeginMenu("Help")) {
                if(ImGui::MenuItem("GitHub"))SDL_OpenURL("https://github.com/monsef64/LegacyEngine");
                showAbout=ImGui::MenuItem("About");ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
        if(showAbout)ImGui::OpenPopup("About LegacyEngine");
        if(ImGui::BeginPopupModal("About LegacyEngine",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("LegacyEngine");ImGui::TextUnformatted("3D scene editor with LegacyScript (Lua 5.4)");
            ImGui::TextUnformatted("SDL3 / OpenGL / Dear ImGui / ReactPhysics3D");
            if(ImGui::Button("Close") || ImGui::IsKeyPressed(ImGuiKey_Escape))ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        // Let text widgets own editing shortcuts while typing. Global routing
        // otherwise makes shortcuts independent of which editor panel has focus.
        if(editAllowed && !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive()) {
            const auto flags=ImGuiInputFlags_RouteGlobal;
            undoRequested |= ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z,flags);
            redoRequested |= ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y,flags) ||
                ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z,flags);
            duplicateRequested |= ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D,flags);
            copyRequested |= ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_C,flags);
            pasteRequested |= ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_V,flags);
            parentRequested |= ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_P,flags);
        }
        ImGui::SetNextWindowPos(ImVec2(layout.toolbar.x,layout.toolbar.y));
        ImGui::SetNextWindowSize(ImVec2(layout.toolbar.width,layout.toolbar.height));
        ImGui::Begin("Scene controls",nullptr,fixedPanelFlags | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDocking);
        if(editAllowed) {
            if(copyRequested && objectSelected)clipboard.copy(world,selectedEntity);
            if((duplicateRequested && objectSelected) || (pasteRequested && clipboard.available())) {
                editor.finish(world);clicking=false;spawnMode=false;
                history.commit(world,selectedEntity);
                selectedEntity=duplicateRequested?duplicateLevelObject(world,selectedEntity):clipboard.paste(world);
                history.commit(world,selectedEntity);
            }
            if(parentRequested && objectSelected) {
                editor.finish(world);clicking=false;
                if(getParent(world,selectedEntity)!=Entity{}) {
                    history.commit(world,selectedEntity);
                    setParent(world,selectedEntity,{});history.commit(world,selectedEntity);
                } else ImGui::OpenPopup("Choose parent");
            }
        }
        if(ImGui::BeginPopup("Choose parent")) {
            ImGui::TextUnformatted("Choose parent for selected GameObject");
            ImGui::Separator();
            for(const auto& object:levelObjects(world)) {
                if(!editAllowed || !canParent(world,selectedEntity,object.entity))continue;
                ImGui::PushID(static_cast<int>(object.entity.index));
                if(ImGui::Selectable(object.name.c_str())) {
                    history.commit(world,selectedEntity);
                    setParent(world,selectedEntity,object.entity);history.commit(world,selectedEntity);
                }
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
        if ((undoRequested || redoRequested) && !playMode.active()) {
            editor.finish(world);clicking=false;
            history.commit(world,selectedEntity);
            const bool restored=undoRequested?history.undo(world,selectedEntity):history.redo(world,selectedEntity);
            if(restored)physics.reset();
            else if(!history.error.empty()){projectError=history.error;history.error.clear();}
        }
        const bool playbackKeys=!ImGui::GetIO().WantTextInput && !editor.dragging() && !projectDialog.pending() && !browser.dialog.pending();
        const bool playKey=playbackKeys && ImGui::Shortcut(ImGuiKey_F5,ImGuiInputFlags_RouteGlobal);
        const bool pauseKey=playbackKeys && ImGui::Shortcut(ImGuiKey_F6,ImGuiInputFlags_RouteGlobal);
        const bool stopKey=playbackKeys && ImGui::Shortcut(ImGuiKey_F7,ImGuiInputFlags_RouteGlobal);
        const float playbackWidth=90+2*ImGui::GetStyle().ItemSpacing.x;
        ImGui::SetCursorPosX((layout.toolbar.width-playbackWidth)*0.5f);
        ImGui::BeginDisabled(playMode.active());
        if (playbackButton("##Play","Play (F5)",PlaybackIcon::Play,playMode.active()) || (playKey && !playMode.active())) {
            editor.finish(world); clicking=false;
            editSelection=selectedEntity;
            history.commit(world,selectedEntity);
            if (playMode.play(world)) { physics.reset(); audio.start(world);scripts.setAssetContext(browser.openedScript,selectedEntity);scripts.startPlay();
                const MeshHandle runtimeMeshes[]{cubeMesh,sphereMesh,capsuleMesh,coneMesh};
                componentScripts.start(world,browser.assets,runtimeMeshes); }
        }
        ImGui::EndDisabled(); ImGui::SameLine();
        ImGui::BeginDisabled(!playMode.active());
        if (playbackButton("##Pause",playMode.state()==PlayMode::State::Paused?"Resume (F6)":"Pause (F6)",PlaybackIcon::Pause,playMode.state()==PlayMode::State::Paused) || (pauseKey && playMode.active())) {
            editor.finish(world); clicking=false; playMode.pause();
        }
        ImGui::SameLine();
        if (playbackButton("##Stop","Stop (F7)",PlaybackIcon::Stop) || (stopKey && playMode.active())) {
            editor.finish(world); clicking=false;
            // Run stop callbacks before restoring, so their edits are discarded too.
            audio.clear();componentScripts.stop();scripts.shutdown();
            playMode.stop(world);
            physics.reset(); scripts.resetRuntime();
            selectedEntity=world.alive(editSelection)?editSelection:Entity{};
        }
        ImGui::EndDisabled(); ImGui::SameLine();
        const bool compactToolbar=layout.toolbar.width<1000;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),layout.toolbar.width-(compactToolbar?125.0f:370.0f)));
        if(compactToolbar && ImGui::Button("Scene options")) ImGui::OpenPopup("Scene options popup");
        if(!compactToolbar || ImGui::BeginPopup("Scene options popup")) {
        if(ImGui::Checkbox("Spawn Mode",&spawnMode)){editor.finish(world);selectedEntity={};clicking=false;}
        ImGui::SameLine();
        if(ImGui::Checkbox("Freecam",&freeCamera))clicking=false;
        ImGui::SameLine();
        const char* names[]{"Cube","Sphere","Capsule","Cone"};int shape=static_cast<int>(selected);
        ImGui::SetNextItemWidth(100);ImGui::BeginDisabled(!spawnMode);
        if(ImGui::Combo("Shape",&shape,names,4))selected=static_cast<ColliderShape>(shape);
        ImGui::EndDisabled();
            if(compactToolbar)ImGui::EndPopup();
        }
        ImGui::End();
        bool hierarchyFocused=false;int hierarchyCreate=-1;
        const std::string hierarchyLevelName=currentProject.empty()?"Untitled":std::filesystem::path(currentProject).stem().string();
        if (drawLevelObjects(world,selectedEntity,!spawnMode,&layout.hierarchy,&hierarchyFocused,&hierarchyCreate,hierarchyLevelName.c_str())) {editor.finish(world);clicking=false;}
        if(hierarchyCreate>=0)createObject(hierarchyCreate);
        const MeshHandle meshOptions[]{cubeMesh,sphereMesh,capsuleMesh,coneMesh};
        drawInspector(world,selectedEntity,layout.inspector,meshOptions,editor.dragging(),&browser.assets);
        scripts.componentOutput=componentScripts.messages();
        scripts.setAssetContext(browser.openedScript,selectedEntity);
        scripts.draw(&layout,playMode.playing());
        browser.projectFolder=currentProject.empty()?browser.assets.root:std::filesystem::absolute(currentProject).parent_path();
        browser.draw(window,world,selectedEntity,scripts,editAllowed,&history);
        browser.settings.draw(browser.projectFolder);
        inputSettings.draw(browser.assets,input,editAllowed);
        if(!audio.error.empty()){ImGui::Begin("Audio errors");ImGui::TextWrapped("%s",audio.error.c_str());if(ImGui::Button("Dismiss"))audio.error.clear();ImGui::End();}
        ImDrawList* sceneDrawList=nullptr;bool sceneVisible=false,sceneFocused=false;
        sceneHovered=false;sceneRect={};
        if(panelOpen[2]) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
            if(beginEditorPanel("Scene")) {
                sceneFocused=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
                const auto pos=ImGui::GetCursorScreenPos(),size=ImGui::GetContentRegionAvail();
                sceneRect={pos.x,pos.y,std::max(size.x,1.0f),std::max(size.y,1.0f)};
                layout.scene=sceneRect;sceneVisible=true;sceneDrawList=ImGui::GetWindowDrawList();
                int dw=0,dh=0;SDL_GetWindowSizeInPixels(window,&dw,&dh);
                auto pixels=scenePixels(sceneRect,windowWidth,windowHeight,dw,dh);
                sceneSurface.resize(pixels.width,pixels.height);
                ImGui::Image(static_cast<ImTextureID>(sceneSurface.texture),ImVec2(sceneRect.width,sceneRect.height),ImVec2(0,1),ImVec2(1,0));
                sceneHovered=ImGui::IsWindowHovered(ImGuiHoveredFlags_None) && ImGui::IsItemHovered();
                if(ImGui::BeginDragDropTarget()) {
                    if(const auto* asset=ImGui::AcceptDragDropPayload("ASSET_PATH"))
                        attachScriptAsset(world,selectedEntity,static_cast<const char*>(asset->Data));
                    ImGui::EndDragDropTarget();
                }
                if(editAllowed && (sceneFocused || sceneHovered) && !cameraLooking && !ImGui::GetIO().WantTextInput &&
                    !ImGui::IsAnyItemActive() && !cameraModifiersHeld(SDL_GetModState()) &&
                    !ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId)) {
                    if(ImGui::IsKeyPressed(ImGuiKey_W,false))editor.setTool(TransformTool::Move);
                    if(ImGui::IsKeyPressed(ImGuiKey_E,false))editor.setTool(TransformTool::Rotate);
                    if(ImGui::IsKeyPressed(ImGuiKey_R,false))editor.setTool(TransformTool::Scale);
                }
                editor.draw(world,selectedEntity,!spawnMode && !cameraLooking,camera,static_cast<float>(sceneSurface.width)/sceneSurface.height,&sceneRect,sceneDrawList);
                if(editor.capturesMouse())clicking=false;
            }
            ImGui::End();ImGui::PopStyleVar();
        }
        bool gameVisible=false;
        gameHovered=false;gameFocused=false;
        if(panelOpen[6]) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
            if(smokeTest && renderedFrames==0)ImGui::SetNextWindowFocus();
            if(beginEditorPanel("Game")) {
                gameHovered=ImGui::IsWindowHovered();gameFocused=ImGui::IsWindowFocused();
                const auto active=activeCamera(world);
                if(active==Entity{})ImGui::TextUnformatted("No active camera. Create > Camera or createCamera(name).");
                else {
                    auto size=ImGui::GetContentRegionAvail();size.x=std::max(size.x,1.0f);size.y=std::max(size.y,1.0f);
                    int dw=0,dh=0;SDL_GetWindowSizeInPixels(window,&dw,&dh);
                    gameSurface.resize(static_cast<int>(size.x*dw/std::max(windowWidth,1)),static_cast<int>(size.y*dh/std::max(windowHeight,1)));
                    world.each<Camera>([&](Entity,Camera& c){c.aspect=static_cast<float>(gameSurface.width)/gameSurface.height;});
                    ImGui::Image(static_cast<ImTextureID>(gameSurface.texture),size,ImVec2(0,1),ImVec2(1,0));gameVisible=true;
                }
            }
            ImGui::End();ImGui::PopStyleVar();
        }
        if(!sceneVisible) {editor.finish(world);layout.scene={};}
        if(editAllowed && (hierarchyFocused || sceneFocused) && world.alive(selectedEntity) &&
            !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
            !ImGui::IsPopupOpen("",ImGuiPopupFlags_AnyPopupId) && ImGui::IsKeyPressed(ImGuiKey_Delete,false)) {
            editor.finish(world);clicking=false;history.commit(world,selectedEntity);
            deleteLevelObject(world,selectedEntity);selectedEntity={};
            history.commit(world,selectedEntity);physics.reset();
        }
        if (!playMode.active()) {
            updateHierarchy(world);
            history.observe(world,selectedEntity,editor.dragging() || ImGui::IsAnyItemActive() ||
                ImGui::IsMouseDown(ImGuiMouseButton_Left));
        }
        ImGui::EndDisabled();
        ImGui::Render();

        // The time that has passed since the previous frame.
        const Uint64 ticks   = SDL_GetTicksNS();
        const float  seconds = static_cast<float>(ticks - lastTicks) * 1e-9f;
        lastTicks            = ticks;

        if(cameraLooking && (!freeCamera || !sceneVisible || browser.dialog.pending() || projectDialog.pending() ||
            !(SDL_GetWindowFlags(window)&SDL_WINDOW_INPUT_FOCUS))) {
            cameraLooking=false;SDL_SetWindowRelativeMouseMode(window,false);
        }
        if (cameraLooking && !browser.dialog.pending() && !projectDialog.pending() && !cameraModifiersHeld(SDL_GetModState()) && !editor.dragging() && !ImGui::GetIO().WantTextInput &&
            (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)) {
            const bool* keys = SDL_GetKeyboardState(nullptr);
            const float speed = sceneCameraSpeed(SDL_GetModState(),cameraBoost);
            camera.move(static_cast<float>(keys[SDL_SCANCODE_W]) - keys[SDL_SCANCODE_S],
                        static_cast<float>(keys[SDL_SCANCODE_D]) - keys[SDL_SCANCODE_A],
                        static_cast<float>(keys[SDL_SCANCODE_E]) - keys[SDL_SCANCODE_Q],
                        speed * std::min(seconds, 0.1f));
        }
        if (playMode.playing()) {scripts.update(std::min(seconds,0.25f));componentScripts.update(std::min(seconds,0.25f));}
        updateHierarchy(world);
        physics.update(world, playMode.playing()?seconds:0.0f, [&](float dt){ scripts.fixedUpdate(dt);componentScripts.fixedUpdate(dt);updateHierarchy(world); });
        updateHierarchy(world);
        updateCameras(world,playMode.playing()?std::min(seconds,.25f):0);
        updateHierarchy(world);
        audio.update(world,browser.assets,playMode.playing());
        physics.update(world,0); // collider poses follow parent motion before picking next frame

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        if (width > 0 && height > 0) {
            if(sceneVisible) {
            glBindFramebuffer(GL_FRAMEBUFFER,sceneSurface.framebuffer);
            const float aspect=static_cast<float>(sceneSurface.width)/sceneSurface.height;
            const Mat4 viewProjection = camera.viewProjection(aspect);
            renderer.draw(world, viewProjection, sceneSurface.width, sceneSurface.height, camera.position());
            if (!spawnMode && world.alive(selectedEntity)) {
                const auto* transform = world.tryGet<Transform>(selectedEntity);
                const auto* collider = world.tryGet<Collider>(selectedEntity);
                if (transform && collider) renderer.drawCollider(*transform, *collider, viewProjection);
                const auto* audioSource=world.tryGet<AudioSource>(selectedEntity);
                if(transform && audioSource)renderer.drawAudioSource(*transform,*audioSource,viewProjection);
            } else {
                selectedEntity = {};
            }
            }
            if(gameVisible && activeCamera(world)!=Entity{}) {
                const auto e=activeCamera(world);const auto& c=world.get<Camera>(e);const auto& t=world.get<Transform>(e);
                glBindFramebuffer(GL_FRAMEBUFFER,gameSurface.framebuffer);
                try {
                    const int x=static_cast<int>(c.viewportX*gameSurface.width),y=static_cast<int>((1-c.viewportY-c.viewportHeight)*gameSurface.height);
                    renderer.draw(world,cameraMatrix(t,c),std::max(1,static_cast<int>(c.viewportWidth*gameSurface.width)),std::max(1,static_cast<int>(c.viewportHeight*gameSurface.height)),cameraEye(t,c),x,y,false);
                }catch(const std::exception&){glClearColor(.1f,.1f,.1f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);}
            }
            glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,width,height);
            glDisable(GL_SCISSOR_TEST);glClearColor(.07f,.08f,.10f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }

        // Compare persisted content, not history depth: Undo back to the saved
        // state clears the marker, while Lua/camera edits also mark it dirty.
        if(!playMode.active() && !editor.dragging()) {
            projectDirty=currentProject.empty() ||
                projectContentState(world,scripts.source(),camera)!=savedProjectState;
        }
        const std::string title=projectWindowTitle(currentProject,projectDirty);
        if(title!=lastWindowTitle){SDL_SetWindowTitle(window,title.c_str());lastWindowTitle=title;}
        if(screenshotPath && renderedFrames==smokeFrames-1 && width>0 && height>0){
            std::vector<unsigned char> pixels(static_cast<std::size_t>(width)*height*4);
            glPixelStorei(GL_PACK_ALIGNMENT,4);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
            const auto stride=static_cast<std::size_t>(width)*4;
            for(int y=0;y<height/2;++y)std::swap_ranges(pixels.begin()+y*stride,pixels.begin()+(y+1)*stride,pixels.begin()+(height-y-1)*stride);
            auto* image=SDL_CreateSurfaceFrom(width,height,SDL_PIXELFORMAT_RGBA32,pixels.data(),width*4);
            screenshotSaved=image && SDL_SaveBMP(image,screenshotPath);if(image)SDL_DestroySurface(image);
            if(!screenshotSaved)SDL_Log("Screenshot failed: %s",SDL_GetError());
        }
        SDL_GL_SwapWindow(window);
        if (width > 0 && height > 0) {
            ++renderedFrames;
            if ((smokeTest || screenshotPath) && renderedFrames >= smokeFrames) running = false;
        }
    }
    editor.finish(world);
    audio.clear();componentScripts.stop();scripts.shutdown();
    return (!smokeTest || renderedFrames >= smokeFrames) && (!screenshotPath || screenshotSaved);
}

int main(int argc, char** argv)
{
    bool smokeTest = false;const char* screenshotPath=nullptr;
    for (int i=1; i<argc; ++i) {
        if (std::strcmp(argv[i], "--smoke-test") == 0) smokeTest = true;
        else if(std::strcmp(argv[i],"--screenshot")==0 && i+1<argc)screenshotPath=argv[++i];
        else { SDL_Log("Unknown argument: %s", argv[i]); return 1; }
    }
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not start SDL: %s", SDL_GetError());
        return 1;
    }

    // Prefer 4x anti-aliasing so the grid lines look smooth, but not every
    // driver offers it: fall back to a plain framebuffer.
    SDL_Window*   window  = nullptr;
    SDL_GLContext context = nullptr;
    createWindow(4, window, context);
    if (!context) {
        createWindow(0, window, context);
    }
    if (!context) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not create an OpenGL 3.3 window: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GL_SetSwapInterval(1); // vsync, where the driver allows it

    // If the renderer cannot start, the reason is already logged: skip the
    // loop and exit with an error.
    Renderer   renderer;
    const bool rendererReady = renderer.init();
    bool runSucceeded = false;
    if (rendererReady) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        dockedEditor=true;
        ImGui::GetIO().IniFilename = screenshotPath?nullptr:"legacy-layout.ini";
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
        runSucceeded = run(window, renderer, smokeTest,screenshotPath);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        renderer.shutdown();
    }

    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    if (smokeTest && runSucceeded) SDL_Log("Smoke test passed: rendered 120 frames and shut down cleanly");
    return rendererReady && runSucceeded ? 0 : 1;
}
