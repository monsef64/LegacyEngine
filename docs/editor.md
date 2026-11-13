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
