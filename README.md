# LegacyEngine

A C++17 3D editor built with SDL3, OpenGL, Dear ImGui, ReactPhysics3D, miniaudio and Lua 5.4.
LegacyScript (`.ls`) adds Inspector-exported fields and engine APIs on top of Lua.

![LegacyEngine editor: Scene viewport, Hierarchy, Inspector, Assets and scripting panels](docs/screenshot.png)

## Current features

- Dockable Scene, Game, Hierarchy, Inspector, Assets, Lua Editor and Console panels.
- Empty GameObjects, cube/sphere/capsule/cone primitives, cameras and audio sources.
- Transform gizmos and labeled X/Y/Z fields; Inspector rotation is limited to ±360°.
- Physics colliders and rigid bodies, parenting, duplication, clipboard, Undo/Redo.
- Play, Pause/Resume and Stop with restoration of the edit-time scene.
- Hierarchy search, object icons and a read-only level root named after the project.
- Dynamic directional, point and spot lights with bounded shadow maps and PCF filtering.
- Camera projection, tracking/following, shake and screen/world conversion APIs.
- AudioSource and AudioListener, WAV/MP3 playback, looping, pitch, volume, pan,
  Spatial Blend, range attenuation, fades and one-shots.
- Project-owned Assets with folder browsing, file import/rename/trash, drag-and-drop
  script/reference assignment and listener-independent audio previews.
- Internal Lua editor with highlighting or external VS Code editing; Dark/Grey themes.
- Three-slot named keyboard/mouse/gamepad-button mappings, raw input APIs and cursor controls.
- Structured JSON projects, separate settings files and native Save/Open dialogs.

## Build and test

Requires a C++17 compiler, CMake 3.16+, Git and platform development libraries.
CMake fetches SDL3 (if not installed), ReactPhysics3D, Dear ImGui, ImGuizmo and Lua.
GLAD, Roboto/Font Awesome fonts, miniaudio, simdjson, nlohmann/json and stb_image
are included under `third_party/`.

On Debian/Ubuntu, install the window/audio/OpenGL development dependencies used
in [the engine workflow](.github/workflows/release.yml). Then:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
./build/LegacyEngine
```

Use `-DLEGACY_VENDORED_SDL=ON` to force a static vendored SDL build.
A graphical session and an OpenGL 3.3-capable driver are required at runtime.
Native file dialogs require a working desktop portal or another SDL dialog backend.
Linux CI builds on Ubuntu 24.04; check its run log for the resulting glibc requirement.

For headless graphical checks on Linux:

```sh
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a build/LegacyEngine --smoke-test
LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a -s '-screen 0 1600x900x24' \
  build/LegacyEngine --screenshot build/editor.bmp
```

Both modes finish after 120 rendered frames. `--screenshot` captures the actual
OpenGL/ImGui framebuffer as BMP and ignores the saved layout for a reproducible view.
CI converts this to PNG and uploads it as **editor-screenshot**. A manual run can
also enable **update_screenshot** to commit the captured image back to its selected
branch; ordinary builds never modify the repository.

## Source organization

| Directory | Responsibility |
| --- | --- |
| `src/app/` | Application entry point and editor/runtime orchestration |
| `src/core/` | Math, ECS, engine components and transform hierarchy |
| `src/physics/` | ReactPhysics3D integration and collider debug drawing |
| `src/rendering/` | OpenGL renderer, procedural meshes and viewport surfaces |
| `src/scene/` | Editor camera and scene-camera calculations |
| `src/audio/` | WAV/MP3 decoding, mixing, listener/spatial audio and previews |
| `src/input/` | Shared keyboard, mouse and gamepad snapshots |
| `src/scripting/` | Lua sandbox, object/component bindings, input bindings, exports and lifecycle |
| `src/editor/` | Panels, Inspector, gizmos, settings, theme, history and playback controls |
| `src/assets/` | Asset resolution, import, storage and copying |
| `src/project/` | JSON/legacy project formats, settings helpers and project storage |
| `src/platform/` | Native file dialogs and system Trash integration |
| `tests/` | Engine/editor regression tests and generated audio fixtures |
| `extensions/legacyscript/` | VS Code extension and bundled language server |
| `scripts/definitions/` | Editor-only Lua API/type reference |
| `third_party/` | Vendored dependencies and licenses |

Includes use subsystem paths such as `physics/physics.hpp`, rooted at `src/`.

## Editor workflow

- **File:** native Open/Save/Save As; Quit is the last entry. Save before quitting.
- **Create:** empty GameObject, Camera, Audio Source or a primitive. The same menu
  is available by right-clicking empty Hierarchy space.
- **Edit:** Undo/Redo, copy/paste, duplicate and parenting operations.
- **Windows:** panel visibility and layout reset.
- **Project → Input Settings:** add or rename an action using its name textbox.
  Each row has **three binding buttons**, accepting a keyboard key, mouse button,
  or gamepad button. Right-click a slot to clear it; Remove deletes the row.
  Enter commits a renamed action. Escape cancels capture.
- **Settings → Editor Settings:** Dark/Grey theme and internal/external code editor.
- **Help:** repository link and About.

Right-click an Inspector component header to remove it. Transform removal is
blocked while a Camera/AudioListener depends on it. Audio transport is script-only.
Double-click folders in Assets, use **Up one folder** to return, and right-click
empty space for **New Folder** or script/text creation. List/Grid icons are at the
bottom-right. Asset types use embedded **Font Awesome Free** icons. PNG, JPEG,
BMP and TGA textures display small aspect-preserving previews. Thumbnails are
stored as 64×64 RGBA in **`<project>/.cache/thumbnails/`**, outside `Assets/`.
The cache checks source size/modification time, regenerates stale/corrupt entries,
and can be deleted safely. Only visible items request thumbnails; each frame loads
up to four entries and decodes at most one uncached image. GPU entries are bounded
to 256; source files are limited to 32 MiB / 16 megapixels. WebP is not supported.
Hover WAV/MP3 assets for Play/Stop preview, independent of Play mode.

Rename a Hierarchy GameObject with **F2** or a double-click. In Assets, use **F2**
or right-click **Rename**. Both open a small popup at the mouse; Enter confirms
and Escape cancels. Double-click a script to open that file in VS Code when the
external editor setting is enabled.

Scene navigation: hold **right mouse** to look and move with WASD/QE. The cursor
is hidden during look and restored on release or focus loss. Shift moves faster;
Shift+wheel while looking tunes boosted speed. Outside right-mouse look, **W/E/R**
in Scene select Move/Rotate/Scale. Ctrl/Alt/Super block camera navigation. Escape does not quit.
F5 starts Play, F6 pauses/resumes, F7 stops. Play-time changes are discarded on Stop.

## Dynamic lighting and shadows

Use **Create > Light > Directional Light / Point Light / Spot Light**, or add a
light component in the Inspector. All three update from their owning Transform
and component settings every frame, in both Scene and Game views. Directional and
spot lights emit along local **-Z**; rotate the object to aim them. The starter
scene includes a directional light. Older projects without lights receive only
ambient illumination until a light is added.

Inspector controls include enabled state, color, intensity, shadow casting, quality and
bias, plus range for local lights and inner/outer half-cone angles for spots.
Directional **Shadow coverage** is the world-space half extent around each view;
objects outside that region still receive light but are not shadowed by that map.
Lights survive save/load, duplication, undo/redo and Play/Stop restoration.

```lua
local lamp = GameObject.new("Moving spotlight")
lamp:setPosition(0, 6, 3)
lamp:setRotation(-65, 0, 0)
local light = lamp:addComponent(SpotLight)
light.intensity = 4
light.range = 25
light.innerAngle = 20
light.outerAngle = 35
light:setShadowQuality(2) -- 0 Off, 1 Low (256), 2 Medium (512), 3 High (1024)
light:setColor(1, 0.8, 0.6)
light:setIntensity(4)
light:setRange(25)
light:setSpotAngles(20, 35)
light:setEnabled(true)
light:setShadows(true)
-- Move/rotate lamp or change light properties from onUpdate for dynamic lighting.
```

The renderer uses a bounded OpenGL 3.3 forward-lighting path rather than introducing
a second rendering framework. Its cost controls are:

- Up to **16 active lights per view**, directional lights first, then local lights
  prioritized by distance relative to range; off-frustum local lights are culled.
- A persistent **12-layer, 1024×1024 depth texture array** (approximately 48 MiB).
  Directional/spot shadows cost one layer; point shadows cost six faces. Shadow
  budgets follow the same priority order; excess lights still illuminate without
  shadows. Disable shadows on fill lights to reserve budget for key lights.
- Shadow-caster sphere/frustum culling, cached mesh bounds/uniform locations, and
  model matrices prepared once per view. Wire geometry does not cast shadows.
- Per-light quality: Off skips shadow passes. Low uses 256² with one bilinear
  comparison lookup, Medium uses 512² with a weighted 3×3 tent (four bilinear
  lookups), and High uses 1024² with a weighted 5×5 tent (nine bilinear lookups).
  Each hardware lookup interpolates four depth-comparison results, so edges
  transition continuously instead of jumping between nearest-depth texels.
  Lower qualities use a smaller region of their fixed atlas layer: they reduce raster/filtering work but do not
  shrink the shared texture allocation.
- Receiver-plane depth correction adjusts comparison depth across the filter
  footprint on sloped surfaces; degenerate gradients fall back safely. Adjustable
  slope-aware bias remains available. World-space derivatives are evaluated before
  divergent light branches, then projected analytically for each light.
- Directional coverage is texel-snapped to reduce shimmering.

Shadow maps are refreshed for each rendered view, so moving lights **and** moving
casters update immediately. This is not clustered rendering, cascaded directional
shadows, baked lighting, global illumination or unlimited lights. Two visible
Scene/Game views incur separate shadow passes. Point shadows are more expensive;
profile your target scenes/hardware and tune range, coverage and shadow toggles.
Tent PCF smooths jagged edges; it does not recover geometry smaller than a shadow
texel or simulate contact-hardening area-light shadows. Large directional coverage
can still lose detail, and point-shadow face boundaries are clamped rather than
filtered across faces. Use tighter coverage or High quality where detail matters.
CI checks all three light types with real OpenGL pixel comparisons for shadow
toggles, moving casters and moving lights; no hardware FPS claim is implied.

## Projects

Use one project per directory:

```text
MyProject/
  MyProject.legacy                 # structured JSON, format 9
  Assets/                         # scripts, audio and other imported assets
  Settings/
    ProjectSettings.json          # project/editor-camera settings
    InputSettings.json            # three-slot named button mappings (v2)
  .cache/thumbnails/               # disposable texture previews; not source assets
```

Save As copies assets/settings to the destination, refusing conflicting asset
content. Untitled data lives in an isolated temporary project until saved.
Distribute the entire project directory, not just the `.legacy` file. Projects
in the same directory share Assets and Settings. Legacy text formats and old
asset-directory references remain readable; saving writes the current layout
without deleting old project data. Scene limits: 10,000 objects, 16 MiB project
JSON and 64 KiB editor source. Runtime state, logs and history are not serialized.

## LegacyScript

Create a `.ls` asset, save it, and attach it to a GameObject. Multiple independently
enabled scripts run in isolated Lua states with `self` bound to their owner.
Existing `.lua` assets remain supported. Lifecycle callbacks are `onStart`,
`onUpdate(deltaTime)`, `onFixedUpdate(deltaTime)` and `onDestroy`.

```lua
@export target GameObject
@export speed number 5

function onStart()
    debug.log("Started", self:getName())
end

function onUpdate(dt)
    if target and isButtonDown("Move") then
        target:translate(speed * dt, 0, 0)
    end
end
```

Define `Move` in Project → Input Settings first. Exported fields appear in the
script's Inspector. Drag objects/components/assets into reference slots; **X**
clears them. Supported values include number, boolean, string, GameObject, engine
component types, Asset and AudioClip. Defaults are literals, not Lua expressions.
Declarations must lead a line; `---@export` comments remain compatible. Bare
`@export` is preprocessed before Lua runs, preserving error line numbers.
Exports currently become globals in the attachment's state: do not shadow them
with local declarations. Missing references bind as `nil` at script startup.

The Lua Editor's Execute action resolves the open asset's selected owner, or its
unique owner automatically. Shared assets require selecting an owner. Open assets
are not additionally run as global snippets on Play. Save asset edits before Play.

Logging: `debug.log(...)`, `debug.warning(...)`, `debug.error(...)`, or `print(...)`.
`debug.error` logs without throwing. This restricted `debug` table is not Lua's
introspection library. The Console offers **Copy all** and right-click **Copy message**.
Lua has bounded memory/instructions and no filesystem/process/module access.

### Audio and input

```lua
local audio
function onStart()
    audio = createAudioSource(self, AudioClip("music.mp3"))
    audio:setVolume(0.7)
    audio:spatialBlend(1)  -- 0 = 2D, 1 = 3D
    audio:setDistance(50)  -- maximum audible 3D range
    audio:fadeIn(2)
end
```

AudioSources show a wireframe speaker glyph only in the Scene editor. Selecting a
spatial source also shows its maximum hearing range as three wire circles in
world units (independent of Transform scale). These gizmos never appear in Game.

Use `self:getComponent(AudioListener):pause()` to disable reception and
`:start()` to enable it again. Sources continue their playback timelines while
the listener is paused; this does not pause or restart individual clips.

Add an enabled AudioListener to the camera/player: gameplay audio is silent without
one, even in 2D. 3D audio uses source/listener world positions and listener rotation;
gain falls linearly to zero at Max Distance. Multiple listeners use the enabled one
with the lowest entity ID. Fade-in continues to the clip's end; fade-out stops after
the fade. WAV/MP3 clips are limited to 32 MiB files / 128 MiB decoded, with a 256 MiB
cache and 128 voices. OGG and automatic Doppler/occlusion are not supported.

Input functions include raw `isKeyPressed/Down/Released`, mouse button equivalents,
mouse position/delta/wheel, gamepad button pressed/down, gamepad axes/sticks, and
named `isButtonPressed/Down/Released`. Digital helpers take defined button names:
`getInputAxis(negative, positive)` and
`getInputVector(left, right, down, up)` (normalized diagonals). Named actions accept up to three alternative keyboard, mouse or gamepad-button
bindings. Existing v1 keyboard settings are read automatically; saves use v2. Edges/deltas last one render
frame. Gameplay input is gated by focus/Play state and disabled while editing text.
The first available gamepad is used, with a 0.15 dead zone. Mapping slots are
buttons, not analog axes; raw stick/axis APIs remain available.

Mouse control functions return a success boolean:
```lua
lockCursor()       -- relative motion; cursor hidden while locked
unlockCursor()     -- leave relative mode
hideCursor()       -- hide without locking
showCursor()       -- request visible cursor (relative mode still hides it)
local visible = isCursorVisible()
local locked = isCursorLocked()
```
Queries report native SDL cursor/window state. Pause, Stop, focus loss and normal
shutdown release the script cursor so the editor does not trap it. Resume does
not automatically relock; scripts may lock again when needed.

See [the API/type definitions](scripts/definitions/legacyengine.lua) and
[the completion catalog](src/scripting/lua_definitions.hpp) for more signatures.

Audio decoding and device playback use vendored miniaudio 0.11.23. Scene audio
retains a deterministic main-thread mixer; the device callback consumes bounded
stereo float PCM without accessing GameObjects. Asset previews have their own
playback queue and do not require an AudioListener. WAV and MP3 decoding are
provided by miniaudio; there is no separate dr_libs dependency.

## VS Code extension

See [LegacyScript extension documentation](extensions/legacyscript/README.md).
Its bundled LSP provides API/export completion, hover/signature help, highlighting,
function outlines and snippets. It is not a full Lua type checker or debugger.

```sh
cd extensions/legacyscript
npm ci
npm test
npm run package
code --install-extension legacyscript.vsix
```

The **engine Release workflow** also tests and builds the extension, publishing
**`legacyscript.vsix`** as a separate downloadable artifact in the same run. Tagged engine
releases attach `legacyscript.vsix` alongside the engine package. Use VS Code's
**Install from VSIX** command with that file. The standalone **LegacyScript VSIX**
workflow remains available (its artifact is a ZIP to extract first). `.ls` files
should show **LegacyScript** as their language. It is not on the Marketplace.

## JSON parsing

Project/settings JSON is parsed with **simdjson 3.12.3**, which selects an available
SIMD implementation at runtime and has a portable fallback. The parsed DOM is
converted directly to the existing editable Json structure; there is no second
text parse. nlohmann/json is retained for schema access/editing and serialization.
Existing JSON project files remain readable. Size, nesting, schema and light/input
bounds are still validated. End-to-end loading also includes DOM conversion and
project validation; no measured speedup is claimed without profiling.

## Automation and limitations

- [Engine workflow](.github/workflows/release.yml): build, regression tests, Xvfb
  graphical light/shadow/cursor tests, screenshot capture, Linux packaging and a
  separate VSIX job/artifact. Tags publish engine and VSIX release assets;
  branch/PR/manual runs provide downloadable artifacts.
- [VSIX workflow](.github/workflows/legacyscript.yml): regenerate engine API catalogs,
  test the LSP and upload the installable extension. Relevant source paths track the
  subsystem layout.

The engine is experimental. Arbitrary imported meshes/material pipelines, OGG,
folder rename/trash, named analog bindings and a full Lua debugger are not implemented.
System Trash is Linux-tested; platform-dependent dialogs, clipboard, VS Code URI
launching, real audio hardware and physical gamepads need target-platform testing.

### Materials

Create a **Material (.mat)** from the Asset Browser's **Create** context menu,
then assign it using the **MeshRenderer → Material** dropdown or drag the asset
onto that slot. An empty slot also offers **Create Material** (seeded with the
mesh's current color/specular). Materials are owned by the project's `Assets`
folder, use numeric duplicate suffixes, and persist as readable JSON.

Expand **Material properties** in MeshRenderer to edit the shared asset:
- **Diffuse** (default): color, albedo texture, UV tiling and specular.
- **Bumped Diffuse**: the same controls plus a tangent-space **Normal map** and
  normal strength. Use OpenGL-style (+Y/green-up) normal maps; this is normal
  mapping, not grayscale height/displacement mapping.
- Texture dropdowns accept PNG, JPEG, BMP and TGA; you can also drag textures
  from the Asset Browser. `None` removes a texture. All built-in solids and the
  plane have UVs. Surfaces remain opaque; texture alpha is not transparency.

Inspector edits are saved immediately to the **shared material asset**, affecting
all users; asset-file edits are not part of scene undo. Script changes are
**per-MeshRenderer overrides**, so Play/Stop restores the scene without modifying
material files. Reset overrides to use shared values again. Old projects without
materials keep their inline colors, checkerboard and specular.

```lua
@export albedo Texture
@export normals Texture

function onStart()
    local mesh = self:getComponent(MeshRenderer)
    mesh:setMaterial("Stone.mat")
    mesh:setMaterialColor(0.8, 0.9, 1)
    mesh:setMaterialTexture(albedo)
    mesh:setMaterialShader("Bumped Diffuse")
    mesh:setMaterialNormalMap(normals)
    mesh:setMaterialNormalStrength(1)
    mesh:setMaterialTiling(2, 2)
    mesh:setMaterialSpecular(0.2)
    -- mesh:resetMaterialOverrides()
end
```

`Texture` is **export-only**: assign its asset in the script Inspector. It is a
project-relative asset reference, not a texture-loading constructor/API. Passing
`nil` to either texture setter clears that instance's texture. Material scripts
and `.ls` autocomplete expose all methods above. `mesh.material` also assigns a
`.mat` path and resets overrides. The older `color`/`specular` fields remain the
inline fallback when no material is assigned.

Rendering uses full-resolution textures with mipmaps and repeat wrapping (not
Asset Browser thumbnails); each image is limited to 32 MiB encoded and
4096×4096 pixels. The GPU texture cache is capped at 128 MiB/128 entries and
checks changed files once per second. Material assets refresh within 250 ms;
missing/invalid assets report errors and fall back safely. Large first-use
texture loads are synchronous. No transparency, custom GLSL, height maps, or
standalone texture-loading scripting API is included.

Asset Browser folders can be renamed with **F2** after selecting them, or with
**right-click → Rename…**, in either List or Grid view. Renaming a folder keeps
nested script, audio, exported asset and material references up to date, including
texture paths stored in `.mat` files. Existing destination names are rejected;
folder deletion remains unavailable.

### Skybox component

Select a GameObject and choose **Add Component → Skybox**. Its **Skybox type** is:

- **Multi Texture Skybox**: assign six PNG/JPEG/BMP/TGA faces using dropdowns or
  Asset Browser drag/drop: **Right (+X), Left (-X), Top (+Y), Bottom (-Y),
  Front (-Z), Back (+Z)**. Images must be equally sized squares, up to 2048².
  Faces use the standard OpenGL cubemap layout (no automatic vertical flip).
- **Skybox HDR**: assign a Radiance **`.hdr` equirectangular panorama**, up to
  4096×2048. OpenEXR is not supported. HDR uses Reinhard tone mapping followed
  by gamma correction for display; it is not clamped to an LDR input texture.

**Skybox color** multiplies the texture (white preserves its color). Disable the
component to turn it off. The first enabled Skybox in component iteration order supplies the
background in Scene and Game views; disable it to select another. With no enabled
Skybox, the existing clear background remains. Missing or invalid images produce
an error and a solid tint fallback. Each encoded image is limited to 64 MiB;
only the active sky's texture is retained on the GPU. Files refresh within one
second. Initial image loading is synchronous.

The background follows camera orientation, not position, and stays behind scene
geometry. The owning object's Transform does not rotate or move it. Skyboxes are
backgrounds only: they do not provide image-based lighting, reflections or ambient
light. Components persist in projects and participate in duplication, undo/redo,
Play/Stop restoration and Asset Browser path renames.

```lua
@export panorama Texture
@export right Texture
@export left Texture
@export top Texture
@export bottom Texture
@export front Texture
@export back Texture

function onStart()
    local sky = self:getComponent(Skybox)
    if sky == nil then sky = self:addComponent(Skybox) end
    sky:setEnabled(true)
    sky:setColor(1, 1, 1)
    sky:setType("HDR")
    sky:setHDR(panorama)
    -- Or use six face textures:
    -- sky:setType("Textures")
    -- sky:setTextures(right, left, top, bottom, front, back)
    -- sky:setFace("front", front)
end
```

`Skybox` can itself be exported as a component reference. Direct fields are
`type` (0 = textures, 1 = HDR), `color`, `enabled`, `hdr`, `right`, `left`, `top`,
`bottom`, `front` and `back`. Texture fields are project-relative paths; `Texture`
exports now also accept `.hdr` for skyboxes, still without a texture-loading API.
Use `setHDR(nil)` or `setFace(name, nil)` to clear a slot. The scripting methods
and properties are included in `.ls` editor/VS Code completion.

### Debug menu

The top bar's **Debug** menu has two session-only toggles (off by default):
- **Show FPS** displays the editor's rolling frame rate over the Game view. This
  measures the whole editor frame, not an isolated player-render benchmark.
- **Show Colliders in Game** overlays all collider wireframes in green using the
  active Game camera, including during Play. Hidden edges are visible for debugging.

These options do not change the scene or saved project. Without an active camera,
the Game view continues to show only its centered no-camera message.
