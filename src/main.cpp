// LegacyEngine - a small 3D scene: an orange 1 m cube floating above a
// 5 x 5 m plane, on a 7 x 7 m grid. Pressing F drops the cube onto the plane.
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

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>

// ---------------------------------------------------------------------------
// Scene description
// ---------------------------------------------------------------------------

constexpr int   kGridCells = 7;    // grid blocks per side
constexpr float kCellSize  = 1.0f; // metres per grid block
constexpr float kPlaneSize = 5.0f; // metres per side of the plane
constexpr float kCubeSize  = 1.0f; // metres per edge of the cube

// Where the centre of the cube is before it falls.
constexpr Vec3 kCubeStart = {0.0f, 1.0f, 0.0f};

// Puts the cube back where it started and lets it fall from there.
static void dropCube(World& world, Entity cube)
{
    Transform& transform = world.get<Transform>(cube);
    transform.position   = kCubeStart;
    transform.rotation   = {};

    RigidBody& body      = world.get<RigidBody>(cube);
    body.linearVelocity  = {};
    body.angularVelocity = {};
    body.useGravity      = true;
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
                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
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
    world.add(grid, Transform{});
    world.add(grid, MeshRenderer{gridMesh, {0.55f, 0.58f, 0.62f}});

    // The plane is drawn and is solid. It has no thickness, and a collider
    // that thin would let fast objects slip through between two simulation
    // steps, so its collider is a 1 m thick slab whose top is the plane.
    const Entity plane = world.create();
    world.add(plane, Transform{});
    world.add(plane, MeshRenderer{planeMesh, {0.30f, 0.34f, 0.40f}});
    world.add(plane, BoxCollider{{kPlaneSize, 1.0f, kPlaneSize}, {0.0f, -0.5f, 0.0f}});

    // The cube is drawn, is solid and is moved by the physics. Gravity is off
    // at first, so it floats until F is pressed.
    const Entity cube = world.create();
    world.add(cube, Transform{kCubeStart});
    world.add(cube, MeshRenderer{cubeMesh, {0.90f, 0.47f, 0.13f}});
    world.add(cube, BoxCollider{{kCubeSize, kCubeSize, kCubeSize}});
    world.add<RigidBody>(cube).useGravity = false;

    OrbitCamera camera;
    Uint64 lastTicks = SDL_GetTicksNS();
    bool   running   = true;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE) {
                    running = false;
                } else if (event.key.key == SDLK_F && !event.key.repeat) {
                    dropCube(world, cube);
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                // Drag with the left button to orbit, as if grabbing the world.
                if (event.motion.state & SDL_BUTTON_LMASK) {
                    camera.yaw -= event.motion.xrel * 0.005f;
                    camera.pitch = std::clamp(camera.pitch + event.motion.yrel * 0.005f,
                                              radians(2.0f), radians(89.0f));
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                // Scroll to zoom in and out.
                camera.distance = std::clamp(camera.distance * std::pow(0.9f, event.wheel.y),
                                             2.0f, 50.0f);
                break;
            default:
                break;
            }
        }

        // The time that has passed since the previous frame.
        const Uint64 ticks   = SDL_GetTicksNS();
        const float  seconds = static_cast<float>(ticks - lastTicks) * 1e-9f;
        lastTicks            = ticks;

        physics.update(world, seconds);

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        if (width > 0 && height > 0) {
            const float aspect = static_cast<float>(width) / static_cast<float>(height);
            renderer.draw(world, camera.viewProjection(aspect), width, height);
        }

        SDL_GL_SwapWindow(window);
    }
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
        run(window, renderer);
        renderer.shutdown();
    }

    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return rendererReady ? 0 : 1;
}
