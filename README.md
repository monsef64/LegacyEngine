# LegacyEngine

A C++17 3D editor built with SDL3, OpenGL, Dear ImGui, ReactPhysics3D and Lua 5.4.
LegacyScript (`.ls`) adds Inspector-exported fields and engine APIs on top of Lua.

![LegacyEngine editor: Scene viewport, Hierarchy, Inspector, Assets and scripting panels](docs/screenshot.png)

## Current features

- Dockable Scene, Game, Hierarchy, Inspector, Assets, Lua Editor and Console panels.
- Empty GameObjects, cube/sphere/capsule/cone primitives, cameras and audio sources.
- Transform gizmos and labeled X/Y/Z fields; Inspector rotation is limited to ±360°.
- Physics colliders and rigid bodies, parenting, duplication, clipboard, Undo/Redo.
- Play, Pause/Resume and Stop with restoration of the edit-time scene.
- Hierarchy search, object icons and a read-only level root named after the project.
- Camera projection, tracking/following, shake and screen/world conversion APIs.
- AudioSource and AudioListener, WAV/MP3 playback, looping, pitch, volume, pan,
  Spatial Blend, range attenuation, fades and one-shots.
- Project-owned Assets with folder browsing, file import/rename/trash, drag-and-drop
  script/reference assignment and listener-independent audio previews.
- Internal Lua editor with highlighting or external VS Code editing; Dark/Grey themes.
- Project-defined keyboard buttons and raw keyboard/mouse/gamepad scripting input.
- Structured JSON projects, separate settings files and native Save/Open dialogs.

## Build and test

Requires a C++17 compiler, CMake 3.16+, Git and platform development libraries.
CMake fetches SDL3 (if not installed), ReactPhysics3D, Dear ImGui, ImGuizmo and Lua.
GLAD, the font, dr_mp3 and the JSON library are included under `third_party/`.

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
- **Project → Input Settings:** define a button name and capture a key; rebind/remove
  entries later. Escape cancels key capture.
- **Settings → Editor Settings:** Dark/Grey theme and internal/external code editor.
- **Help:** repository link and About.

Right-click an Inspector component header to remove it. Transform removal is
blocked while a Camera/AudioListener depends on it. Audio transport is script-only.
Double-click folders in Assets, use **Up one folder** to return, and right-click
empty space for **New Folder** or script/text creation. List/Grid icons are at the
bottom-right. Hover WAV/MP3 assets for Play/Stop preview, independent of Play mode.

Scene navigation: WASD/QE, right-drag look, Shift for faster movement and Shift+wheel
to tune boosted speed. Ctrl/Alt/Super block camera navigation. Escape does not quit.
F5 starts Play, F6 pauses/resumes, F7 stops. Play-time changes are discarded on Stop.

## Projects

Use one project per directory:

```text
MyProject/
  MyProject.legacy                 # structured JSON, format 9
  Assets/                         # scripts, audio and other imported assets
  Settings/
    ProjectSettings.json          # project/editor-camera settings
    InputSettings.json            # named keyboard-button bindings
```

Save As copies assets/settings to the destination, refusing conflicting asset
content. Untitled data lives in an isolated temporary project until saved.
Distribute the entire project directory, not just the `.legacy` file. Projects
in the same directory share Assets and Settings. Legacy text formats 1–8 and old
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
`getInputVector(left, right, down, up)` (normalized diagonals). Named bindings are
keyboard-only; raw mouse/gamepad APIs work separately. Edges/deltas last one render
frame. Gameplay input is gated by focus/Play state and disabled while editing text.
The first available gamepad is used, with a 0.15 dead zone.

See [the API/type definitions](scripts/definitions/legacyengine.lua) and
[the completion catalog](src/scripting/lua_definitions.hpp) for more signatures.

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

Alternatively download **legacyscript-vsix** from a successful **LegacyScript VSIX**
Actions run, extract it and use VS Code's **Install from VSIX** command. `.ls` files
should show **LegacyScript** as their language. It is not on the Marketplace.

## Automation and limitations

- [Engine workflow](.github/workflows/release.yml): build, regression tests, Xvfb
  graphical smoke test, screenshot capture and Linux packaging. Tags publish releases;
  branch/PR/manual runs provide downloadable artifacts.
- [VSIX workflow](.github/workflows/legacyscript.yml): regenerate engine API catalogs,
  test the LSP and upload the installable extension. Relevant source paths track the
  subsystem layout.

The engine is experimental. Arbitrary imported meshes/material pipelines, OGG,
folder rename/trash, named analog bindings and a full Lua debugger are not implemented.
System Trash is Linux-tested; platform-dependent dialogs, clipboard, VS Code URI
launching, real audio hardware and physical gamepads need target-platform testing.
