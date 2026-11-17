# Terrain foliage, mesh assets and editor navigation

## Terrain tabs

Terrain tools now has **Terrain**, **Grass** and **Trees** tabs. The Terrain tab
retains sculpt/paint/import controls. Grass and Trees paint GPU instances instead
of creating one GameObject per placement.

1. Select a Terrain and open Terrain tools.
2. Choose Grass and click **Install 5 sample grass textures**. This copies the
   bundled generated RGBA images into `Terrain/Grass` in the project's Assets and
   adds prototypes without overwriting existing files. Choose a prototype.
3. For trees, import an OBJ/glTF/GLB model in Assets, choose Trees > Add tree prototype,
   and assign its `.mesh` root asset. Old `.lemesh` models are also supported.
4. Enable Scene brush and paint with the left mouse button. Hold **Shift** to erase
   instances of the selected prototype. Brush radius uses terrain-local units.
5. Adjust width/height multipliers, random scale, tint and draw distance per prototype.
   Tree placement runs at 1/20 of the displayed base rate to avoid dense forests
   from a brief stroke. Removing a prototype removes its instances.
6. Save the `.terrain` asset in Edit mode. Foliage is included in terrain version 2;
   version 1 terrain files still load without foliage. Play edits are temporary,
   and terrain saving/sample installation are disabled during Play.

The generated samples are meadow, dry, fescue, wild and short grass, each a 512x512
RGBA cutout with real alpha. They are generated test textures, not photographs.
Import your own transparent PNGs using Add grass prototype as needed.

## Rendering and limits

Both grass and trees use OpenGL `glDrawArraysInstanced`, shared static geometry,
per-prototype distance limits and per-instance frustum culling. Visible instance
placements are uploaded for each view. Geometry is cached across placements and
terrain objects; model reimport invalidates the geometry cache.

Grass is two crossed alpha-tested cards with inexpensive wind sway. Grass UVs
use V=0 at the base and V=1 at the tips, matching the material loader's OpenGL
row flip; tree-model UVs are unchanged. Trees use
static bind-pose mesh parts, diffuse colors and albedo textures from imported
materials. This supports multi-part models; no individual tree GameObjects are
created. Instances follow terrain height edits automatically, and transform with
their Terrain's hierarchy.

This is lightweight visual foliage, not the full Unity vegetation pipeline:
- no tree colliders, animation, physics, navigation, LOD billboards or shadow maps;
- simple fixed hemisphere/directional shading rather than scene-light/material parity;
- no per-instance Inspector selection; use the brush to erase;
- at most 16 prototypes per category, 100,000 grass and 10,000 trees per terrain;
- at most 64 cached geometry assets and 2 million cached vertices per Renderer;
- tree roots are limited to 32 parts and 300,000 vertices: use low-poly models
  with pivots at their base, and reduce draw distance for dense vegetation.

GPU instancing reduces draw-call overhead, but does not remove triangle/fill-rate
cost. No frame-rate improvement is claimed without target-hardware measurements.

## Mesh extension

New model imports, including model children, use **`.mesh`**. Compression,
animated glTF support, material children and previews are unchanged. Existing
`.lemesh` files and references remain readable; no project files are blindly
renamed and old animation/collider references keep working. New scripting
examples recommend `.mesh`.

Double-click a mesh in Assets to open the model preview. Left- or right-drag over
the preview to orbit; use the wheel to zoom. The preview camera is independent
of the Scene camera.

## Editor defaults

- Asset Browser starts in Grid mode with 60-pixel icons. List/Grid and icon-size
  controls remain available.
- Scroll zoom is immediately to the right of Snap on the Scene toolbar. Narrow
  layouts keep them on the same row with horizontal scrolling rather than putting
  Scroll zoom below Snap.
- Opening a project or loading a level starts entity parents collapsed. The level
  root remains visible/open; search can still reveal matching descendants.

## Manual verification

- Paint/erase each grass sample and several user-provided tree models. Inspect
  alpha edges, material parts, scale, height following, tint, wind and culling.
- Verify instanced draw calls with a GPU capture; compare 1/100/1,000 placements
  and check that per-instance placement does not create Hierarchy objects.
- Paint, undo/redo, save/reopen, duplicate terrain, switch levels/projects and
  perform temporary Play-mode foliage edits followed by Stop.
- Load a version 1 terrain, save as version 2, and verify original sculpt/layers.
- Import OBJ and animated glTF/GLB: new root/children use `.mesh`, play animations,
  use mesh colliders, copy/move assets, save/reopen and preview orbit/zoom.
- Open an existing `.lemesh` scene and confirm models, animations and colliders work.
- Open a project with nested parents and verify entity children remain collapsed.
- Check Snap/Scroll zoom at wide and narrow Scene widths and browser Grid/60 defaults.
