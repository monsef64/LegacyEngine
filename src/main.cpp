// LegacyEngine - a small 3D editor with an ordinary 1 m cube above a
// 16 x 16 m plane, on a 32 x 32 m grid.
// A click in spawn mode puts a new shape under the mouse pointer.
//
// World conventions:
//   * 1 unit = 1 metre
//   * +Y is up, the ground is the XZ plane (y = 0)
//   * right-handed coordinates (the OpenGL convention)

#include "camera.hpp"
#include "components.hpp"
#include "ecs.hpp"
#include "mesh.hpp"
#include "physics.hpp"
#include "renderer.hpp"
#include "transform_editor.hpp"
#include "script_editor.hpp"
#include "level_objects.hpp"
#include "create_menu.hpp"
#include "editor_layout.hpp"
#include "inspector.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_opengl3.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
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

static void run(SDL_Window* window, Renderer& renderer)
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
    ColliderShape selected = ColliderShape::Box;
    bool spawnMode = true;
    bool freeCamera = true;
    Entity selectedEntity;
    ScriptEditor scripts;
    scripts.bindWorld(world,cubeMesh,sphereMesh,capsuleMesh,coneMesh);
    TransformEditor editor;
    OrbitCamera camera;
    Uint64 lastTicks = SDL_GetTicksNS();
    bool   running   = true;

    // The left mouse button does two things. Pressed and released in one
    // place, it is a click, which spawns or selects. Moving away cancels
    // the click; camera navigation uses the right button and keyboard.
    bool        clicking   = false; // the button is down and this may still be a click
    float       pressX     = 0.0f;  // where the button went down
    float       pressY     = 0.0f;
    std::size_t clickCubes  = 0;     // how many cubes clicks have created

    while (running) {
        int windowWidth=1,windowHeight=1;
        SDL_GetWindowSize(window,&windowWidth,&windowHeight);
        const EditorLayout layout=EditorLayout::calculate(static_cast<float>(windowWidth),static_cast<float>(windowHeight),std::max(20.0f,ImGui::GetFrameHeight()));
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            const bool mouseEvent = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
                event.type == SDL_EVENT_MOUSE_BUTTON_UP || event.type == SDL_EVENT_MOUSE_MOTION ||
                event.type == SDL_EVENT_MOUSE_WHEEL;
            if (mouseEvent && (ImGui::GetIO().WantCaptureMouse ||
                (!spawnMode && editor.capturesMouse()))) {
                clicking = false;
                continue;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && ImGui::GetIO().WantCaptureKeyboard &&
                event.key.key != SDLK_ESCAPE) continue;
            switch (event.type) {
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                editor.finish(world);
                clicking = false;
                break;
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE) {
                    running = false;
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
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
                    if (freeCamera && layout.scene.contains(event.motion.x,event.motion.y)) camera.look(event.motion.xrel, event.motion.yrel);
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
                            selectedEntity = physics.raycast(ray, kClickReach, hit) ? hit.entity : Entity{};
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
                float mx=0,my=0;SDL_GetMouseState(&mx,&my);
                if (freeCamera && layout.scene.contains(mx,my)) camera.move(event.wheel.y, 0, 0, std::abs(event.wheel.y));
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
        const int created=drawCreateMenu();
        if(created>=0) {
            editor.finish(world);spawnMode=false;clicking=false;
            const char* labels[]{"Empty GameObject","Capsule","Cone","Cube","Sphere"};
            const MeshHandle meshes[]{MeshHandle{},capsuleMesh,coneMesh,cubeMesh,sphereMesh};
            const ColliderShape shapes[]{ColliderShape::Box,ColliderShape::Capsule,ColliderShape::Cone,ColliderShape::Box,ColliderShape::Sphere};
            Vec3 position=camera.target;
            position.y=std::max(position.y,created==1?1.0f:created==0?0.0f:0.5f);
            if(created!=0)position=findRoom(physics,position,shapes[created]);
            selectedEntity=createLevelObject(world,labels[created],position,meshes[created],shapes[created],created==0);
        }
        placeEditorPanel(layout.toolbar);
        ImGui::Begin("Scene controls",nullptr,fixedPanelFlags | ImGuiWindowFlags_NoTitleBar);
        if(ImGui::Checkbox("Spawn Mode",&spawnMode)){editor.finish(world);selectedEntity={};clicking=false;}
        ImGui::SameLine();
        if(ImGui::Checkbox("Freecam",&freeCamera))clicking=false;
        ImGui::SameLine();
        const char* names[]{"Cube","Sphere","Capsule","Cone"};int shape=static_cast<int>(selected);
        ImGui::SetNextItemWidth(100);ImGui::BeginDisabled(!spawnMode);
        if(ImGui::Combo("Shape",&shape,names,4))selected=static_cast<ColliderShape>(shape);
        ImGui::EndDisabled();ImGui::SameLine();
        ImGui::TextDisabled("WASD/QE: move | RMB: look | Shift: faster");
        ImGui::End();
        if (drawLevelObjects(world,selectedEntity,!spawnMode,&layout.hierarchy)) {editor.finish(world);clicking=false;}
        const MeshHandle meshOptions[]{cubeMesh,sphereMesh,capsuleMesh,coneMesh};
        drawInspector(world,selectedEntity,layout.inspector,meshOptions,editor.dragging());
        scripts.draw(&layout);
        // A non-interactive Scene title is drawn over the actual GL viewport.
        ImGui::GetBackgroundDrawList()->AddText(ImVec2(layout.scene.x+8,layout.scene.y+6),IM_COL32(170,180,190,255),"Scene");
        int drawableWidth=0, drawableHeight=0;
        SDL_GetWindowSizeInPixels(window,&drawableWidth,&drawableHeight);
        if (drawableWidth>0 && drawableHeight>0) {
            const auto pixels=scenePixels(layout.scene,windowWidth,windowHeight,drawableWidth,drawableHeight);
            const EditorRect gizmoRect{static_cast<float>(pixels.x)*windowWidth/drawableWidth,
                static_cast<float>(pixels.top)*windowHeight/drawableHeight,
                static_cast<float>(pixels.width)*windowWidth/drawableWidth,
                static_cast<float>(pixels.height)*windowHeight/drawableHeight};
            editor.draw(world,selectedEntity,!spawnMode,camera,pixels.aspect(),&gizmoRect);
            if (editor.capturesMouse()) clicking=false;
        } else editor.finish(world);
        ImGui::Render();

        // The time that has passed since the previous frame.
        const Uint64 ticks   = SDL_GetTicksNS();
        const float  seconds = static_cast<float>(ticks - lastTicks) * 1e-9f;
        lastTicks            = ticks;

        float mouseX=0,mouseY=0;SDL_GetMouseState(&mouseX,&mouseY);
        if (freeCamera && layout.scene.contains(mouseX,mouseY) && !editor.dragging() && !ImGui::GetIO().WantCaptureKeyboard &&
            (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)) {
            const bool* keys = SDL_GetKeyboardState(nullptr);
            const float speed = keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT] ? 15.0f : 5.0f;
            camera.move(static_cast<float>(keys[SDL_SCANCODE_W]) - keys[SDL_SCANCODE_S],
                        static_cast<float>(keys[SDL_SCANCODE_D]) - keys[SDL_SCANCODE_A],
                        static_cast<float>(keys[SDL_SCANCODE_E]) - keys[SDL_SCANCODE_Q],
                        speed * std::min(seconds, 0.1f));
        }
        scripts.update(std::min(seconds,0.25f));
        updateHierarchy(world);
        physics.update(world, seconds, [&](float dt){ scripts.fixedUpdate(dt);updateHierarchy(world); });
        updateHierarchy(world);
        physics.update(world,0); // collider poses follow parent motion before picking next frame

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        if (width > 0 && height > 0) {
            const auto pixels=scenePixels(layout.scene,windowWidth,windowHeight,width,height);
            const float aspect=pixels.aspect();
            const Mat4 viewProjection = camera.viewProjection(aspect);
            renderer.draw(world, viewProjection, pixels.width, pixels.height, camera.position(),pixels.x,pixels.bottom(height));
            if (!spawnMode && world.alive(selectedEntity)) {
                const auto* transform = world.tryGet<Transform>(selectedEntity);
                const auto* collider = world.tryGet<Collider>(selectedEntity);
                if (transform && collider) renderer.drawCollider(*transform, *collider, viewProjection);
            } else {
                selectedEntity = {};
            }
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }

        SDL_GL_SwapWindow(window);
    }
    editor.finish(world);
    scripts.shutdown();
}

int main(int, char**)
{
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
    if (rendererReady) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::StyleColorsDark();
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
        run(window, renderer);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        renderer.shutdown();
    }

    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return rendererReady ? 0 : 1;
}
