# Mesh import, folder operations and mouse confinement

## Mesh storage and imports

New or rewritten `.lemesh` files use a `LEMZ0001` binary envelope: an explicit
little-endian expanded byte count followed by zlib-compressed CBOR. Existing
JSON meshes remain readable without conversion. Mesh schema versions and paths
are unchanged. Both packed and expanded data are bounded by the mesh file limit;
corrupt compression is rejected. Meshes should be edited through the editor, not
as text. Fast compression (level 1) avoids expensive maximum-compression work.

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
- Verify `@export` and `---@export` declarations work; malformed declarations,
  duplicate field names and unknown types should produce useful errors.
- Put export examples inside quoted/long strings and comments; these must remain text.
- Verify valid Lua 5.4 local attributes do not produce spurious syntax errors.
- Confirm engine runtime errors remain in its script console rather than being
  misleadingly reported as statically detectable VS Code errors.
