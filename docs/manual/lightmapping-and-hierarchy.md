# Static objects, lightmapping and hierarchy copying

## Workflow

1. In Edit mode, mark each receiver/occluder **Static** in the Inspector header.
   Static is a saved Transform flag, not a physics lock; do not animate or move
   baked objects. Mark the lights to bake Static as well. Unmarked lights stay
   realtime, including on lightmapped objects.
2. Open **Windows > Lightmapping**. The window shows the current level,
   render-object total, eligible/excluded counts, atlas assignments, static-light
   count and eligible object list. Imported model triangle/atlas budgets are
   validated when the bake loads geometry; eligibility alone does not guarantee
   that a model fits those budgets.
3. Choose Draft/Medium/High chart quality and shadow ray bias, then **Bake level**.
   A bounded background job loads geometry, builds a ray-tracing BVH, generates
   UV charts and bakes direct diffuse lighting and hard shadows. Loading/building
   and baking phases, triangle progress, elapsed time and Cancel are shown.
4. Keep editing changes out of a running bake. A result is discarded if the
   scene signature, project, level or import revision differs at completion, or
   if completion occurs outside an editable scene. Closing the window does not
   stop the job; Cancel does. Changing project/level cancels a pending job.
5. Save the level after completion. Atlases and a settings/statistics manifest
   are written into a uniquely named `Assets/Lightmaps/Bake-*` folder. Include
   those assets when distributing the level. Failed/cancelled/unaccepted output
   is cleaned up. Runtime texture uploads use the existing asynchronous cache.
6. Rebake after changing static lights, transforms, model imports or geometry.
   New bakes carry a static-scene fingerprint. Moving/rotating/scaling a static
   mesh, changing static light settings, changing active/static flags, or adding/
   removing static meshes/lights makes all affected scene bake assignments stale.
   The renderer ignores stale maps and restores realtime lighting/shadows; maps
   remain assigned so undoing the edit can recover the matching bake. Imported
   file-content changes still require a manual rebake (not fingerprinted).
   Clear level assignments removes references, not files: old atlases remain
   available to undo, other saved levels and backups. Remove unused bake folders
   manually only after confirming no saved scene references them.

## Scope and limits

- Direct diffuse lighting only: no indirect bounces, AO, baked specular or area
  light/soft-shadow sampling. The existing ambient term remains realtime.
- Baked diffuse is not added a second time as realtime diffuse. Baked receivers
  also receive an irradiance-weighted realtime shadow mask and realtime specular
  from visible static lights, so moving realtime casters can shadow a baked floor.
  The mask uses the existing realtime shadow/light budgets. It is approximate
  for multiple overlapping static lights and may darken soft shadow transitions.
  Unbaked/stale receivers retain normal realtime lighting and shadows.
- Only eligible static geometry participates as a baked shadow caster. Animated
  objects/descendants, dynamic rigid bodies/descendants, characters, cameras,
  inactive objects, singular transforms and unsupported custom meshes are excluded.
  Static rigid bodies with zero mass are supported.
- Built-in solid primitives and baked/imported `.lemesh` models are supported.
  UVs are generated automatically as one isolated chart per triangle, without
  modifying source UVs, materials or imported files. This first layout trades
  packing efficiency for predictable UVs. Chart size now grows with the longest
  world-space triangle edge, rather than assigning a large floor only 16 pixels.
  Each object's largest edge sets its chart size; exact density is not guaranteed
  for small triangles within mixed-size meshes or when the atlas cap is reached.
- Charts include gutters; the shader samples level zero to avoid cross-chart
  mip bleeding. Large/high-poly models may need splitting or lower quality.
- Limits: 2048x2048 atlas per object, 32M atlas texels per bake, 500k triangles,
  and 128 static lights. Quality targets 8, 16 or 32 texels per metre, with
  minimum chart sizes of 8/16/32 pixels and two-texel gutters. Per-object atlas
  caps may reduce density. Budget failures abort without replacing existing maps.
- Atlas TGA RGB stores `sqrt(irradiance / 16)`, decoded by the shader; irradiance
  above 16 clamps. Mesh normals are used, not material normal maps. Transparent
  geometry casts opaque shadows in this initial implementation.
- Static flags/atlas references survive level and prefab save/load and undo/redo.
  Older scenes default to non-static/no atlas. Legacy text schema is version 21;
  optional JSON fields retain compatibility with existing supported scenes.
- Bake settings live in the window for the editor session and are recorded with
  each output manifest; they are not automatically restored per level.

## Manual checks (no automated tests added or run)

- Export an `Animation` reference in a LegacyScript; drag the Animation component
  header and then its GameObject onto the export. Verify both assignments resolve
  and survive save/reload.
- Duplicate a parent with children/grandchildren. Verify complete hierarchy,
  world/local transforms, activity, Static flags, Animation components and other
  component values. Child names stay unchanged; root receives a copy suffix.
- Copy the same hierarchy, delete the source and paste twice. Verify clipboard
  independence and separate trees. Internal script references and camera targets
  must point into the corresponding new tree, not the source or another paste.
  Duplicate keeps external references; clipboard transfer clears references
  outside the copied tree rather than binding unrelated entity IDs.
- Undo/redo duplicate, paste, Static toggles, bake assignment and clear operations.
- Compare Inspector with the reference: Active/name/Static on the first row,
  Layer below, compact labeled XYZ rows, and no Tags section.
- Bake a Static plane/cube with a Static directional light; check shadows, UV
  orientation/seams, color and nonuniform/mirrored scales. Repeat with point and
  spot lights and an imported model. Compare with a realtime-only light.
- Save/reload the level and a prefab. Check atlas assignments and old scene
  compatibility. Rename/move an atlas or bake folder through the Asset Browser;
  check updated references in the open world and saved levels/prefabs. If the
  bake manifest's atlas list no longer matches, expect safe realtime fallback
  until the manifest is updated or the level is rebaked.
- Cancel during loading/baking, switch levels/projects, edit the scene mid-bake,
  try oversized/corrupt geometry and a read-only output folder. Existing bake
  assignments should remain intact and rejected output should be cleaned up.
- Verify graphical runtime rendering and Windows MinGW-w64 manually. The Linux
  build validates compilation/linking, not shader execution or visual quality.

## Regression checks for moving shadows / ground lighting

- **Rebake once after this update.** Version-1 manifests have no static-state
  fingerprint and intentionally use realtime fallback. Version-2 `bake.json`
  must accompany its atlas files when saving/distributing a level.
- Bake a static cube above a static 16m plane with a static directional light.
  Move the cube: both receivers must switch to realtime, with no old shadow
  attached to the cube/floor. Repeat rotation, scale, delete, Static toggle,
  parent movement, activity toggle and light color/intensity/direction edits.
- Undo back to the exact baked static scene: the matching atlas can be reused.
  Save/reload without edits: matching maps should remain valid. Check stale and
  valid counts in the Lightmapping window.
- Bake the plane while a separate non-static cube is present, then move that
  cube. The floor must retain baked static lighting AND show its realtime shadow
  when the light has an available realtime shadow map. Check that static direct
  light is not double-added. Realtime shadow distance/atlas limits still apply.
- Compare floor atlases at each quality. Small-object shadows should no longer
  disappear simply because a large floor consists of only two triangles.
- Temporarily remove/corrupt `bake.json` or an atlas, then restore it. Confirm
  realtime fallback (manifest cache rechecks within one second) rather than an
  unlit floor. Also inspect from above; planes remain intentionally one-sided.
- No automated tests were added/run. Full Linux build validation does not prove
  graphical behavior; visually verify shader execution and Windows separately.
