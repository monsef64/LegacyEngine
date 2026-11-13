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
with Transform scale; it has no RigidBody by default. **Create → Skybox** creates
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
