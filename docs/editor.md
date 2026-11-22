# Editor workflow
- **File:** native Open/Save/Save As; Quit is the last entry. Save before quitting.
- **Create:** empty GameObject, Camera, Audio Source, Skybox or a primitive (including Plane). The same menu
  is available by right-clicking empty Hierarchy space.
- **Edit:** Undo/Redo, copy/paste, duplicate and parenting operations.
- **Windows:** panel visibility and layout reset.
- **Project → Input Settings:** add or rename an action using its name textbox.
  Each row has **three binding buttons**, accepting a keyboard key, mouse button,
  or gamepad button. Right-click a slot to clear it; Remove deletes the row.
  Enter commits a renamed action. Escape cancels capture.
- **Settings → Editor Settings:** Legacy Dark, Grey, or Classic Light theme and internal/external code editor.
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
Wheel while looking/moving adjusts normal freecam speed; Shift+wheel tunes the
boost multiplier. When not looking/moving, vertical wheel zooms and horizontal
wheel pans. **Alt+left-drag** orbits around the selected object's world position,
controlled by mouse movement. Outside right-mouse look, **W/E/R** in Scene select
Move/Rotate/Scale. Ctrl/Super block navigation; Alt is reserved for orbit. Escape does not quit.
F5 starts Play, F6 pauses/resumes, F7 stops. Play-time changes are discarded on Stop.

## Focus the selected object

Select a GameObject in Hierarchy or Scene and press **F** while either panel is
focused to frame it in the Scene camera. Viewing direction is preserved; camera
distance adapts to object scale/collider bounds and the viewport's aspect ratio.
This affects the editor camera only, not the Game camera. It is disabled while
playing, typing, dragging a widget, navigating with RMB, or using Ctrl/Alt/GUI.
Objects without a Transform are ignored.

## Creation and gizmos

**Create → Shape → Plane** (also in the Hierarchy context menu) creates a lit,
UV-mapped 16×16 XZ plane with a thin static box collider. Its size can be changed
with Transform scale; it has no RigidBody by default. **Create → Rendering → Skybox** creates
a GameObject with a Skybox component, ready for six textures or an HDR panorama.
MeshRenderer's mesh dropdown and `mesh.mesh = "Plane"` also support planes.

Selecting a camera shows a white wire view frustum, driven by FOV, aspect,
orthographic size and near/far clip values, pointing down local -Z. Camera
body/lens icons and these lines are Scene-only, unlit and never shadow casters;
they never appear in Game. Like the audio-range sphere, camera gizmos use world
units rather than inheriting arbitrary object scale.

The internal code editor uses one syntax-colored text layer while retaining
native caret, selection, scrolling, completion and undo. UI text uses embedded
18 px Roboto with antialiased rasterization and matching icon sizing (previously
16 px); framebuffer-density support remains enabled for high-DPI displays.


Assets is the selected bottom tab on startup. **Script Editor** and **Console**
remain available from the window menu. File rename edits the base name and keeps
its original extension automatically; folder rename still edits the full name.

Right-click the Hierarchy level root and choose **Rename Level...** to rename
both the saved level and its registry entry (outside Play). The root remains
nonmovable and cannot be parented.

Creation menus group **Audio Source** under **Audio**, and **Skybox / Camera**
under **Rendering**. **Settings > Editor Settings > Always Show Colliders** shows
unselected Scene colliders in translucent green; the selected collider stays
bright green. **Debug > Show Game Colliders** controls the separate Game overlay.

**Classic Light** uses light-grey panels, blue selections, bordered controls and
top-lit glossy buttons inspired by the older editor. Select it under
**Settings > Editor Settings > Theme**; the choice persists between launches.

## Reference layout and orientation gizmo

The default workspace follows the classic editor arrangement: Hierarchy at the
upper left, Scene and Game as central tabs, a full-height Inspector on the right,
and Assets/Console/Script Editor tabs across the bottom left and center. Assets
is initially selected. **Windows > Reset layout** restores this arrangement.
The updated default uses a new dockspace identifier so older saved layouts do
not hide the change; subsequent custom arrangements are still saved normally.

Choose **Settings > Editor Settings > Theme > Classic Dark** for the
reference-inspired charcoal panels, grey glossy buttons and muted blue selections.
The earlier light **Classic Light** theme remains available.

The Scene viewport's upper-right orientation gizmo shows colored X/Y/Z axes.
Click a colored axis endpoint to snap to the positive direction, or a pale
opposite endpoint to view from the negative direction. Drag within the gizmo to
orbit around the Scene camera's current pivot. Snapping preserves that pivot and
viewing distance, including stable straight-down/straight-up views. This is
Scene navigation only: it does not rotate selected objects or the Game camera.

## Scene-only grid and file workflows

Spawn mode has been removed. Create objects using the main **Create** menu or
Hierarchy context menu; Scene clicks select objects. New startup scenes no longer
contain a Grid GameObject or a checkerboard ground plane. Existing authored scene
objects are not silently removed when opening an older project.

The grid is an engine-owned Scene overlay, absent from Hierarchy, Game rendering,
physics, saved scene content and scripts. In **Settings > Editor Settings > Scene
Grid**, adjust visibility, XZ/XY/YZ plane, X/Y/Z position, cell count, spacing and
color. These preferences persist with editor settings. User-created planes remain
available through Create.

**File > New Level...** and **File > Save Level** provide direct level actions.
Project Save/Save As saves the current level and pending script document along
with project assets. The default bottom tab order is **Assets, Script Editor,
Console**; use **Windows > Reset layout** to restore it. Assets also has an
icon-only **New Folder** button with a tooltip.

ImGui conflict highlighting and red recovery tooltips are disabled in the engine
viewport. Recoverable UI errors are still sent to the engine log rather than
silently discarded.

## Settings history and Play destination

Editor preferences (including theme and Scene grid), rendering VSync and project
input bindings participate in Undo/Redo in Edit mode. Settings actions log to the
Console, including undo and redo. Slider gestures are grouped on release. Level
New/Save actions live only in the File menu; Project > Levels selects levels.

Play selects/focuses Game, even when its tab was hidden. Enable **Separate Game
window** beside the playback controls before Play to render into an additional
resizable native window. It shares the running world, pauses with Pause, and
closes on Stop. Closing that window returns to the docked Game view without
stopping Play. Keyboard/mouse input follows the focused Game window.

The title reads `LevelName* - ProjectName - Linux` (or `Windows`); the asterisk
marks unsaved scene changes. Asset browsing lists folders first, then files, with
folder names only; import, script creation and folder creation use right-aligned
icons. Materials display a lit sphere thumbnail (color, albedo, specular) and a
larger rendered sphere preview on hover. Grid icons are lightweight approximate
previews, not a full renderer/normal-map preview.

GameObject **Active** in Inspector controls local activity. Disabled parents
also disable their children; reactivating a parent preserves each child's local
setting. Inactive objects are hidden from rendering/shadows, removed from
physics, silent, and do not run attached scripts, mouse callbacks, custom event
listeners or wait timers. They remain selectable in Hierarchy and persist in
scene saves and Undo/Redo.

## Prefab and Scene workspace changes

[Prefab assets](prefabs.md) open an isolated Prefab Window; linked hierarchy
objects are blue. Clicking unused space in Hierarchy clears selection.
Move/Rotate/Scale controls now sit in their own bar directly below the Scene tab,
above the viewport image: the bar is not part of camera rendering or picking.
Asset icons reserve explicit pixel space and textures offer large hover previews.

### Deleting a hierarchy

Deleting a parent GameObject removes **all children and grandchildren**, including
inactive objects. Undo restores the complete subtree and its components; redo
removes it again. To retain a child, unparent it before deleting the parent.
Scripting `destroyGameObject`, `destroy` and `object:destroy()` use the same rule.

### Copy and paste component values

Right-click a component header (expanded or collapsed) and choose **Copy values**.
Right-click the matching component on another GameObject and choose **Paste values**
to replace all its values. Paste is disabled for different component types or an
empty clipboard. The target must already have that component; other components,
object identity, activity and parenting are unchanged. Paste supports Undo/Redo.

The clipboard holds a value snapshot, so later edits to or deletion of the source
do not change it. Copying another component replaces the clipboard. Transform
copies the displayed parent-local pose for children (world pose for roots), so
pasting makes the destination Inspector values match without reparenting it.
Use within the same project: asset handles and script object references are copied
as-is, not cloned or remapped. AudioSource copies its full state as well as settings.

### Hierarchy groups and folding

Click a row for primary selection, Ctrl-click to add/remove rows, or Shift-click
to select a visible range. The primary row drives Inspector and the transform gizmo.
Delete removes selected roots and their subtrees; Unparent and drag/drop operate
on selected roots, without processing selected descendants twice. Drop on another
object to parent the group, or on blank Hierarchy space / the level root to unparent
while preserving world poses. Cyclic group reparenting is rejected. Arrows beside
parents fold/unfold their children; search temporarily reveals matching descendants.
With multiple objects selected, Inspector exposes shared **Active**, **Static**, **Layer**,
and local **Position / Rotation / Scale** controls. Values initially show the primary
object; editing an axis assigns that axis to every selected object without overwriting
their other axes. Parent/child selections retain local-space semantics. Select one
object for component-specific properties. Gizmo transforms still affect only the primary.
Batch Inspector edits participate in normal undo and Play-mode rollback.

Scene transform and playback toolbars do not scroll. Asset navigation uses an
up-arrow button (hover for “Up one folder”). Create menus include Character
Controller and FPS Camera; see [first-person setup](first-person.md).

### Inspector order and filtered asset menus

Drag a component header onto another component header **on the same GameObject**
to place it before that component. This changes display order only (not script or
physics execution order), supports Undo/Redo, and persists with scenes/prefabs.
Component dragging onto exported fields still assigns component references.
Script Explorer files now drag as Assets too, so drop them onto Hierarchy objects
or Inspector script slots just like files from Assets.

Material, texture, cubemap/HDR, audio and script slots open filtered popup menus
showing only supported files, grouped into their Assets folder hierarchy. Exported
asset slots use the same picker. Drag/drop and None clearing remain available.
Character ability toggles are horizontal without the old debug-state text.
Audio range visualization uses three simple wire circles, without changing its
actual hearing range. See [physics queries](physics-queries.md) for trigger and
Raycast Inspector settings and Project layer management.

### Camera previews and material assets

Selecting a GameObject with a Camera displays a small live preview in the
bottom-right of Scene. It uses that camera without activating it or changing
its saved settings; it renders only while Scene and the selected camera are
available. New primitives start white (existing object colors are unchanged).

Select a `.mat` asset in Assets to edit its shader, color, textures, normal-map
settings, UV tiling and specular in Inspector. Changes save directly to the
shared asset, affecting every object that uses it. Select another object to
return to its component Inspector.

File labels hide extensions and truncate names longer than twelve characters
with `...`; folder names are never shortened. Labels are centered in grid view
and left-aligned in list view. Hover an item for 1.5 seconds to see its
full name and type with a large preview: textures, rendered model/mesh geometry,
material spheres, cubemaps, and static prefab geometry; scripts show an excerpt
and audio shows a waveform and duration (hovering does not start playback).
Models retain their part materials. Model import settings in Inspector also show
the rendered model. Preview scenes are isolated from the edited level and never
run scripts or physics. Rendered previews are cached until the selection, asset
file, project, or model import settings change; invalid assets show an error
instead. File content previews are bounded. Paths on disk are unchanged.
The Scene viewport has no scrollbars; mouse-wheel viewport navigation is unchanged.
The default dock layout places Prefab Window below Hierarchy. When the separate
Game window is open, the embedded Game viewport is not displayed or rendered.

The Assets spacing slider sits left of the view-mode buttons. It changes grid
cell spacing only and is disabled in list view. Inspector's header displays the
object name, layer and unlabeled Active checkbox on a single row. Scene editing
always uses freecam controls; the old toolbar toggle has been removed (Game
FPSCamera movement settings are unchanged). Layers and Rendering are available
directly from Project, without a Project Settings submenu.

### Creating scripts in Script Explorer

Use **New Script** in Script Editor's explorer to create and open an empty `.lua`
asset beside the current script, or in Assets when no script is open. Right-click
an explorer folder and choose **New LegacyScript (.lua)** to create one there;
right-click empty explorer space to create one at the Assets root. Filenames
start at `NewScript.lua` and use numbered suffixes to avoid overwriting files.
The current document is saved before switching. The tree refreshes automatically.

Hover a script filename for its full path and available actions. Select/open a
script and press **F2**, or right-click a file and choose **Rename**. Enter confirms
and Escape cancels. The extension is preserved and existing files are not
overwritten. **Duplicate** creates a numbered copy in the same folder. **Delete**
asks for confirmation, sends the file to Trash and supports editor Undo/Redo;
references to deleted files remain missing until restored or reassigned.

Drag script files onto an explorer folder (including empty folders) to move them;
drop onto the **Assets** heading to move back to the root. The same payload still
works on script assignment targets in Inspector/Hierarchy. Rename/move reuses the
Asset Browser's project-reference remapping, including scene attachments and
saved asset references, and keeps the currently edited script open. Dirty document
contents are saved before file operations. File management is disabled during
Play. Create folders through Assets; folder renaming/deletion remains there.

Manual checks: rename with F2/right-click, cancel, reject duplicate/invalid names;
duplicate unsaved text; move into an empty/nested folder and back to Assets;
verify attached scripts and reopened levels/prefabs retain paths; delete/cancel,
undo/redo deletion, and verify Play disables file operations. These interactive
checks are pending; no automated tests were added or run.


See [static 3D model import](models.md) for Assimp format choices, baked model
assets, model placement and the current static-only limitations.

The Inspector no longer has a Parent dropdown; drag objects in Hierarchy to
change their parent or use its unparent action. For the separate Game window,
press **F11** or **Alt+Enter** while it has focus to toggle desktop fullscreen.
The editor toolbar also provides **Fullscreen Game / Windowed Game** while that
window is open. These shortcuts are reserved for window control and are not
forwarded to gameplay. Repeated key-down events do not toggle repeatedly.

Asset import offers **Files / Models** and **Folder (keep structure)...**.
The Assets toolbar also has a direct **Folder...** button; the folder picker
accepts multiple selections.
Imports and OS drops go into the current Assets folder. Clicking a model opens
its shared import-scale settings in Inspector; see [models](models.md) for
multiple mesh/material support and static triangle-mesh collision geometry. Instantiation
creates a named empty model root with mesh children.

After deselecting an object, **Alt+left-drag** still orbits its last remembered
position. Right-drag free-look and the existing camera navigation remain available.

Inspector's **Create Material** button and material-slot context action first
open a naming popup. Enter a filename (the `.mat` suffix is optional), then
choose **Create** or press Enter. **Cancel** / Escape creates nothing. Invalid
filenames and existing names are rejected without overwriting assets. Successful
creation saves the material in Assets and assigns it to the MeshRenderer.

## Autosave, safe exit, and project cleanup

Editor Settings includes **Autosave delay (seconds)** (default 120; 0 disables).
Autosave starts only after the project has a disk path, saves dirty edit-mode
content, and pauses during Play, prefab editing, gestures, and file dialogs.
Failures leave the project open and report the save error.

Closing the engine, File > Quit, and Alt+F4 use the same exit flow. Play mode is
stopped/restored first. Unsaved projects offer **Save / Discard / Cancel**; saved,
unchanged projects exit without that dialog. Canceling Save As or a failed save
does not exit. Pending dialogs/transitions and prefab editing must be finished first.

**Project > Fix Project** rewrites the current project and saved `.level`/`.prefab`
assets using the current supported schema, dropping retired component fields.
It preserves `.before-fix.bak` copies before rewriting. It does not guess how to
repair corrupt files or unsupported future formats. Review backups before deleting.

Assets has a case-insensitive search field (searches recursively within the current
folder). Inspector asset buttons open a separate searchable selection window,
filtered to matching types. Model thumbnails are fixed-size and cached, with
bounded per-frame generation. Separate Game windows also show FPS when enabled.
Scene free-look and game cursor locking use relative mode plus window capture and
viewport confinement; releasing capture or losing focus restores the pointer.

## Project backups

**Project > Backup** first saves the current project, then writes a ZIP of the
entire project directory, including assets, settings, and empty folders, to
`Backups/ProjectName_Backup_YYYY-MM-DD_HH-MM-SS.zip`. The root `Backups` folder
is excluded to avoid recursively archiving older backups. Repeated backups in
the same second get a numbered suffix. Save the project to disk first; Backup
is disabled during Play, prefab editing, and pending editor operations.

Backups use portable, uncompressed ZIP entries and stream files instead of
loading the project into memory. Symbolic links and unsupported filesystem
entries are rejected. This implementation supports ZIP32 archives (less than
4 GiB total and at most 65,534 entries); larger projects report an error rather
than producing a truncated archive. Failed writes remove the partial ZIP.
The Project menu shows the last successful backup path on hover.

## Collider face handles

Select an object with a **Box collider** to see a dot at the center of each face
in Scene. Drag a dot along that face's axis to change the collider Size and
Center while the opposite face stays fixed. Rotation, nonuniform scale, and
mirrored Transform scales are respected; resizing never scales the visible mesh.
Face resizing is grouped into one Undo/Redo operation per drag. Release the mouse
to finish; the minimum local size is 0.001. Orbit the camera if a face's axis is
pointing directly toward the camera and cannot be dragged reliably.

Sphere/Capsule/Cone dimensions remain editable in Inspector. Mesh colliders use
their source triangles, so box-face resizing does not apply to them.

Collider drag fixes: Scene transformation and collider-face gestures remain enabled
while dragging; Undo and unrelated editing commands stay disabled until release.
Mesh colliders automatically follow the same object's rendered `.lemesh` model
when no explicit source is chosen. Their overlay shows the actual simplified
triangles (not a cube). Without either model source, the collider is disabled
with a diagnostic rather than silently substituting box geometry. Mesh scale
handles allow independent X/Y/Z scaling; uniform scale remains for round shapes.

**Friction** and **Bounciness** are contact-surface properties on Collider, so
static floors and walls can have different materials without a RigidBody.
RigidBody owns mass, gravity, and linear/angular damping, not surface material.

## Transform snapping and pivot placement

The Scene toolbar has a right-aligned **Snap** increment, initially **0.50**.
Its minus/plus controls change by **0.25**, with a minimum of **0.25**. Hold
**Ctrl** while dragging a transform gizmo to snap; release Ctrl for free dragging.
Move uses scene units, Rotate uses degrees, and Scale uses scale increments.
Collider face handles are independent of transform snapping. Narrow Scene panels
use a second toolbar row so the pivot and snap controls remain accessible.

Choose **Origin** or **Center** next to World/Local. Origin uses the selected
object's Transform position. Center uses the combined world-space mesh bounds
of that object and all descendants, including imported model parts; groups with
no meshes use their children's positions. Rotation and scaling operate about
that center, and translation moves the group without changing the model's baked
origin. The center is held fixed relative to the object throughout a gesture to
avoid drift. This is a gizmo-pivot setting, not a permanent mesh-origin edit.

Third-party licenses ship under **data/license/**; see its README for the source
versions, nested dependency notices, and the refresh command.

## Terrain foliage and mesh workflow

Terrain tools has separate Grass and Trees painting tabs, including GPU-instanced
placements and five generated grass test cutouts. New imported models use `.mesh`,
with `.lemesh` backward compatibility. See the
[terrain foliage and mesh guide](../docs/manual/terrain-foliage-and-mesh-assets.md)
(repository) for usage, limits and manual checks.


### Alphabetical creation/component menus

The **Create** menu (including Hierarchy context menus) and Inspector **Add
Component** menu are alphabetical, including their submenus. Categories and
component names are retained. Hierarchy objects keep their existing entity order;
the earlier **A-Z Order** Hierarchy toggle has been removed. For Vehicle objects,
the context menu still offers **Create wheel meshes**; see [vehicles](vehicles.md).


### TPS, environment lighting and Scene interaction follow-up

See [TPS camera and environment lighting](third-person-and-environment.md) for
third-person camera setup/Lua, optional sky-colored ambient light, steering
stability, terrain surface edges, expanded Scene picking, middle-drag navigation,
near-camera creation, field Reset value controls and uncapped defaults.
Existing saved FPS caps/VSync preferences remain respected.


See [Mesh LOD and baked occlusion](lod-and-occlusion.md) for the MeshRenderer
**Generate LODs** action and **Windows > Occlusion Culling** bake workflow.


[Terrain chunks and Navigation](navigation.md) documents smaller camera-culled
terrain pieces, the Recast/Detour bake window and scriptable Navmesh Agents.

### Sublime Text 4

**Editor Settings > Code editor** now offers **Internal**, **VS Code**, and
**Sublime Text 4**. The selection persists across launches. In external mode,
double-click a LegacyScript asset to open it in the selected editor. Install the
`code` or `subl` launcher in PATH; standard Windows application locations are
also detected (and the standard macOS Sublime application is recognized).
Install native Sublime syntax highlighting, API/member completions and hover help
by copying `extensions/sublime-legacyscript` into Sublime's Packages directory as
`LegacyScript`. See that folder's README for installation details and limitations.
The existing Animation player and cross-fades remain; the recently introduced
Animator/AnimatorSheet state-machine system and its asset creation/editor UI have
been removed. Old serialized Animator components are ignored when loading a scene.

### Focus suspension, primitives and terrain tabs

When neither the editor nor the external Game window has input focus, the main
loop stops simulation, scripting, rendering and autosave work and blocks waiting
for a window event. Audio output is suspended without resetting playback state.
Focus recovery resets frame timing so no catch-up simulation runs. A focused
external Game window continues normal gameplay. Input capture is released on focus
loss. Asset importing is an explicit exception: OS file/folder drops and completed
native import-dialog selections are accepted while unfocused, and queued imports
continue to completion. Pending imports/dialogs use a low-frequency timed event
wait; an idle engine still blocks normally. Thumbnail GPU updates wait until
rendering resumes. Gameplay, rendering, scripting and autosave remain suspended;
this is main-loop suspension, not unsafe suspension of every process thread.

New primitive shapes retain a Collider but no longer get a RigidBody automatically.
Add RigidBody explicitly when physics-driven movement is desired. Character and
vehicle presets still include their required physics components.
Sphere Transform scale is independently editable on X/Y/Z in the Inspector and
scale gizmo. Uniform spheres retain the exact sphere collider; non-uniform spheres
use a bounded convex ellipsoid approximation for collision. Collider wireframes
follow the ellipsoid dimensions. Existing objects are not automatically rescaled.

Terrain tools docks into the Hierarchy dock node when it appears, sharing the tab
bar rather than covering Hierarchy in a floating window. Click either tab to
switch. Reset Layout also places Terrain tools beside Hierarchy. The panel can
still be manually undocked; reopening it docks it with Hierarchy again.

### Layer collision matrix

Open **Layers** from the existing editor settings/menu entry. Add named layers,
assign them to objects in the Inspector, then expand **Layer Collision Matrix**.
The scrollable triangular table contains each layer pair once. Checked means
physics collisions and trigger events are enabled; unchecked means the pair is
ignored. The diagonal controls collisions within the same layer. The rule applies
symmetrically, including terrain colliders and stationary trigger overlaps.

Settings are saved in the project's `Settings/Layers.json`. Existing projects
without a matrix default to all pairs enabled. Removing/reusing a layer resets its
pair rules. Collision rules are edited outside Play mode. Raycast/overlap query
masks remain separate: a disabled physics pair does not globally hide objects from
picking, camera obstruction queries or scripted queries.

### Manual verification checklist

- Create cube/sphere/capsule/cone/plane; ensure no automatic RigidBody, then add one
  manually and verify physics. Character/vehicle presets should remain functional.
- Resize a sphere independently on all axes using Inspector and gizmo. Check its
  visual shape, collider overlay and contacts. Uniform spheres should stay spherical.
- Add two layers, assign a falling rigid body and floor, and disable their pair.
  Verify pass-through; enable again and verify contact. Check same-layer diagonal,
  triggers (including stationary ones), terrain colliders and save/reopen behavior.
- Select terrain and verify Terrain tools and Hierarchy share one tab strip, both
  in an existing layout and after Reset Layout.
- Alt-Tab during Play: simulation/audio should pause and CPU/GPU usage should drop.
  Return after several seconds: no time jump or stuck keys. Test with the external
  Game window focused, then with both engine windows unfocused. Closing the engine
  must still honor the normal save/discard prompt.
- Bake a static terrain and mesh scene; check terrain occluders and chunk bounds in
  the selected-camera debug overlay. Sculpt/move the terrain: bake must bypass
  until rebaked. Terrain shadow casters should remain correct.


## Folder import, orbit capture and mesh-part selection

**Folder...** in Assets copies each selected folder into the currently open
Assets folder, preserving nested paths and empty directories. Dropping a folder
from the OS file manager uses the same background import queue. Existing folder
names get numbered copies rather than merging or overwriting. Supported model
sources inside the copied tree are converted in their own subfolders; the original
files and relative resource layout remain in place. Import still rejects symlinks,
unsupported Blender sources and oversized batches (10,000 files, 1 GiB total,
256 MiB per file); model conversion failures are reported without deleting the
copied source tree.

**Alt+left-drag** starts Scene orbit even after editing an Inspector/script text
field. It transfers focus to Scene, commits the active transform gesture, and
uses relative mouse deltas without relying on a Windows motion-event button
mask. Left-button release, Escape or window-focus loss releases capture. An
Alt-click without movement does not jump the view or change selection.

Scene selection now defaults to the visible **child/mesh part** hit by the ray.
The Scene toolbar's **Select model roots** checkbox restores assembly-root
selection; **Ctrl-click** still picks the part when that checkbox is enabled.
Visible triangles take priority over enclosing collider proxies, so a parent's
large collider cannot prevent selecting its mesh children. Children can then be
moved, rotated and edited independently in Inspector. Invisible/occluded children
and empty objects remain directly selectable through Hierarchy. This selects
existing child GameObjects/model parts, not individual vertices or arbitrary
triangle islands within a single mesh.

Manual verification (no automated tests):

- Import a folder containing two nested subfolders with identically named files,
  an empty directory, and a glTF with relative textures/bin files. Verify paths,
  converted model placement and materials. Repeat import and check numbered
  copies, then select multiple folders and try OS folder drag/drop.
- Select a model, edit an Inspector text field, then Alt+left-drag in Scene.
  Confirm continuous orbit on Windows, including over a gizmo. Check plain
  Alt-click, Escape, release outside Scene and application focus loss; capture
  must clear and RMB/WASD must continue working.
- Instantiate a multi-part model, add a large root collider, and click separate
  visible parts: Inspector should show the exact mesh child. Toggle **Select model
  roots**, then compare plain click with Ctrl-click. Move a selected child,
  undo/redo, and verify the parent and siblings remain unchanged.

### Process and GPU usage

Debug > Profiler displays process resident RAM and CPU utilization normalized across
all logical cores (100% means all cores), sampled twice per second while open.
GPU memory usage is device-wide, not an attribution to LegacyEngine, and is shown
only when the OpenGL driver supports `GL_NVX_gpu_memory_info`. Unsupported drivers
show unavailable rather than an estimated number. Pause sampling freezes these values.

### Manual performance and batch-edit checks

- Select objects with different positions, including a selected parent and child.
  Change only X; confirm Y/Z remain unchanged and local coordinates are preserved.
- Batch-toggle Active / Static, assign a layer, and undo/redo. Repeat in Play,
  then Stop and verify the authored scene is restored.
- Edit an attached script externally: exported fields should refresh within about
  half a second without resetting already assigned Inspector values.
- Change a shared material through Inspector and confirm immediate persistence;
  switch projects to confirm cached values do not leak between projects.
- Open Profiler, exercise the scene, pause sampling, and verify metrics freeze.
  Confirm unsupported GPU drivers show an unavailable label.
- Move the camera away from terrain; confirm no missing chunks when returning.
  Terrain detail and silhouettes should be unchanged (no new automatic LOD).

### When Bake level does not start

The Lighting window now reports missing prerequisites when **Bake level** is clicked,
rather than silently ignoring the click. A receiver must be an active, static mesh
or terrain without animation/dynamic rigid-body ownership. A source must be an
active, enabled light with positive intensity and **Mode: Static in the light
component**. Checking only the light GameObject's Static box is not sufficient.
Open/save a project first. At most 128 static light sources are supported.

Manually verify no-receiver, no-static-light, and valid-scene cases. In a valid
scene the progress display should appear; after completion verify assignments,
shadows, save/reopen persistence, and undo. This startup feedback change does not
establish that every reported bake-start failure has the same cause.

### Half-Life 1 MDL import

Import the main GoldSrc studio `.mdl` file (IDST version 10). The importer uses
the existing BSD-licensed Assimp dependency, not Half-Life Asset Manager code.
Meshes, bones, animation clips, and embedded palette textures flow through the
normal model pipeline. Keep external texture (`*T.mdl`) and numbered sequence-group
files beside the source model during import. Import the main file, not its companions.
Quake MDL and Source-engine MDL files are rejected explicitly.

This is a model/animation conversion, not a Half-Life runtime: game-specific
sequence events, blend controllers, bodygroup switching, attachments, and hitbox
semantics are not exposed as engine gameplay behavior. Check model orientation,
scale, default bodygroups, texture alpha, skeleton deformation, and all clips
manually, including models with external companion files. Converted resources
are stored in the same packed model container as other new model imports.

## Texture quality and packed model containers

Project Settings > Rendering selects **Full / Half / Quarter / Eighth** texture
resolution. This reduces uploaded material image dimensions (including normal maps)
before mipmap generation; it does not modify source files. Light cookies and scripted
GUI images remain full resolution. `getTextureQuality()` returns 0..3;
`setTextureQuality(quality)` overrides it during Play only, including from `onStart`.
Stop restores the project value; the runtime override is never serialized.

New model imports store meshes, animation clips, materials and encoded textures in
one `.mesh` container, not a generated asset folder. Expand its arrow in either
Asset Browser layout to select or drag individual subassets. References such as
`Character.mesh#Animation_0.leanim` identify virtual members, not filesystem files.
Materials remain editable through the Inspector; saving updates the container.
Move/copy/rename the owning model to keep its members together. Older separate-file
models remain supported. Packed models are bounded to 256 MiB of decoded JSON;
each encoded texture is bounded to 32 MiB. Texture rendering retains its existing
image dimension limits, so packing does not bypass renderer resource budgets.

The multiple-object Inspector exposes fields on components shared by the whole
selection. Mixed fields are marked; editing a vector axis preserves other axes on
each object. Each field is validated for all selected objects before application.
Component runtime state and unrelated fields are not copied from the primary object.
Specialized script attachment, terrain brush and river point editors remain
single-object tools. Batch changes use the normal Undo/Redo and Play rollback.

In the Occlusion Culling window, the selected-camera visibility preview excludes
objects outside that camera's frustum and, when valid baked data exists, baked
occluded objects. It does not change authored Active flags. Preview overlays are
hidden so editor helpers do not obscure the retained visible geometry, except
for the selected camera frustum lines, which remain visible for orientation.


### Terrain tools and folder deletion

Selecting a Terrain no longer opens Terrain tools automatically. Use **Open terrain
tools** in its Inspector to open the panel. Creating/adding a Terrain can still
open its tools as part of that explicit creation action.

Folder deletion first attempts the normal Trash/bin operation with Undo support.
If that fails, the Assets browser offers a separate **Permanently delete** warning
with Cancel. Permanent deletion includes all folder contents and cannot be undone;
it is never attempted silently. Scene references remain missing until reassigned.


### Skybox loading and HDR storage

Skybox image decoding and ambient-color sampling run on the shared worker pool.
Unchanged cubemap descriptors are no longer parsed every frame; dependency stamps
are checked at most once a second per skybox renderer. Project/asset changes discard
stale jobs before they can upload. Crossfades wait for the requested textures to be
ready. Until an image loads, the ordinary tinted fallback remains visible.

For an HDR panorama, enable **Compress HDR (RGB9_E5)** in the Skybox Inspector.
This packs GPU HDR storage into 4 bytes/pixel instead of RGB16F's 6 (about one-third
less base-level texture memory). It trades color precision for memory/bandwidth,
particularly for very differently sized RGB channels. It is not BC6H and does not
rewrite/compress the original `.hdr` file on disk. Set `sky.hdrCompression` in Lua
for a Play-time change. Old projects default to uncompressed RGB16F. The setting
is included in scene/prefab serialization, undo and Stop rollback.

The shader reconstructs sky rays at the fullscreen triangle vertices rather than
doing inverse-matrix transforms for every fragment. Performance gains depend on
scene, image size, hardware and resolution; they have not been benchmarked here.

### Vehicle center of mass

Vehicle settings include `centerOfMassX`, `centerOfMassY`, and `centerOfMassZ`
(default zero, −10 to 10 local metres, scaled with the chassis Transform).
Negative Y lowers the center of mass. The physical collision surface stays in
place: this changes Jolt's mass frame rather than shifting the collider or
locking rotation. Changing an offset rebuilds the chassis body on the next
physics synchronization. Settings persist with the Vehicle, support multi-edit
and Lua, and default to zero in older projects. Anti-roll strength has been
removed; old saved keys are ignored. Wheel spring/damper suspension and
center-of-mass tuning remain available without a hard upright lock.

### Prefab studio lighting and animation view modes

Isolated prefab editing uses a render-only warm key light, cool fill light and
minimum ambient illumination in the Scene view. These are not ECS entities and
cannot enter the hierarchy, undo history, prefab asset, linked level instances
or a bake. Exiting prefab mode restores normal Scene lighting. Game views and
asset-preview lighting are unchanged. The Animation window's Dope Sheet and
Curves buttons are aligned to the right of its track/frame status row.
