# LegacyEngine

Trying to make a custom game engine with AI

The screenshot below shows the older scene, before the larger checkerboard floor and editor UI.

![An orange 1 m cube floating above a 5 × 5 m plane that lies on a 7 × 7 grid](docs/screenshot.png)

## What it does so far

LegacyEngine opens a maximized, resizable window onto a small 3D world:

- a 32 × 32 m grid on the ground, 1 m per grid block
- a solid 16 × 16 m plane lying on the grid, centred on the origin
- a black/white checkerboard floor with 1 m squares and a glossy, view-dependent specular highlight
- an ordinary 1 m cube starting at (0, 1, 0), which falls under gravity

Choose **Cube**, **Sphere**, **Capsule** or **Cone** in the
Dear ImGui **Spawn shapes** list, then left-click the scene to spawn that shape. The selection
stays active for subsequent clicks. Every spawned object has gravity and a matching native
ReactPhysics3D collider: box, sphere, capsule or convex cone (not a bounding-box approximation).

Cubes have 1 m edges, spheres have a 0.5 m radius, and capsules have a 0.5 m radius plus a
1 m cylindrical section (2 m total height). Cones have a 0.5 m base radius and 1 m height,
with matching 32-sided render geometry and convex collision hulls. UI interactions do not spawn objects or move the camera.

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
**Q/E** to move down/up and **right-drag** to look around in place. Holding Ctrl, Shift, Alt, or Super/Command blocks camera navigation.
The wheel moves forward/back. Static mode ignores all navigation input; spawning and selection
still work. Switching modes preserves your view.

### Editor layout and Inspector

The editor uses Dear ImGui docking. **Hierarchy**, **Scene**, **Inspector**,
**Assets**, **Lua Editor**, and **Lua Console** can be rearranged by dragging
their tabs/title bars, docked beside or tabbed with other panels, resized, floated
inside the application, or closed with their X button. Restore panels from
**Windows**, or choose **Windows > Reset layout** for the default arrangement.
Layout positions are remembered in `legacy-layout.ini` in the working directory.
Floating panels remain inside the engine's main OS window (not separate OS windows).

The Scene renders into its own texture, so docking and floating panels do not
cover or corrupt the 3D framebuffer. Camera input, picking, and gizmos follow
the Scene content rectangle, including high-DPI displays.

Select an object to inspect its existing components:

- **Name / Parent:** rename the object and change its parent.
- **Transform:** edit world position, rotation in degrees and scale. Round primitive colliders use
  uniform scale to keep rendering and collision geometry aligned.
- **MeshRenderer:** choose a primitive mesh, color, checkerboard material and specular strength.
  Choosing a primitive also updates an existing collider to the matching shape.
- **Collider:** edit shape, local center, size/radius/height, friction and bounciness independently.
- **RigidBody:** edit mass, gravity and linear/angular velocity.

**Add Component** adds missing Transform, MeshRenderer, Collider or RigidBody components. Optional
render/collision/physics components can be removed from their sections; Transform and the identity
fields remain available for scene editing. Inspector writes update live components and are picked
up by physics on the next update. Dynamic bodies continue simulating while you inspect them.

### Create menu and parenting

The top menu contains **Create → Empty GameObject** and **Create → Shape → Capsule / Cone / Cube /
Sphere**. Menu creation places the object near the camera target, selects it, and switches off
Spawn Shapes Mode so it can immediately be edited. Empty objects have a name and Transform but
no renderer or collider. Shape objects have matching render/collision geometry and a RigidBody.
Cones are also available in the click-to-spawn shape picker and Lua mesh/collider fields.

Select an object in **Hierarchy** and use the **Inspector → Parent** dropdown, or drag its row onto another
object's row. **None** unparents it. Children appear indented under parents. Parenting and
unparenting preserve the current world pose; subsequent parent translation, rotation and scale
move descendants. Empty objects are useful as editable group pivots. Self-parenting, cycles and
chains deeper than 128 levels are rejected. Removing a parent detaches children rather than
removing them, retaining their last world pose.

Lua supports `setParent(child, parent)`, `setParent(child, nil)`, `getParent(child)` and the live
`object.parent` property. For example:

```lua
local group = createGameObject("Group")
local cube = createGameObject("Child")
addComponent(cube, "MeshRenderer")
addComponent(cube, "Collider")
getComponent(cube, "Transform").position.x = 2
setParent(cube, group)
getComponent(group, "Transform").position.y = 3
```

Transforms exposed to Lua and gizmos remain **world-space**. Parenting propagates transform
deltas; it is not a rigid physics joint. Children with RigidBodies still simulate independently.
Use uniform parent scaling for rotated or round descendants: the TRS/component representation
does not represent shear or ellipsoidal colliders.

### Hierarchy

The **Hierarchy** panel lists the grid, plane, original cube and all spawned shapes by name.
Turn off **Spawn Shapes Mode**, then click a row to select it. List selection and scene selection
share the same highlight and transform tools; **Clear selection** clears both. Spawned objects
appear automatically and destroyed objects disappear. The grid can be selected in the list but is
a visual helper with no collider outline, but its Transform can still be edited.

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
| Spawn Shapes Mode checkbox | Checked: spawn; unchecked: select objects |
| Shape list (spawn mode) | Select Cube, Sphere or Capsule |
| Left click in the scene | Spawn the chosen shape, or select an object in selection mode |
| Freecam checkbox | Checked: fly camera; unchecked: lock current view |
| Right drag (freecam) | Look around in place |
| WASD, Q/E (freecam) | Fly forward/back/sideways, down/up |
| Ctrl / Shift / Alt / Super held | Block camera navigation |
| Mouse wheel (freecam) | Move forward/back |
| Closing the window | Quit (Escape does not exit the engine) |

Clicks hit the nearest solid surface, or the ground at y = 0 when nothing solid is under the
pointer (up to 100 m away). A sky click places an object at the camera target's distance.
Objects are offset to touch the surface and moved nearby if space is blocked.
The room search uses a conservative bounding box, so round objects may be placed slightly
further away in tight gaps. Colours cycle
through blue, green, yellow, red, purple and turquoise.

Only the plane is solid ground: objects outside it fall, but are **not automatically deleted**.
They remain in Hierarchy and can be repositioned in Inspector. Physics may suspend bodies whose
coordinates exceed its supported range (about one million metres); correcting the position lets
them rejoin the simulation. The initial cube has no special behavior and `F` has no engine action.
Tall stacks may wobble or collapse.

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
in red. Execution banners and completion messages are omitted. Both windows occupy fixed bottom panels in the editor layout.

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
with long messages truncated at about 8 KiB. Lua source is saved with the project when you use File > Save Project.

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
is `"Cube"`, `"Sphere"`, `"Capsule"` or `"Cone"`; Collider `shape` is `"Box"`, `"Sphere"`, `"Capsule"` or `"Cone"`.
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
Set `Collider::shape` to `ColliderShape::Box`, `Sphere`, `Capsule` or `Cone`. Boxes use `size`;
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
[releases page](https://github.com/monsef64/LegacyEngine/releases). New build archives use a UTC timestamp, for example:

```sh
tar -xzf LegacyEngine-20261112-143025-linux-x86_64.tar.gz
./LegacyEngine-20261112-143025-linux-x86_64/LegacyEngine
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

### Editor appearance

The editor uses a slate-dark ImGui theme with blue interaction accents, rounded
input controls, and flat panel borders. Bundled Roboto Regular at 16 px replaces
the small bitmap default; oversampled glyphs and anti-aliased UI geometry improve
legibility. The font is embedded in the executable (no external font installation
required). Its Apache 2.0 license is in `third_party/roboto/LICENSE`.

### Play mode and Hierarchy actions

The editor starts in **Edit** mode: physics and Lua lifecycle updates do not run.
Select **Play** to snapshot the scene, execute the Lua Editor source in a fresh
runtime, and start physics and script updates. **Pause** freezes simulation and
script callbacks; **Resume** continues without taking a new snapshot. **Stop**
restores the pre-Play entities, components, names, parenting, transforms, and
velocities, removes runtime-created objects, and clears the Lua runtime and
physics solver state. Changes made in the Inspector or Hierarchy during Play
are temporary. Lua Execute/Reset controls are enabled only while playing;
source text remains editable and is not discarded on Stop.

Right-click a GameObject in the Hierarchy to select it and open its actions:
**Duplicate** copies its standard components (not its children), **Unparent**
keeps its world-space pose, and **Delete** removes the object while keeping its
children detached at their current world-space poses. Selection mode is enabled
by default; turn off Spawn Mode to use Hierarchy actions if spawning is active.

### Component access from Lua GameObjects

Component type tokens avoid string names; both dot and colon calls work:

```lua
local gameObject = createGameObject("Player")
gameObject.getComponent(Transform).position.y = 3
gameObject:addComponent(RigidBody).mass = 2
gameObject.getComponent(ObjectName).value = "Hero"
```

Available tokens: `Transform`, `RigidBody`, `Collider` (`BoxCollider` alias),
`MeshRenderer`, and `ObjectName`. `getComponent` returns `nil` when the object
lacks that component. Fields depend on the component: `.value` belongs to
`ObjectName`, while `Transform` exposes `.position`, `.rotation`, and `.scale`.
The existing `getComponent(gameObject, Transform)` global function and string
forms such as `gameObject.getComponent("Transform")` remain supported. Tokens
also work with add/remove and `findGameObjectsByComponent`.

### Startup smoke test

`LegacyEngine --smoke-test` renders 120 frames and exits through normal engine
shutdown. It returns nonzero if the window closes before those frames complete.
CI runs it under Xvfb with software OpenGL and a 60-second watchdog; a timeout,
crash, or forced kill fails the check rather than being accepted as success.

### Undo and redo

In Edit mode, use **Edit > Undo / Redo** in the menu bar, **Ctrl+Z** to undo, or
**Ctrl+Y / Ctrl+Shift+Z** to redo. History covers scene creation/deletion,
duplication, reparenting, Inspector component/property edits, and gizmo changes.
Continuous drags and active text-field edits are grouped until the gesture ends.
Text fields retain their own text undo shortcuts while typing. Up to 64 scene
undo steps are retained; new edits discard the redo branch. Camera navigation,
selection-only changes, and Lua source text are not scene undo steps. History is
disabled during Play/Pause and preserved when Stop restores the edit scene.

Playback controls are icon-only and centered in the top toolbar, with tooltips
for Play, Pause/Resume, and Stop. On narrower windows, secondary Scene controls
are available through **Scene options** to avoid overlapping playback buttons.

New release and workflow build packages are named
`LegacyEngine-YYYYMMDD-HHMMSS-linux-x86_64.tar.gz` using packaging time in UTC.

### Editor shortcuts

| Shortcut | Action |
| --- | --- |
| Ctrl+Z | Undo scene edit |
| Ctrl+Y / Ctrl+Shift+Z | Redo scene edit |
| F5 | Play (from Edit mode) |
| F6 | Pause / Resume |
| F7 | Stop and restore the edit scene |
| Ctrl+D | Duplicate selected GameObject |
| Ctrl+C | Copy selected GameObject |
| Ctrl+V | Paste copied GameObject |
| Ctrl+P | Unparent if parented; otherwise open a parent picker |

Undo/Redo buttons have been removed from the toolbar; these commands live in
**Edit**, along with Duplicate, Copy, Paste, and Parent / Unparent. Scene editing
commands are disabled during Play/Pause and gizmo drags. While typing, text
widgets keep their normal editing shortcuts instead of triggering scene actions.

Copy stores an internal snapshot of the object's standard components, not its
children. Paste creates a detached object at the copied world-space pose even if
the original has since changed or been deleted; Duplicate retains the original's
parent. These scene changes participate in undo/redo. The parent picker excludes
invalid targets to prevent cycles and preserves the object's world-space pose.

### Saving and opening projects

Use **File > Save Project** (**Ctrl+S**), **Save Project As...**
(**Ctrl+Shift+S**), or **Open Project...** (**Ctrl+O**). Open and Save As use
the system file picker, with a `.legacy` project filter. The first save also
opens the picker; later Ctrl+S saves update the same file. If no extension is
chosen when saving, `.legacy` is added. Cancel leaves the current project
unchanged. Escape never exits the engine.

The window title shows the project filename (or **Untitled**) with a trailing
`*` while unsaved, for example `MyScene.legacy* - LegacyEngine`. Successful Save
or Open clears the marker; scene, Lua source, and camera changes set it again.
Undoing back to the saved state clears it. Play-mode simulation is not counted
as an edit to the saved scene. There is no persistent "Project saved" status
message in the menu bar.

A project is a versioned text file with an accompanying `Assets/` directory, containing all scene GameObjects,
names, transforms, built-in mesh/material properties, colliders, rigid bodies,
parent relationships, the Lua Editor source, and camera view. Built-in meshes
are stored by stable catalog index rather than graphics-driver handles. Runtime
Lua state, console output, clipboard, and undo history are not stored. Imported
custom meshes and custom C++ component types are not supported by this format.

Save/Open are available only in Edit mode. Opening replaces the current scene,
resets physics and the Lua runtime, and clears scene undo history; save existing
work first. Invalid or unsupported files leave the current project intact.
Saves are validated and written to a temporary sibling file before replacing
the destination, so serialization/write failures do not truncate an existing
project. Errors are shown in a system message box. Projects are limited to
10,000 objects, 16 MiB total, and 64 KiB of Lua source.

Duplicate and Paste use numbered names: `GameObject` becomes `GameObject (2)`,
and copying `GameObject (2)` produces `GameObject (3)`. Existing scene names are
skipped so repeated duplicates/pastes do not create naming collisions.

On Linux, native file selection uses SDL's desktop portal or a supported system
dialog backend (for example Zenity). A desktop session with a working picker
backend is required; unavailable pickers report an error instead of falling
back to the old typed-path dialog.

### Assets and attached Lua scripts

The **Assets** panel manages the `Assets/` directory next to the open project
(or in the working directory for an untitled scene). Use **Import...**, or drop
individual files from your system file explorer onto the engine to copy them
into Assets. Existing filenames are preserved with numbered collision suffixes.
The browser displays file-type badges for Lua, images, models, audio, projects,
text, and other files. Right-click assets to rename or delete them; renaming
updates matching Lua Script component references in the current scene. Deletion
moves files to the system Trash without a confirmation dialog and can be undone
or redone through Edit / Ctrl+Z / Ctrl+Y. Deleted scripts may leave missing
references until restored. Import, rename, and script-file writes are not part
of scene Undo/Redo. Import is limited to individual files up to 256 MiB;
folder import and media/model rendering or thumbnails are not implemented.

Choose **New Lua** to create a template. Double-click a Lua asset to edit it in
the Lua Editor, then use **Save Lua asset** in Assets to write edits to disk.
Project Save stores the editor source but does not implicitly overwrite a Lua
asset; save the asset before playing to run its latest disk contents.

Drag a Lua asset onto a Hierarchy GameObject, the Inspector's script drop area,
or the Scene with a selected GameObject to attach it. You can also use
**Add Component > Lua Script**, enter a relative asset path, and toggle Enabled.
There is currently one Lua Script component per GameObject. Attaching another
replaces its path. Script components participate in duplicate, copy/paste,
scene Undo/Redo, project Save/Open, and Play/Stop scene restoration.

Each enabled component runs in an isolated, budgeted Lua state. `self` is the
owning GameObject; the usual lifecycle functions execute on Play:

```lua
function onStart()
    print("Started", self.name)
end
function onUpdate(dt)
    local transform = self.getComponent(Transform)
    transform.position.x = transform.position.x + dt
end
function onFixedUpdate(dt)
    -- Physics-step logic
end
function onDestroy()
    print("Stopped")
end
```

Pause freezes callbacks, Stop runs teardown and restores the edit scene. Script
errors and output appear in Lua Console with the asset path. Missing scripts
report errors without crashing the editor; at most 128 component runtimes are
active. The existing Lua Editor Execute/runtime remains separate. Components
are accessible through `self.getComponent(LuaScript).path` and `.enabled`.

Project format version 2 stores script references; version 1 projects still
load. Save As copies the current asset tree into the destination's `Assets/`
directory and refuses conflicting files instead of replacing them silently.
Distribute the `.legacy` file together with its `Assets/` directory.

### Asset views and focused keyboard actions

Switch between **List** and **Grid** in the Assets panel. Grid tiles use the same
selection, Lua editing, context menu, and drag/drop behavior as list rows.
While Assets is focused, **F2** opens Rename for the selected asset (Enter applies,
Escape cancels), and **Delete** moves it to the system Trash immediately. These
shortcuts do not fire while typing, using another panel, or running Play mode.

Asset deletion shares the same chronological, 64-step Undo/Redo history as scene
edits. Undo restores the file to its original location; Redo trashes it again.
The Linux build uses freedesktop Trash files and `.trashinfo` metadata, so files
also appear in the desktop Trash. If the desktop has emptied the Trash, or a new
file occupies the original path, Undo reports an error and does not overwrite
anything or advance history. Abandoning/trimming history leaves files in Trash,
not permanently deleted. History is session-only; opening a project or saving
into a different Assets root clears it. Platforms without an implemented Trash
backend report an error instead of permanently deleting an asset.

With **Hierarchy** or **Scene** focused, **Delete** removes the selected GameObject
and records the change in scene history. Its children remain detached at their
current world-space poses, as with the Hierarchy context-menu action. Asset
focus never deletes a GameObject, and Hierarchy/Scene focus never trashes assets.

### Scene Cameras and Game view

Use **Create > Camera** to add a Camera GameObject. It has a selectable 3D body,
lens, and viewfinder model in **Scene**, but that editor-only model is never
drawn in **Game**. Cameras are not player characters and have no collider or
rigid body unless you explicitly add one. The existing Scene freecam remains
independent of the active game camera. Use **Windows > Game** to restore the
Game panel if closed. Its layout is dockable just like other panels.

The first created camera is active automatically. Select it to edit projection,
FOV, clip planes, orthographic size, viewport, and active state in Inspector.
Camera settings participate in Undo/Redo, duplication, and Play/Stop restoration.
Duplicates start inactive. Project format 3 saves camera settings and remaps
camera target references; format 1/2 projects remain readable. Shake and current
Game-view aspect are transient and not saved.

Lua camera functions:

```lua
local camera = createCamera("Main Camera")
setCameraPosition(camera, {x=0, y=3, z=8})
lookAt(camera, {x=0, y=1, z=0})
setFieldOfView(camera, 60)
setNearClipPlane(camera, 0.1)
setFarClipPlane(camera, 1000)
setProjectionMode(camera, "Perspective") -- or "Orthographic"
setViewport(camera, 0, 0, 1, 1)
setActiveCamera(camera)
assert(getActiveCamera() == camera)
```

- `setCameraRotation(camera, rotation)` accepts Euler degrees `{x,y,z}` (yaw Y,
  pitch X, roll Z), or a normalized quaternion `{x,y,z,w}`. Local forward is -Z,
  up is +Y. Explicit rotation clears target tracking.
- `lookAt(camera, target)` aims once. Target can be a world position or GameObject.
- `setCameraTarget(camera, target)` continuously aims without moving the camera.
- `setFollowTarget(camera, target, offset)` continuously positions at target +
  world-space offset and aims at it. Target can be a point or GameObject; `nil`
  clears tracking/following. Destroyed entity targets detach safely.
- `setCameraShake(camera, intensity, duration)` adds a temporary render-only
  shake in world units for the given seconds. It never permanently alters the
  Transform. Pause freezes shake, and Stop restores the edit scene.
- `setActiveCamera(nil)` disables the game camera. If none is active, Game shows
  a placeholder rather than silently using the editor freecam.
- `camera.getComponent(Camera)` exposes `active`, `orthographic`, `fieldOfView`,
  `nearClip`, `farClip`, and `orthographicSize`. Use the setter functions for
  validated projection changes. FOV must be between 0 and 179 degrees, near clip
  positive, far clip greater than near clip, and viewport contained within 0..1.

`setViewport(camera,x,y,width,height)` uses normalized Game-view coordinates
with a top-left origin. `worldToScreen(camera,worldPosition)` returns normalized
full-Game-view `{x,y,z}` with depth 0 at near clip and 1 at far clip.
`screenToWorld(camera,screenPosition)` reverses it; supply all three coordinates,
including depth. Values outside the view/depth range represent off-screen points.
Conversions use the current Game-view aspect (last known aspect when hidden,
or 16:9 before first display). Orthographic size is the vertical world-space
span, adjustable through Inspector or `getComponent(Camera).orthographicSize`.
