# LegacyEngine

Trying to make a custom game engine with AI

The screenshot below shows the older scene, before the larger checkerboard floor and editor UI.

![An orange 1 m cube floating above a 5 × 5 m plane that lies on a 7 × 7 grid](docs/screenshot.png)

## What it does so far

LegacyEngine opens a maximized, resizable window onto a small 3D world:

- a 32 × 32 m grid on the ground, 1 m per grid block
- a solid 16 × 16 m plane lying on the grid, centred on the origin
- a black/white checkerboard floor with 1 m squares and a glossy, view-dependent specular highlight
- a 1 m cube floating above the plane, with its centre at (0, 1, 0)

Press `F` and the cube falls onto the plane. Choose **Cube**, **Sphere** or **Capsule** in the
Dear ImGui **Spawn shapes** list, then left-click the scene to spawn that shape. The selection
stays active for subsequent clicks. Every spawned object has gravity and a matching native
ReactPhysics3D collider: box, sphere or capsule (not a bounding-box approximation).

Cubes have 1 m edges, spheres have a 0.5 m radius, and capsules have a 0.5 m radius plus a
1 m cylindrical section (2 m total height). UI interactions do not spawn objects or move the camera.

The **Spawn Shapes Mode** checkbox starts checked. Uncheck it to select instead of spawn:
left-click any solid object (including the original cube or plane) to see its actual collider
in **green lines**. Box edges, sphere curves and capsule curves follow the selected object's
position and rotation as it moves. The overlay is visible through surfaces, including the
plane's hidden collider thickness. Click empty space to clear selection. Checking spawn mode
clears the outline and disables scene selection; unchecking it prevents spawning.
The shape list is disabled while selecting, and your last spawn shape is remembered.
Green outlines are deliberately sparse: 12 box edges, three great circles for a sphere,
and two end rings with four curved meridians for a capsule.

The **Freecam** checkbox switches between free flight (checked by default) and a static camera
(unchecked, locks the current view). In freecam, use **WASD** to fly forward/back/sideways,
**Q/E** to move down/up, **Shift** for faster movement and **right-drag** to look around in place.
The wheel moves forward/back. Static mode ignores all navigation input; spawning and selection
still work. Switching modes preserves your view.

### Level Objects

The **Level Objects** window lists the grid, plane, original cube and all spawned shapes by name.
Turn off **Spawn Shapes Mode**, then click a row to select it. List selection and scene selection
share the same highlight and transform tools; **Clear selection** clears both. Spawned objects
appear automatically and destroyed objects disappear. The grid can be selected in the list but is
a visual helper with no collider outline or transform gizmo.

### Transform tools

The bottom-left toolbar has three **icon-only buttons** with hover tooltips: **Move** (arrows),
**Rotate** (circular arrow), and **Scale** (expanding square). The active tool is highlighted.
Turn off **Spawn Shapes Mode**, select an object, then drag its gizmo handles:

- **Move:** drag the red X, green Y or blue Z arrow (world axes), or a plane handle.
- **Rotate:** drag an axis ring to turn the object around that local axis.
- **Scale:** drag a local-axis handle to resize a box, or the centre to resize uniformly.
  Spheres and capsules scale uniformly with any scale handle so their native colliders still
  match the graphics. Scale is limited to 0.05–100 to avoid degenerate physics shapes.

During a drag, the selected dynamic object is held in place; on release, its mass/gravity settings
are restored with zero velocity. The rest of the scene keeps simulating. Objects can be moved into
other objects; normal collision resolution resumes after release. Tool handles capture the mouse,
so manipulating an axis does not spawn, deselect, or move the camera. Tools are hidden when nothing
is selected or spawn mode is on; the bottom-left toolbar remains available.

One unit is one metre, +Y points up and the ground is the XZ plane.

| Input | Action |
| --- | --- |
| `F` | Drop the cube. Pressing it again puts the cube back and drops it once more |
| Spawn Shapes Mode checkbox | Checked: spawn; unchecked: select objects |
| Shape list (spawn mode) | Select Cube, Sphere or Capsule |
| Left click in the scene | Spawn the chosen shape, or select an object in selection mode |
| Freecam checkbox | Checked: fly camera; unchecked: lock current view |
| Right drag (freecam) | Look around in place |
| WASD, Q/E (freecam) | Fly forward/back/sideways, down/up |
| Shift (freecam) | Move faster |
| Mouse wheel (freecam) | Move forward/back |
| `Esc`, or closing the window | Quit |

Clicks hit the nearest solid surface, or the ground at y = 0 when nothing solid is under the
pointer (up to 100 m away). A sky click places an object at the camera target's distance.
Objects are offset to touch the surface and moved nearby if space is blocked.
The room search uses a conservative bounding box, so round objects may be placed slightly
further away in tight gaps. Colours cycle
through blue, green, yellow, red, purple and turquoise.

Only the plane is solid ground: spawned objects outside it fall and are removed below y = −200 m.
The original orange cube stays fixed until `F`; if it later falls out of the scene, it is
kept there without a `RigidBody` so `F` can bring it back. Tall stacks may wobble or collapse.

It is written in C++17 with:

- [SDL3](https://www.libsdl.org/) for the window, the input and the OpenGL context
- OpenGL 3.3 (core profile) for the rendering
- [GLAD](https://github.com/Dav1dde/glad) for loading the OpenGL functions (generated code in `third_party/glad`)
- [Dear ImGui](https://github.com/ocornut/imgui) for the editor UI (MIT licence)
- [Lua](https://github.com/lua/lua) for the embedded snippet runtime (MIT licence)
- [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo) for draggable transform axes (MIT licence)
- [ReactPhysics3D](https://www.reactphysics3d.com/) for the physics: gravity and collisions

Only Linux has been built and tested so far.

The checkerboard texture is generated at startup and mipmapped; no image asset is needed.
It follows the plane when transformed. Its collider spans the full 16 × 16 m surface.

## Lua editor and console

The **Lua Editor** opens on the right with an example snippet. Edit the multiline text (Tab inserts
indentation), then click **Execute**. `print(...)` output appears in the **Lua Console**, with arguments
separated by tabs. Printed messages use the format `lua > output`. Syntax errors and runtime tracebacks appear
in red. Execution banners and completion messages are omitted. Both windows can be moved, resized and collapsed.

```lua
counter = (counter or 0) + 1
print("Execution", counter)
for i = 1, 5 do
    print(i, math.sqrt(i))
end
```

Variables persist between executions. **Reset Runtime** clears Lua variables and restores the
built-in functions; it does not erase the editor text or existing console messages. **Clear** only
clears the console. The console has an auto-scroll toggle and keeps the last 1,000 messages,
with long messages truncated at about 8 KiB. Scripts are held in memory, not saved to disk.

Lua 5.4.8 is embedded, so no Lua installation is needed. Scripts can manipulate the live scene
using the game-object and component API below. Available libraries are base,
`math`, `string`, `table` and `utf8`. File/process access, native modules, debug APIs, coroutines,
metatable access, dynamic loading and script-side `pcall`/`xpcall` are omitted. Execution is synchronous
with a 1,000,000 VM-instruction budget, a 32 MiB Lua allocation limit and a 64 KiB source limit.
The instruction guard covers Lua loops, not time spent inside native library calls; these limits
are guardrails for local snippets, **not a security boundary for untrusted code**.

### Lua game and object lifecycle

Define global `onStart()`, `onUpdate(deltaTime)`, `onFixedUpdate(deltaTime)` and `onDestroy()`
functions in the editor. `onStart()` runs after **each successful Execute**. Frame callbacks start
on the next frame; `onUpdate` receives seconds capped at 0.25, and `onFixedUpdate` receives 1/60
seconds immediately before each actual physics step (zero or multiple times per frame).
`onDestroy()` runs on Reset Runtime or engine shutdown. Callback errors go to the Lua console
and pause lifecycle updates until the next successful Execute or Reset Runtime.

The same callbacks can be assigned to individual GameObject handles, e.g.
`ball.onUpdate = function(dt) ... end`. Object `onStart` runs once before that object's first
update/fixed update. Callbacks receive only the documented arguments, **not an implicit self**;
close over the object handle when needed. Objects created during a callback start on a subsequent
tick. Explicit destruction calls the object's `onDestroy` before removing components. Destruction
by the engine is detected on the next tick, when the handle is already invalid; such a callback
should not try to read the destroyed object's components. Each object destruction callback runs
at most once. Reset/shutdown destroys objects created by Lua, but preserves pre-existing objects.

| Lua function | Result |
| --- | --- |
| `createGameObject(name)` | New object with Transform and ObjectName, initially invisible |
| `destroyGameObject(object)` | Calls object onDestroy, then removes it from the scene |
| `getComponent(object, type)` | Live component proxy, or nil if absent |
| `addComponent(object, type)` | Existing component or newly added default component |
| `removeComponent(object, type)` | Removes the named component; harmless if absent |
| `findGameObjectByName(name)` | First matching named object, or nil |
| `findGameObjectsByComponent(type)` | Array of matching objects, including pre-existing scene objects |

Types are strings: `Transform`, `RigidBody`, `Collider` (`BoxCollider` alias), `MeshRenderer`, and
`ObjectName`. Vector properties support both whole assignments (`position = {x=0,y=3,z=0}`) and
live coordinate edits (`position.y = 4`). Rotation is a quaternion `{x,y,z,w}`. MeshRenderer `mesh`
is `"Cube"`, `"Sphere"` or `"Capsule"`; Collider `shape` is `"Box"`, `"Sphere"` or `"Capsule"`.
Set both to matching shapes. Other fields match the C++ component names below. Names are limited
to 256 bytes, and the runtime tracks at most 4,096 simultaneous object handles. Stale handles and
removed component accesses report Lua errors instead of accessing recycled ECS storage.

See [`scripts/lifecycle_example.lua`](scripts/lifecycle_example.lua) for a runnable example.
Use uniform scale for spheres/capsules, as with C++ component edits. Reset clears callbacks and
globals; Execute otherwise retains existing objects/globals and re-runs the current onStart.

### Autocomplete definitions

The multiline editor shows API/field signatures while typing. **Tab** completes an unambiguous
identifier (or extends a shared prefix); type more characters to disambiguate. Tab with an empty
prefix inserts indentation. This is built-in API completion, not a full Lua language server.

[`scripts/definitions/legacyengine.lua`](scripts/definitions/legacyengine.lua) supplies LuaLS/EmmyLua
types, lifecycle signatures and component overloads. The repository's `.luarc.json` includes that
folder for external editors. Definitions are editor-only metadata; do **not** Execute them.

## Adding objects

The scene is put together in `run()` in [`src/main.cpp`](src/main.cpp). Every object in it is an
*entity*: an id with *components* attached to it, and the components decide what the object is.

| Component | What it gives the entity |
| --- | --- |
| `Transform` | A position, a rotation and a scale |
| `MeshRenderer` | A mesh and a colour: the entity is drawn |
| `Collider` (`BoxCollider` is a backward-compatible alias) | A solid box, sphere or capsule |
| `RigidBody` | Physics: the entity falls, and collisions push it around |

For example, this adds a blue crate that falls onto the plane beside the cube:

```cpp
const Entity crate = world.create();
world.add(crate, Transform{{1.5f, 3.0f, 0.0f}});
world.add(crate, MeshRenderer{cubeMesh, {0.20f, 0.45f, 0.90f}});
world.add(crate, BoxCollider{});
world.add(crate, RigidBody{});
```

Without the `RigidBody`, the crate is an obstacle that stays where it was put. Without the
`BoxCollider` as well, it is only drawn.

Components are plain structs, and a change to one takes effect on the next frame:

```cpp
world.get<Transform>(crate).position = {0.0f, 4.0f, 0.0f}; // move it above the cube
world.get<MeshRenderer>(crate).color = {0.9f, 0.1f, 0.1f}; // paint it red
world.remove<RigidBody>(crate);                            // stop it where it is
world.destroy(crate);                                      // or take it out of the scene
```

Any struct of your own can be a component too, and `world.each()` visits every entity that has
the components it names. This makes the crate spin while it falls:

```cpp
struct Spin {
    float speed = 1.0f; // radians per second
    float angle = 0.0f;
};

// Where the scene is put together:
world.add(crate, Spin{2.0f});

// In the main loop, before physics.update():
world.each<Transform, Spin>([&](Entity, Transform& transform, Spin& spin) {
    spin.angle += spin.speed * seconds;
    transform.rotation = axisAngle({0.0f, 1.0f, 0.0f}, spin.angle);
});
```

A mesh is a list of triangles, and it can be shared by any number of entities. `makeBox()` and
`makePlane()` build the two shapes of the scene; any other shape is three vertices per triangle,
each with a position and a normal. This puts a red pyramid on the plane:

```cpp
MeshData pyramid;
const Vec3 tip = {0.0f, 1.0f, 0.0f};
const Vec3 corners[4] = {
    {-0.5f, 0.0f, 0.5f}, {0.5f, 0.0f, 0.5f}, {0.5f, 0.0f, -0.5f}, {-0.5f, 0.0f, -0.5f}};
for (int i = 0; i < 4; ++i) {
    // One side. Seen from outside, its vertices run counter-clockwise.
    const Vec3 a      = corners[i];
    const Vec3 b      = corners[(i + 1) % 4];
    const Vec3 normal = normalize(cross(b - a, tip - a)); // points away from the side
    pyramid.vertices.push_back({a, normal});
    pyramid.vertices.push_back({b, normal});
    pyramid.vertices.push_back({tip, normal});
}
const MeshHandle pyramidMesh = renderer.createMesh(pyramid);

const Entity roof = world.create();
world.add(roof, Transform{{-1.5f, 0.0f, 0.0f}});
world.add(roof, MeshRenderer{pyramidMesh, {0.80f, 0.20f, 0.20f}});
```

There is no loader for model files such as `.obj` yet; one would fill a `MeshData` in the same way.
Set `Collider::shape` to `ColliderShape::Box`, `Sphere` or `Capsule`. Boxes use `size`;
spheres use `radius`; capsules use `radius` and `height` (distance between hemisphere centres,
along local Y). All use `center`, `friction` and `bounciness`. Attach only one `Collider` per entity.
Use uniform scale for round meshes: native round colliders cannot become ellipsoids; non-uniform
scale uses the largest radial scale for the collider, and Y scale for capsule section height.

The code is in `src`:

| File | What is in it |
| --- | --- |
| `main.cpp` | The scene, the input and the main loop |
| `ecs.hpp` | `World`, which holds the entities and their components |
| `components.hpp` | The components above |
| `mesh.hpp` | What a mesh is made of, and the box, the plane and the grid |
| `renderer.hpp` | Drawing with OpenGL. `createMesh()` takes the triangles of any shape |
| `physics.hpp` | The ReactPhysics3D simulation, plus `raycast()` and `checkBox()` queries |
| `camera.hpp`, `math.hpp` | The orbit camera, `rayThrough()` for picking, rays, vectors, rotations and matrices |

## Download

Linux x86_64 binaries are published on the
[releases page](https://github.com/monsef64/LegacyEngine/releases). For example, for version `v0.1.0`:

```sh
tar -xzf LegacyEngine-v0.1.0-linux-x86_64.tar.gz
./LegacyEngine-v0.1.0-linux-x86_64/LegacyEngine
```

The binary is built on Ubuntu 24.04. It needs a graphics driver with OpenGL 3.3 and glibc 2.38 or
newer, so it runs on Ubuntu 24.04 and Debian 13, for example, but not on Ubuntu 22.04. SDL and
ReactPhysics3D are linked into it, so there is nothing else to install.

## Build

You need a C++17 compiler, CMake 3.16 or newer, Git, and the development packages that SDL builds
against. On Ubuntu and Debian:

```sh
sudo apt-get install build-essential cmake git pkg-config \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev \
  libxi-dev libxss-dev libxtst-dev \
  libwayland-dev libxkbcommon-dev libdecor-0-dev \
  libgl1-mesa-dev libegl1-mesa-dev libgles2-mesa-dev libdrm-dev libgbm-dev \
  libdbus-1-dev libibus-1.0-dev libudev-dev libusb-1.0-0-dev \
  libasound2-dev libpulse-dev libpipewire-0.3-dev libjack-dev libsndio-dev \
  libfribidi-dev libthai-dev liburing-dev
```

Then:

```sh
cmake -S . -B build
cmake --build build --parallel 4
./build/LegacyEngine
```

The first command downloads SDL 3.4.20, ReactPhysics3D 0.10.2, Dear ImGui 1.92.5 and Lua 5.4.8, and the second one builds them
into the executable. If SDL3 is already installed on your system, that one is used instead; add
`-DLEGACY_VENDORED_SDL=ON` to the first command to download it anyway.

## Release

Pushing a tag that starts with `v` runs the [release workflow](.github/workflows/release.yml). It
builds the Linux binary, checks that it starts, and publishes it as a GitHub release:

```sh
git tag v0.1.0
git push origin v0.1.0
```

The workflow can also be started by hand from the Actions tab. On a branch, that only builds the
binary and attaches it to the workflow run.

## Tests

After building, run the CPU-only regression tests (no display required):

```sh
ctest --test-dir build --output-on-failure
```

These cover mesh dimensions and winding, native collider raycasts and overlaps, capsule rotation,
resting heights, collider replacement/removal, and selection wireframe geometry. Transform tests
also exercise matrix-to-quaternion conversion, scale limits, icon toolbar clicks, actual move/rotate/scale
gizmo drags through ImGui input, and physics-body hold/release. A real graphical session is still needed
to verify the rendered editor and physical mouse interaction.

Lua tests cover console output, errors/tracebacks, persistent state, reset/clear, execution and
memory limits, bounded output, and recovery. CPU ImGui integration tests click Execute and Clear
to verify the editor-to-console flow without needing a display.
