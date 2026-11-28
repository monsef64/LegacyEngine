# Mesh import, folder operations and mouse confinement

## Mesh storage and imports

New or rewritten `.mesh`, `.model`, `.lemesh` and `.terrain` assets are stored as
uncompressed compact JSON in the editor. Existing JSON, `LEMZ0001` zlib,
`LEMZ0002` Zstandard and `TERZ0001` terrain files remain readable; saving/reimporting
rewrites only the affected asset. Schema and paths are unchanged. Expanded and
on-disk byte limits remain enforced. Zstandard compression is performed on export
package chunks, not while editing.

The Assets import action and dropped files use one background worker. That worker
owns its AssetManager and Assimp importer; it does not access ImGui, OpenGL, the
scene world or editor caches. Results/cache invalidation are applied on the main
thread. Additional imports are rejected while a job runs rather than starting
unbounded jobs. Asset edits, project save/switch/autosave and application exit are
blocked until the import finishes, preventing operations on half-written imports.
Conversion still uses CPU (typically one busy core); threading keeps that work off
the UI thread, and is not a promise of a fixed CPU-percentage cap.

Manual checks (not automated tests):
- Import small and large static/skeletal meshes and folders containing source models.
  Verify the UI stays responsive, the busy message is shown, and failures are visible.
- Verify imported materials, animation clips, bounds, colliders and thumbnails load.
- Open old JSON meshes and new compressed meshes in the same project. Save/reload.
- Compare file sizes with equivalent JSON data; do not assume a fixed compression ratio.
- Try corrupt/truncated mesh files; verify errors rather than excessive allocation.
- Try save, autosave, another import and exit while converting. Verify only one job runs
  and no project snapshot is made from an in-progress conversion.

## Folder actions

In Assets, use Copy / Move (Cut) from the context menu or Ctrl+C / Ctrl+X. Navigate
to the destination folder and use Ctrl+V or the background Paste command. The folder
context menu also has Paste into folder. Name conflicts get a numbered suffix;
pasting a folder into itself or its descendants is rejected. Copying beside the
original creates a distinct folder rather than modifying the original.

Moves update live asset references and known structured asset references. Copies
remap internal structured asset references inside the copied hierarchy; script
source text is not rewritten. An open animation's asset/model paths follow moves.
Move to Trash and Delete now accept folders. Ctrl+Z restores a trashed folder using
editor history. On Windows, undoable trash is application-managed under
`%LOCALAPPDATA%/LegacyEngine/Trash` (not the Windows Recycle Bin). Linux/macOS retain
their existing desktop Trash integration.

- Copy and move folders containing models, materials, textures, prefabs and animation
  clips. Verify originals stay unchanged after copying and references follow moves.
- Duplicate a folder in its current parent and verify it gets a suffix.
- Reject self/descendant paste and paths escaping Assets; keep existing destinations.
- Trash/undo folders, including a folder on a different filesystem where applicable.
- Save an open dirty animation after moving its containing folder; verify the old
  folder is not recreated by saving to a stale path.

## Locked mouse

Relative mouse mode disables SDL's whole-window centre confinement so a docked
Game image can supply its own rectangle (important on Windows). Rectangles are
clamped to window coordinates, reapplied after resizing and cleared on unlock.
A cursor outside a newly locked viewport is moved inside it. Invalid/unsupported
confinement releases capture instead of reporting a lock that cannot be enforced.

- Lock/unlock in a docked Game viewport placed away from the editor-window centre.
- Move aggressively against all four edges; the pointer must not reach other panels.
- Resize/reposition the Game panel; test HiDPI scaling, separate Game window,
  focus loss, Escape, collapsed Game view, Play stop and free-look in Scene view.
- Repeat on Windows MinGW-w64 and a Linux graphical backend. Headless build success
  alone does not validate OS-level pointer confinement or GPU rendering.

## Large mesh/model JSON

The generic SIMD parser no longer applies its default 16 MiB ceiling to every file.
File readers pass their own configured limit: mesh/model JSON can use the existing
256 MiB mesh budget, matching compressed-mesh limits. Other limits (vertex count,
expanded size, hierarchy depth and source import size) remain intentional safeguards.
Manually open a legacy JSON mesh larger than 16 MiB but below 256 MiB, including a
large skeletal model document, and verify rendering/import settings. Check malformed
and over-budget input still produces an error.

## VS Code diagnostics

- Rebuild/install extension 0.1.1. Open `.ls` files and introduce a missing `end`,
  unmatched delimiter, or invalid expression. Verify squiggles and Problems entries.
- Correct the error, save, close/reopen, and edit an untitled LegacyScript document.
  Verify diagnostics update and stale entries disappear.
- Verify `global` declarations work; malformed declarations,
  duplicate field names and unknown types should produce useful errors.
- Put export examples inside quoted/long strings and comments; these must remain text.
- Verify LuaJIT syntax is accepted and unsupported Lua 5.4 local attributes are diagnosed.
- Confirm engine runtime errors remain in its script console rather than being
  misleadingly reported as statically detectable VS Code errors.

## Asset workflow improvements

Editor mesh saves are uncompressed; old compressed assets remain readable. Static
Zstandard 1.5.7 remains linked for backward-compatible reading and export packaging,
including MinGW builds; no Zstandard DLL installation is needed.

- Material thumbnails use a stable untextured sphere after their texture is
  deleted, rather than alternating stale/empty previews. Restoring the texture
  refreshes its material thumbnail on the normal one-second metadata check.
- The browser enumerates directory contents each frame. Missing selections and
  deleted current folders are cleared; asset mutations/undo invalidate thumbnails.
- Single-click selects models; **double-click** opens Model Preview. Right-drag
  in Model Preview orbits its framed model center; wheel zoom preserves that pivot.
- The browser header is compact, with no drop-file instructions or persistent
  error paragraphs. Errors appear as red tooltips while hovering Assets, for five
  seconds. Check grid and list layouts in a short/narrow dock.
- Select any file type or folder and press **Delete** with Assets focused (outside
  text entry/popups). Undo/Redo should restore/delete it. Filesystem-only history
  actions reuse the scene snapshot instead of cloning the world again.
- Deleting an imported root model also trashes its generated data directory;
  Undo restores both. New roots record `dataFolder`; legacy bundles infer their
  sibling `<model>_data*` directory from part paths. Ordinary mesh parts or unrelated
  folders are not recursively deleted. Copies of individual models may share data;
  do not delete an owner model if those copies still need its generated directory.
- Trash uses filesystem moves on the same volume (not per-file recursive deletion).
  Cross-volume system Trash moves still require copying and may take longer.
- **Project → Open in VS Code** opens the project folder using the VS Code URL handler.
  In external-script mode, double-clicking a script launches a single VS Code request
  with the project folder and `--goto` script; no shell command interpolation is used.
  The `code` command must be in PATH on Linux/macOS; standard per-user/system Windows
  Code.exe installations are detected. Custom Windows installs require Code.exe in PATH.
- Scene RMB freelook is separate from Game cursor locking. Check RMB rotation,
  WASD/QE motion, Shift boost, releasing RMB, focus loss, switching Play/Edit, and
  docked/separate Game views. No repeated cursor warps should perturb relative motion.

## Accessing owner components from a component

Any **valid** component can call shared owner component/hierarchy/transform methods,
for example:

```lua
local fps = camera.transform:addComponent(FPSCamera)
local rigidBody = fps:getComponent(RigidBody)
local animation = fps.animation
local parentTransform = fps:getComponentInParent(Transform)
```

Use `.` for property access and `:` for method calls. `camera:transform:addComponent`
is not valid Lua/LegacyScript syntax; use `camera.transform:addComponent(FPSCamera)`.
Lower-camel component aliases return nil when absent (for example `meshRenderer`,
`rigidBody`, `fpsCamera`, `animation`); `gameObject` returns the owner. Deleted or removed
component handles still report an error rather than accessing invalid memory.
Component-specific methods retain their existing behavior.

### Manual verification

Import a model and inspect its uncompressed JSON file, then reopen both old/new formats and
play skeletal clips. Delete/undo its root and confirm the data folder follows it.
Delete a material's texture externally and through Delete, then restore it; the icon
must not flicker. Delete each supported/unknown file type, a folder and the open
folder from outside the editor. Verify grid/list refresh and history behavior.
Verify VS Code opens the right project even when another project window is active,
including paths with spaces, quotes and non-ASCII characters. Exercise component
access in live scripts, removed components and destroyed owners. Interactive and
Windows-specific checks must be performed on target hardware; no automated tests
are added or run for these changes.

## Expandable imported models and icon sizing

New imports no longer create `<model>_data` folders. Generated meshes, materials,
textures and animation wrappers are collision-safely namespaced **files beside the
root model**, which records a `subAssets` manifest and original display names.
The browser groups those files under the model instead of displaying them as
unrelated top-level items. Use the small triangle at the upper-right of a model
item to expand/collapse it; a new import starts expanded. List view indents child
items; grid view places them after the root with a subtle grouped outline.
Double-click remains the action to open Model Preview; toggling expansion does
not open it. Expanded children retain normal selection, drag, preview and Delete
behavior. Search includes child display names even for collapsed models.

Existing `_data` imports are left unchanged (no automatic destructive migration).
Reimport to use the new flat/grouped layout. Deleting a new root trashes all its
manifest-owned generated files and Undo restores the group; legacy model deletion
still handles its generated directory. Physical files remain individually addressable
by scripts and scenes. Namespacing prevents overwriting existing project assets.

The **Icons** slider sits beside **Spacing** in the browser footer. It scales icons
and row/tile height from 20–96 pixels in both list/grid views; spacing remains a
separate grid-only control. Verify small/large icons, changing columns, long names,
folder navigation, arrow clicks, searching original mesh/material/animation names,
model preview on double-click, dragging child meshes and root deletion/undo.

Every Inspector component header context menu now includes **Reset**. It restores
that component's default values without removing the component or changing its
siblings. Transform resets **local** position/rotation/scale, so parenting is
preserved. MeshRenderer returns to the same cube defaults as Add Component.
Normal scene history records resets for Undo/Redo in Edit mode. Check collapsed
and expanded headers, parented objects, component references/assets, lights,
physics, scripts and Animation. Reset clears assigned assets/state as appropriate;
it is not the Animation playback toolbar's pose-reset command.
