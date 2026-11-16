# Editor/input and bake follow-up — manual verification

## Status

Implemented changes compile/link on Linux with headless SDL configuration.
No automated tests were added or run. Windows MinGW-w64, rendered output and
physical cursor confinement have not been validated here.

Laser components and spotlight cookies are implemented; see
`lasers-and-cookies.md` for usage, limits and manual checks.

## Input

- In docked Game, call `Input.lockCursor()` from Lua. Move vigorously across all
  four boundaries, resize/redock, unlock and relock. Check FPS look deltas and Lua
  locked/visible queries. Repeat in detached Game and fullscreen.
- Hold right mouse in Scene and move vigorously at every viewport edge. Release
  outside the original viewport, Alt+Tab, open a dialog and hide Scene: no stuck
  cursor or jumping camera on return. Check orbit/pan modifier shortcuts too.
- SDL relative mode alone cannot confine to a docked subrectangle. Windows/X11
  now use a hidden, recentered absolute cursor with synthetic-warp filtering.
  Wayland retains native relative mode because clients cannot freely warp the
  pointer. Actual compositor behavior must be checked on target systems.

## Assets and editor

- Trash a nested folder, undo, redo and restore; verify contents and folder
  metadata. The Linux regular-file-only check now accepts directories.
- Create Particles from hierarchy/Scene menus. Move using Transform values and
  Scene gizmos during preview. Playback-panel interaction must not select the
  Scene behind it, and dragging unrelated Inspector fields must not block gizmos.
- Narrow Inspector below 340 pixels: vector axes stack vertically so all X/Y/Z
  inputs remain editable. Widen again and verify inline layout.
- Verify Add Component > Collider, Physics > Rigidbody/Character Controller,
  and Audio > Audio Source/Audio Listener, including Reset and undo.
- Open/save projects repeatedly via relative, absolute and alternate spelling
  paths. File > Open Recent should list at most 12 unique canonical paths, newest
  first, and survive restart. Windows path comparison ignores case.
- With dirty project and/or script/animation edits, open picker or recent project:
  test ImGui Save and continue, Discard, Cancel and Escape. Repeat on Quit/Alt+F4.
  Test saving a previously unnamed project, picker cancellation and save failures.

## Bakes

- Bake dense geometry and large world-space triangles. Chart density is reduced
  to fit a per-object atlas within 2048 pixels rather than rejecting oversized
  density. Small charts use an adaptive gutter shared by baker and vertex shader.
- Global 32M texel, 500k triangle and GI work budgets still apply. This does not
  remove intentional memory/time limits. Very small charts lose lighting detail.
- Bake a static occluder over a static floor, move it and verify the old shadow
  remains on the floor until rebake. Save/reload and repeat. Bake assignments are
  now persistent snapshots (including old v3 bakes), not invalidated by scene-state
  mismatches. Light/material/geometry edits likewise need an explicit rebake.
  The moved receiver's own baked lighting moves with its UVs and may look stale.
- Rebake and verify updated direct and indirect shadows in both Scene and Game.
