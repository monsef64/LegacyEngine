# Manual verification: terrain jumping, materials, exports and Scene tools

No automated tests are added or run for these changes. Use a desktop OpenGL build;
repeat exported-player checks on Linux and Windows MinGW-w64.

## Terrain grounding

- Place a CharacterController with a dynamic capsule Collider on a flat terrain
  with no explicit Collider component. Walk, stop and jump repeatedly. Repeat on
  moderate hills and at terrain chunk edges, in editor Play and exported player.
- Check ordinary box/mesh grounds still permit jumping. Triggers, excluded
  collision layers, steep slopes and airborne jump requests must not act as ground.
- Stop Play and verify authored transforms and controller settings are restored.

## Inspector

- Select valid and packed model/material/script assets and inspect their previews.
- Rename/delete an assigned asset externally while its Inspector is open. No raw
  filesystem exception or absolute host path should appear; an actionable missing
  asset/preview message is allowed. Restore/reassign the asset and verify recovery.
- Inspect broken material/script/model references and Bone Attachment sources.
  Existing parse/validation messages should remain visible (not silently hidden).

## Editor navigation and snapping

- Click the orientation indicator's center cube: Persp changes to Ortho and back
  without moving the pivot or selected objects. Axis endpoints still align views;
  dragging the gizmo orbits and does not toggle the projection on release.
- In Ortho, select objects near every corner, drag transform gizmos, pan, zoom,
  frame selection, and use collider/terrain editing. Straight up/down axis views
  must stay stable. Game Camera projection must remain unchanged.
- Rotate a selected object while holding Ctrl: default snapping is 15 degrees.
  Change Snap (deg), test local/world rings, multi-selection, origin/center pivot,
  undo/redo, and free rotation with Ctrl released. Move/Scale snap values remain
  independent of rotation. Save/reload and Stop Play preserve authored rotations.

## Stochastic material

- Create a material and select Stochastic Texture Sampling. Apply tiled albedo and
  a directional normal map to a plane and arbitrary meshes. Compare repetition
  with Diffuse Bumped; check normal orientation under a moving light.
- Adjust tiling, normal strength, color, specular and lights. Test negative tiling,
  absent textures, shadows/lightmaps, packed material members, script shader changes,
  animated meshes and save/reload. Existing four material modes must be unchanged.
- Check terrain rendering still compiles/works after the shared lit shader change.

## Export closure

- Reference a material -> texture/normal, prefab -> model -> embedded material,
  terrain -> layers/foliage/lightmap, audio and literal script asset paths.
  Leave unrelated images/models/audio/levels in Assets and export. Exercise all
  registered levels and runtime objects/UI in a clean exported player without the
  source project present. Unused project files should not be packaged.
- Exercise literal script asset paths, nested dependencies and cycles. Packed
  containers are intentionally included whole, not partially repacked.
- Add runtime-constructed paths to Settings/ExportAssets.json and verify both file
  and directory inclusions, recursive dependencies, spaces in paths, and rejection
  of missing entries and referenced symlinks. Unused symlinks must not be traversed.
- Check Windows/Linux native exports, encryption, chunk settings, icons and
  project Settings still work. Existing output folders must never be overwritten.
