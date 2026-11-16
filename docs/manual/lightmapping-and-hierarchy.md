# Static objects, lightmapping and hierarchy copying

## Workflow

1. In Edit mode, mark each receiver/occluder **Static** in the Inspector header.
   Static is a saved Transform flag, not a physics lock; do not animate or move
   baked objects. Mark the lights to bake Static as well. Unmarked lights stay
   realtime, including on lightmapped objects.
2. Open **Windows > Lighting**. The window shows the current level,
   render-object total, eligible/excluded counts, atlas assignments, static-light
   count and eligible object list. Imported model triangle/atlas budgets are
   validated when the bake loads geometry; eligibility alone does not guarantee
   that a model fits those budgets.
3. Choose Draft/Medium/High chart quality and shadow ray bias, then **Bake level**.
   A bounded background job loads geometry, builds a ray-tracing BVH, generates
   UV charts and bakes direct diffuse lighting, hard shadows and optional diffuse
   GI. Loading/building and baking phases, texel progress, elapsed time and Cancel
   are shown.
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

- Direct diffuse lighting plus optional static diffuse GI (see the GI section
  below). No baked specular or area-light/soft-shadow sampling. The existing
  ambient term remains realtime.
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

- **Rebake once after this update.** Older manifests lack current GI/material metadata and intentionally use
  realtime fallback. Version-3 `bake.json`
  must accompany its atlas files when saving/distributing a level.
- Bake a static cube above a static 16m plane with a static directional light.
  Move the cube: both receivers must switch to realtime, with no old shadow
  attached to the cube/floor. Repeat rotation, scale, delete, Static toggle,
  parent movement, activity toggle and light color/intensity/direction edits.
- Undo back to the exact baked static scene: the matching atlas can be reused.
  Save/reload without edits: matching maps should remain valid. Check stale and
  valid counts in the Lighting window.
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

## Diffuse global illumination (LightmapGI-style baking)

This is LegacyEngine's native CPU baker, not Godot's `LightmapGI` node or a
Godot dependency. It now traces static diffuse indirect transport in addition
to the existing direct lighting bake.

- **Indirect bounces:** 0 (direct only) through 4; default 2.
- **GI samples / texel:** 4–256; default 32. More samples reduce Monte Carlo
  noise but increase bake time. Increase samples before increasing bounce count.
- **GI ray distance:** 1–1000 metres per segment; default 50. Misses contribute
  black: the baker does not integrate HDR sky/environment or emissive materials.
- **Denoise indirect lighting:** optional chart-local 3x3 box filter, enabled by
  default. This reduces noise, but can blur fine indirect detail; it is not an
  advanced feature-aware denoiser.
- Each hit uses cosine-weighted hemisphere sampling, the nearest BVH intersection,
  material-tint diffuse reflectance, and shadow-tested static direct illumination.
  Reflected light carries surface tint to other surfaces (color bleeding).
  Reflectance clamps to 0–0.95 per channel for bounded diffuse transport. Material
  texture pixels, normal maps, transparent transport, specular/refraction and
  emissive lighting are not sampled. Geometry remains opaque and one-sided for
  reflection; back-face hits stop a path rather than leaking through the mesh.
- The bake retains the existing texel/triangle/atlas/light caps and adds a 500M
  potential path-segment cap (`texels * samples * bounces`). This bounds admission,
  not wall-clock duration; scenes with many lights can still take a long time.
  Progress now counts atlas texels on real triangle charts (not triangles).
- Direct and indirect lighting are stored in separate TGA atlases. Indirect is
  an extra runtime texture sample, not realtime ray tracing. The existing mixed
  realtime shadow mask only affects direct light; it does not erase baked bounce
  light. Dynamic objects do not contribute baked indirect light or receive GI
  probes. Runtime texture/cache memory use grows with the companion atlases.
- **Rebake once for this update:** version-3 manifests list direct and optional
  `.indirect.tga` companions, settings, and the material-aware static fingerprint.
  Versions 1/2 intentionally fall back to realtime lighting. Keep `bake.json` and
  both atlas files together. A pending/missing indirect companion temporarily
  omits indirect light while retaining available direct/realtime lighting.
- In-memory material tint/override edits and shared material color changes now
  affect bake validity. Shared material files are checked through a 250ms cache.
  Imported model and texture file-content changes still require manual rebaking.
  Settings remain session-local and are recorded in the manifest, not automatically
  restored per level. Existing ambient lighting remains present at runtime.

### Manual GI checks (no automated tests added or run)

1. Create a room with a red static wall and white static floor. Illuminate the
   wall with a static light; compare bounces 0, 1 and 2. Verify bounce illumination
   and red color bleeding on nearby floor areas without double-added direct light.
2. Compare 8/32/128 samples on the same scene, denoise on/off. Check indirect noise,
   charts/seams, bake time, progress, ray-distance truncation and memory use.
3. Move a realtime caster across a baked floor: direct shadows should update,
   while baked indirect illumination should not disappear with the shadow mask.
4. Edit static material tint, transform, light or active/static flags, then undo.
   Verify stale fallback and restoration of the matching bake. Edit a shared
   material's color and allow the file cache interval to elapse.
5. Save/reload the level, verify both atlases stream in, and verify no GI rays run
   during rendering. Remove/restore the indirect companion and check safe behavior.
6. Cancel during a noisy/high-sample bake, switch levels/projects, trigger a budget
   error and try an unwritable output folder. Existing assignments should survive
   rejected output and unfinished folders should be cleaned up.
7. Visually verify shader execution and Windows MinGW-w64 separately. Compile/link
   success alone does not establish image quality or runtime performance.
