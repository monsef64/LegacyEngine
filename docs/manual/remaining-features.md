# Remaining-feature manual verification

These are manual checks only. No automated tests were added or run.

- **Baking:** open the affected scene, start its static light bake, and inspect
  startup/progress diagnostics. Confirm a bake starts or reports the actionable
  reason it cannot. Confirm cancel and rebake leave the editor responsive.
- **Visibility:** select a Camera in Occlusion Culling, enable its Scene preview,
  and compare with its Game view. Check offscreen objects disappear without a
  bake; check occluded static objects disappear with valid baked data. Rotate the
  editor freecam without changing authored Active flags. Disable preview and
  confirm normal Scene rendering/overlays return. Include terrain, foliage,
  particles, clouds, rivers, lasers, mesh children and camera effects.
- **Batch Inspector:** select objects with shared and nonshared components. Edit
  mixed scalar, checkbox, vector-axis, path and object-reference values. Confirm
  only common components are offered, unrelated fields/axes remain distinct,
  invalid dependent values are rejected for the entire selection, and Undo/Redo
  restores the batch. Repeat during Play and verify Stop restores authored data.
- **UI/GUI:** load the RML/RCSS HUD with a UI component; confirm old saved GUI
  component data migrates. Run `data/examples/imgui/hud.lua` and `inventory.lua`.
  Check tables, menu/popup, combo, typed drag/drop and styles. Add image/imageButton,
  font scopes and custom drawing. Check malformed scope calls yield script errors
  without damaging later subscribers. Repeat with the separate Game window,
  paused/resumed Play, focus changes, Stop and a second Play session.
- **Texture quality:** compare Full/Half/Quarter/Eighth on a detailed material
  and normal map. Call setTextureQuality from onStart and during Play. Confirm
  cookies and GUI images remain full resolution, Stop restores project quality,
  saved settings persist, and rapid changes/project switches do not reuse stale
  images. Measure RAM/VRAM and frame time rather than assuming improvements.
- **Packing:** import multi-material OBJ, skinned multi-mesh glTF/GLB and GoldSrc
  MDL with embedded/external textures and sequence groups. A single-model import
  should leave one model asset. Expand it in grid and list, drag meshes/materials/
  clips, edit a packed material, instantiate, animate, cross-fade, save/reload,
  copy/rename/move and Save As. Confirm internal references follow the owner and
  old separate-file models still work. Directory import intentionally preserves
  copied source files and dependencies as well as converted model containers.
- **Performance/platforms:** inspect terrain from large distances and with the
  camera facing away; verify hills, grass and collision remain correct. Check
  process RAM/normalized CPU and supported-driver VRAM counters. Repeat the core
  scenarios on Windows MinGW-w64/OpenGL; the sandbox build is Linux only.

Interactive verification, affected-scene baking and MinGW compilation have not
been performed in the sandbox.
