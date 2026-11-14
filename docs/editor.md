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
- **Settings → Editor Settings:** Legacy Dark, Grey, or Unity 5 Classic theme and internal/external code editor.
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

**Unity 5 Classic** uses light-grey panels, blue selections, bordered controls and
top-lit glossy buttons inspired by the older editor. Select it under
**Settings > Editor Settings > Theme**; the choice persists between launches.

## Reference layout and orientation gizmo

The default workspace follows the classic editor arrangement: Hierarchy at the
upper left, Scene and Game as central tabs, a full-height Inspector on the right,
and Assets/Console/Script Editor tabs across the bottom left and center. Assets
is initially selected. **Windows > Reset layout** restores this arrangement.
The updated default uses a new dockspace identifier so older saved layouts do
not hide the change; subsequent custom arrangements are still saved normally.

Choose **Settings > Editor Settings > Theme > Unity Dark Classic** for the
reference-inspired charcoal panels, grey glossy buttons and muted blue selections.
The earlier light **Unity 5 Classic** theme remains available.

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
larger 240px preview on hover. These are lightweight approximate previews, not a
full renderer/normal-map preview.

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
Multi-selection does not apply Inspector field edits or gizmo transforms to the group.

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
and left-aligned in list view. Hover an item for three seconds to see its
full name and type (and thumbnail where available). Paths on disk are unchanged.
The default dock layout places Prefab Window below Hierarchy. When the separate
Game window is open, the embedded Game viewport is not displayed or rendered.

The Assets spacing slider sits left of the view-mode buttons. It changes grid
cell spacing only and is disabled in list view. Inspector's header displays the
object name, layer and unlabeled Active checkbox on a single row. Scene editing
always uses freecam controls; the old toolbar toggle has been removed (Game
FPSCamera movement settings are unchanged). Layers and Rendering are available
directly from Project, without a Project Settings submenu.

### Creating scripts in Script Explorer

Use **New Script** in Script Editor's explorer to create and open an empty `.ls`
asset beside the current script, or in Assets when no script is open. Right-click
an explorer folder and choose **New LegacyScript (.ls)** to create one there;
right-click empty explorer space to create one at the Assets root. Filenames
start at `NewScript.ls` and use numbered suffixes to avoid overwriting files.
The current document is saved before switching. The tree refreshes automatically.

See [static 3D model import](models.md) for Assimp format choices, baked model
assets, model placement and the current static-only limitations.

The Inspector no longer has a Parent dropdown; drag objects in Hierarchy to
change their parent or use its unparent action. For the separate Game window,
press **F11** or **Alt+Enter** while it has focus to toggle desktop fullscreen.
The editor toolbar also provides **Fullscreen Game / Windowed Game** while that
window is open. These shortcuts are reserved for window control and are not
forwarded to gameplay. Repeated key-down events do not toggle repeatedly.

Asset import now offers **Files / Models** and **Directory (full structure)**.
Imports and OS drops go into the current Assets folder. Clicking a model opens
its shared import-scale settings in Inspector; see [models](models.md) for
multiple mesh/material support and convex collision geometry. Instantiation
creates a named empty model root with mesh children.

After deselecting an object, **Alt+left-drag** still orbits its last remembered
position. Right-drag free-look and the existing camera navigation remain available.

Inspector's **Create Material** button and material-slot context action first
open a naming popup. Enter a filename (the `.mat` suffix is optional), then
choose **Create** or press Enter. **Cancel** / Escape creates nothing. Invalid
filenames and existing names are rejected without overwriting assets. Successful
creation saves the material in Assets and assigns it to the MeshRenderer.
