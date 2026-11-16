# Terrain: manual verification

No automated tests are included or run for this feature.

## Workflow

1. Create > Terrain, or Add Component > Terrain on an object with a Transform.
2. Select the object and open **Terrain tools** from its Inspector. Enable **Scene brush**.
3. Left-drag in Scene using Raise, Lower, Smooth, Flatten or Paint. Brush radius,
   strength and falloff control the stroke. Flatten uses the entered local height;
   Paint blends toward the selected layer (0–3). Alt navigation/right-drag still
   control the editor camera. Disable the brush to use transform gizmos/select.
4. Configure four material layers with `.mat` assets, or direct albedo/normal-map
   images. Each layer has tint, tiling and normal strength. When a material is
   selected, its albedo, normal map, color, strength and X/Y tiling are used;
   layer tint, strength and tiling multiply those material values. Terrain is
   opaque: transparent/reflection material shader modes are not applied.
   Stochastic triangular-lattice sampling rotates/translates and blends texture
   samples with explicit mip gradients to break obvious repetition.
5. Import an 8/16-bit heightmap image from Assets. Height range and offset map
   normalized pixel values into local heights; the image is bilinearly resampled.
   Limits: 4096² source pixels/64 MiB, 65²/129²/257² terrain samples, 1–10000m size,
   ±10000m elevation. New Flat replaces the current terrain grid.
6. Save the terrain explicitly, or save the project, level or prefab. New assets
   go to `Assets/Terrains/Terrain-N.terrain`. They contain all heights, normalized
   four-channel paint weights, layer references and settings in CBOR + Zstandard
   (`TERZ0001` header). Scenes reference the asset, not embedded height arrays.

Brush units and height are terrain-local; use unit Transform scale for metre-based
editing. This is a single-valued heightfield: cliffs can be steep, but overhangs,
vertical walls and caves require separate meshes.

## Shadows, baking and brush preview

- Terrain shares the mesh surface-lighting code for directional/point/spot lights,
  cascaded/PCF shadows and cookies. It also casts full-resolution terrain shadows
  (without render skirts). Depth geometry is cached separately, with a bounded
  32-terrain cache, and rebuilt for sculpt/import/undo rather than paint edits.
- For baked lighting, mark the terrain Transform **Static**, use **Static** lights,
  and open **Windows > Lighting**. Bake as usual, then save the project/level.
  Terrain contributes its exact triangles to the bake BVH and receives a planar
  256²/512²/1024² atlas according to the existing quality setting. Atlases encode
  **baked direct irradiance with softened shadows**, not new GI bounces. Dynamic
  occluders remain active on baked terrain. The existing combined-RGB overlapping
  static-light masking limitation also applies to terrain.
- The atlas reference is saved inside `.terrain`; legacy terrain assets default
  to no atlas and no normal maps. Bake assignment/clearing participates in undo.
  **Clear terrain bake** is available in Terrain tools; Lighting's clear-level
  command clears mesh and terrain assignments together. As with mesh bakes,
  lighting is an authored snapshot: rebake after sculpting or moving terrain.
- Enable Scene brush and hover the terrain to see a gold radius ring following
  its surface, a center cross and a half-strength falloff guide. This is a clipped
  editor overlay, not a game effect or a depth-tested decal. Alt/right-drag camera
  navigation hides it. Nonuniform scale transforms the local brush into an ellipse.
- Normal maps follow the same stochastic tile offsets/rotations as albedo. Their
  tangent XY vectors are counter-rotated before blending so rotated tiles do not
  rotate the apparent light direction. Existing lightmaps are nondirectional:
  normal-map detail responds to realtime lights, not a baked directional field.

## Checks

- Make hills and valleys; smooth a ridge; flatten to a known height. Check all
  four paint layers and brush controls, including strokes at grid/chunk edges.
- Release each stroke, undo and redo. Each drag should be one history step.
  Undo/redo imports, flat replacement and material edits. Save again after undo
  to persist the restored data; history does not undo filesystem writes.
- Import grayscale 8/16-bit heightmaps, a nonsquare image, invalid files and an
  oversized file. Verify clear errors and no destructive replacement on failure.
- Save/reopen a project, switch saved levels, and save/spawn a terrain prefab.
  Verify heights, paints and layer paths survive. Check legacy projects still load.
- Duplicate a terrain and edit only one: in-memory copy-on-write must preserve the
  other. Project/level saving forks conflicting snapshots sharing one asset path
  into separate files. Explicit Save Terrain writes the displayed asset path.
- Drop a dynamic body/character onto hills and valleys. Collision is full-resolution,
  static triangle mesh, independent of render LOD. It rebuilds on stroke release,
  import, replacement and undo/redo, not on every brush frame or paint-only edits.
- Move/rotate/scale the object; compare visible surface and collision. Terrain
  collision is automatic; do not add a separate Collider or dynamic RigidBody.
- Move the camera near/far and outside the frustum. Check chunk LOD transitions,
  skirt coverage and edited chunk normals. Four LODs use 32-cell chunks and a
  bounded 256-chunk GPU cache; exceptionally large multi-terrain scenes can exceed
  that visible-chunk budget. Check FPS/memory manually.
- Enable the brush without clicking; change radius/falloff and check the two rings
  on slopes, near terrain edges and under rotated/nonuniformly scaled transforms.
  Navigate with Alt/right-drag and confirm no unwanted strokes or stuck preview.
- Assign a bumped `.mat`, paint several layers, and check normal strength 0/1/4.
  Use a strongly directional normal texture and move a light: stochastic rotations
  must preserve correct tangent-space lighting. Save/reopen and undo layer edits.
- Place a mesh over terrain and terrain hills beside mesh receivers. Check cast and
  received shadows for directional (including cascades), point and spot lights.
- Bake a static terrain under static lights and mesh occluders; compare soft shadow
  placement, then move a dynamic occluder. Check mixed baked + dynamic lights for
  double-lighting, LOD-independent atlas alignment, save/reopen and assignment undo.
  Clear terrain/level assignments and verify realtime lighting resumes.
- View a tiled material across a large flat region: repetition should be reduced.
  Check mip stability in motion and oblique views. Inspect logs for terrain shader
  compile/link errors on the Windows OpenGL driver.

Linux compilation does not validate GLSL, Windows/MinGW execution or these visual
and physics checks. Existing manual performance/particle guides remain applicable.
