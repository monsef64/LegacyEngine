# Manual verification: scripts, animation, preview caches and game export

No automated tests are required for this checklist. Run on a graphics-capable
Linux desktop and Windows MinGW-w64 build; record platform/build and results.

## Named components and persistence
- Create Health.ls and a second script using the named-component guide. Verify
  getScript and getComponent, same/other owners, function results and field writes.
- Verify missing/disabled scripts return nil on retrieval, retained proxies fail
  safely after disabling/deleting owners, and Stop/Play resets all runtime edits.
- Verify locals/tables/handles/reserved members are rejected; a failing script
  does not crash the editor. Check Console includes source names/errors.
- Try Health.lua, health.ls in another folder, batch imports and folder renames
  containing collisions. No silent overwrites or ambiguous script selection.
- Open an older project with duplicate names; verify a useful error, rename the
  duplicates and repair attachments, save/reopen and confirm runtime startup.
- Verify two scripts can resolve each other's globals during onStart (not file
  scope); confirm cycles are rejected without hanging.

## Animation
- Add two named function events, one on the animated object and one child. Save,
  reopen and play; check callbacks fire at their timeline crossings and on loops.
- Scrub/seek without playback: no callbacks. Disable/remove the script or child:
  no crash. Stop restores the pre-play scene; undo clip edits as usual.

## Preview warm-up
- Use a fresh default cache with textures, static/animated models, embedded
  subassets, mesh, material, prefab, terrain, cubemap, script, audio and UI assets.
- Import a batch and leave the browser closed: let focused editor frames populate
  cached icons and hover previews, then open grid/list/pickers and hover assets.
- Reopen the project: verify caches reuse disk data, image orientation is correct,
  icons are 128px and hover previews retain detail. Verify badges for unsupported
  types and diagnostics for corrupt assets without an endless warm-up stall.
- Modify an imported dependency and reimport; verify both icon and hover refresh.
  Switch projects during warm-up, unfocus/refocus, delete assets during import,
  and confirm no stale cross-project image, GL corruption or UI freeze.

## Standalone export (repeat on both native platforms)
- Build editor and player; open File > Export Game. Reject missing required
  fields, malformed version, wrong icon type, editor-as-template, already exported
  template, and existing output destination without destroying files.
- Export a scene with scripts, physics, animation events, UI, audio, prefabs,
  materials and registered level transitions. Also export a stored starting level.
- Move the entire export folder outside the project and launch with the source
  project unavailable. Confirm camera/input, fixed updates, triggers/collisions,
  audio events, animation callbacks, level changes, F11,
  focus loss/resume and normal close. No editor windows should appear.
- Windows: inspect EXE Properties (name/company/version/copyright) and icon;
  launch on a clean compatible machine to check copied DLL dependencies.
- Linux: verify executable permissions, window icon and runtime system library
  requirements on a second compatible installation.
- Confirm GameName_Data contains encrypted chunks/manifest plus license notices,
  not loose project assets. Tamper with a copied chunk or manifest: launch must
  fail with authentication error, not run partial data. Restore and relaunch.
- Verify temporary extraction cleanup after normal exit. Confirm the documented
  limitation that forced termination may leave temporary plaintext files.

Mesh combining is intentionally out of scope.
