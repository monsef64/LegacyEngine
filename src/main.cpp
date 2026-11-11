// LegacyEngine - a small 3D scene: an orange 1 m cube floating above a
// 5 x 5 m plane, on a 7 x 7 m grid. Pressing F drops the cube onto the plane,
// and a click puts a new cube under the mouse pointer.
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
#include <cstddef>
#include <iterator>

// ---------------------------------------------------------------------------
// Scene description
// ---------------------------------------------------------------------------

constexpr int   kGridCells = 7;    // grid blocks per side
constexpr float kCellSize  = 1.0f; // metres per grid block
constexpr float kPlaneSize = 5.0f; // metres per side of the plane
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

// A cube that has fallen this far is out of the scene: nothing below the
// ground will ever stop it, and it is too far away to be drawn.
constexpr float kLowestY = -200.0f;

// Lets the first cube fall from where it started. The RigidBody is what makes
// the physics move it, and a new one is at rest.
static void dropCube(World& world, Entity cube)
{
    Transform& transform = world.get<Transform>(cube);
    transform.position   = kCubeStart;
    transform.rotation   = {};
    world.add(cube, RigidBody{});
}

// ---------------------------------------------------------------------------
// The cubes that clicks create
// ---------------------------------------------------------------------------

// The ray from the camera through a mouse position. Returns false while the
// window has no size (it is minimised, say).
static bool mouseRay(SDL_Window* window, const OrbitCamera& camera, float x, float y, Ray& ray)
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
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    ray = camera.rayThrough(x / static_cast<float>(windowWidth),
                            y / static_cast<float>(windowHeight), aspect);
    return true;
}

// The place nearest to `center` where a cube has room: `center` itself if
// nothing is in the way there. If something is, the nearest place where
// nothing is: beside `center`, no further away than a cube is wide, or above
// it.
static Vec3 findRoom(const Physics& physics, Vec3 center)
{
    // Things that rest on each other touch, and reach into each other by up to
    // a centimetre. Neither is in the way, so room is looked for with a cube
    // that is that much smaller on every side.
    const float inner = kCubeSize - 2.0f * 0.01f;
    const Vec3  size  = {inner, inner, inner};
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
static Vec3 placeForCube(const Physics& physics, const OrbitCamera& camera, const Ray& ray)
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
    const float toSurface = (std::abs(normal.x) + std::abs(normal.y) + std::abs(normal.z)) * half;
    Vec3        center    = ray.origin + ray.direction * distance + normal * toSurface;
    center.y              = std::max(center.y, half); // on the ground, not in it

    return findRoom(physics, center);
}

// Adds a cube that falls and that collisions push around.
static void spawnCube(World& world, MeshHandle mesh, Vec3 center, Vec3 color)
{
    const Entity cube = world.create();
    world.add(cube, Transform{center});
    world.add(cube, MeshRenderer{mesh, color});
    world.add(cube, BoxCollider{{kCubeSize, kCubeSize, kCubeSize}});
    world.add(cube, RigidBody{});
}

// Removes the cubes that fell off the plane and out of the scene. The first
// cube is kept, because F brings it back. Until then it stays where it is:
// without its RigidBody it is an obstacle again.
static void removeFallenCubes(World& world, Entity firstCube)
{
    world.each<Transform, RigidBody>([&](Entity entity, Transform& transform, RigidBody&) {
        if (transform.position.y >= kLowestY) {
            return;
        }
        if (entity == firstCube) {
            world.remove<RigidBody>(entity);
        } else {
            world.destroy(entity);
        }
    });
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

    // The first cube is drawn and is solid. Without a RigidBody it is an
    // obstacle, which nothing moves: it floats until F is pressed.
    const Entity cube = world.create();
    world.add(cube, Transform{kCubeStart});
    world.add(cube, MeshRenderer{cubeMesh, {0.90f, 0.47f, 0.13f}});
    world.add(cube, BoxCollider{{kCubeSize, kCubeSize, kCubeSize}});

    OrbitCamera camera;
    Uint64 lastTicks = SDL_GetTicksNS();
    bool   running   = true;

    // The left mouse button does two things. Pressed and released in one
    // place, it is a click, which creates a cube. Once the pointer has moved
    // away from where the button went down, it is a drag, which orbits.
    bool        clicking   = false; // the button is down and this may still be a click
    float       pressX     = 0.0f;  // where the button went down
    float       pressY     = 0.0f;
    std::size_t clickCubes  = 0;     // how many cubes clicks have created

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
                // Drag with the left button to orbit, as if grabbing the world.
                if ((event.motion.state & SDL_BUTTON_LMASK) && !clicking) {
                    camera.yaw -= event.motion.xrel * 0.005f;
                    camera.pitch = std::clamp(camera.pitch + event.motion.yrel * 0.005f,
                                              radians(2.0f), radians(89.0f));
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (event.button.button == SDL_BUTTON_LEFT && clicking) {
                    clicking = false;

                    // A click: put a cube on the spot under the pointer.
                    Ray ray;
                    if (mouseRay(window, camera, event.button.x, event.button.y, ray)) {
                        spawnCube(world, cubeMesh, placeForCube(physics, camera, ray),
                                  kClickColors[clickCubes % std::size(kClickColors)]);
                        ++clickCubes;
                    }
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

        removeFallenCubes(world, cube);

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
