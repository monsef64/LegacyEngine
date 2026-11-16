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
   **Existing assignments are not automatically invalidated after edits.**
   Clear level assignments removes references, not files: old atlases remain
   available to undo, other saved levels and backups. Remove unused bake folders
   manually only after confirming no saved scene references them.

## Scope and limits

- Direct diffuse lighting only: no indirect bounces, AO, baked specular or area
  light/soft-shadow sampling. The existing ambient term remains realtime.
- Baked static lights are not evaluated again on lightmapped receivers. Unbaked
  objects retain realtime lighting/shadows, subject to the existing light limits.
- Only eligible static geometry participates as a baked shadow caster. Animated
  objects/descendants, dynamic rigid bodies/descendants, characters, cameras,
  inactive objects, singular transforms and unsupported custom meshes are excluded.
  Static rigid bodies with zero mass are supported.
- Built-in solid primitives and baked/imported `.lemesh` models are supported.
  UVs are generated automatically as one isolated chart per triangle, without
  modifying source UVs, materials or imported files. This first layout trades
  packing efficiency and equal world-space texel density for predictable UVs.
- Charts include gutters; the shader samples level zero to avoid cross-chart
  mip bleeding. Large/high-poly models may need splitting or lower quality.
- Limits: 2048x2048 atlas per object, 32M atlas texels per bake, 500k triangles,
  and 128 static lights. Quality uses 8, 16 or 32 pixels per triangle chart,
  including gutters. Budget failures abort without replacing existing maps.
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
  check updated references in the open world and saved levels/prefabs.
- Cancel during loading/baking, switch levels/projects, edit the scene mid-bake,
  try oversized/corrupt geometry and a read-only output folder. Existing bake
  assignments should remain intact and rejected output should be cleaned up.
- Verify graphical runtime rendering and Windows MinGW-w64 manually. The Linux
  build validates compilation/linking, not shader execution or visual quality.
