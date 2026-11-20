# Mesh LOD and baked occlusion

## Opt-in mesh LOD

Select an object with a model asset in **MeshRenderer** and press **Generate LODs
(50% / 20%)** in Edit mode. Nothing is generated on import, project open, or during
rendering. Generation is an explicit, synchronous asset-authoring operation; large
meshes can take time. It changes the shared source asset's LOD links, not just the
selected instance. The source geometry remains intact. Additional compressed
`.mesh` assets are written beside it with unused `_LOD1` / `_LOD2` names. No model
folders are created and user-authored files are not overwritten.

This uses meshoptimizer v0.22 simplification, preserving vertex seams and mesh
borders. Requested triangle targets are approximately 50% and 20%, with 2% relative
error tolerance; safe reductions may be smaller than requested. Tiny meshes or
meshes whose seams/borders prevent reduction report a message instead of dropping
arbitrary triangles. Select a single unskinned mesh child for multi-mesh models;
aggregate model containers and skinned meshes are not currently generated.

The renderer selects generated levels at 20 and 50 metres from the object origin,
normalized by the largest absolute Transform scale. Selection is independent for
each Scene/Game camera. Missing/pending LODs fall back to original or an available
higher-detail level while the requested mesh streams asynchronously. Physics,
picking, shadows, and direct-light baking continue to use original geometry.
Animated pose meshes and lightmapped instances also keep full detail. Changing
import scale bypasses old LODs until regenerated. Reimporting replaces asset
metadata; regenerate LODs after reimport. Generated assets can be managed normally
in Assets. Regeneration uses new unused filenames; it does not silently delete
previous generated assets. These are discrete LOD switches, not cross-fades.

## Windows > Occlusion Culling

1. Mark stationary opaque objects **Static** in Transform.
2. Open **Windows > Occlusion Culling** in Edit mode.
3. Press **Bake static objects**. The window shows the retained triangle count,
   enable checkbox, status and last rendered view's culling counts.
4. Move Scene/Game cameras to inspect visibility. **Clear bake** removes the saved
   bake and disables this optimization without modifying objects or meshes.

The bake gathers actual transformed triangles from imported mesh assets and known
built-in primitives. It retains up to the largest 2,048 opaque triangles (with a
2-million-source-triangle scan limit). Baked data is stored in a level-keyed JSON
file under the project's `Settings/` directory, separate from scene objects.
Generation is synchronous and limited to Edit mode; baking and clearing do not
modify transforms, components, physics or the scene undo history.

This is **conservative triangle-cone occlusion**, not a sampled visibility-cell
PVS, GPU occlusion query system, or terrain bake. A candidate's expanded world
bounds must be entirely behind and inside one solid front-facing baked triangle's
projection. There is no center-ray heuristic and no stitching over holes. Large
walls often provide useful occluders; highly tessellated surfaces can provide
little culling because adjacent triangles are not merged. Small occluders are
omitted when the budget is exceeded. This sacrifices culling opportunities rather
than hiding visible geometry.

Only eligible static MeshRenderer objects are used/tested. Terrain, water,
transparent materials, per-instance material overrides, cameras, animations, bone
attachments, character controllers, and rigid-body hierarchies are excluded.
Dynamic objects still render normally. Transforms, activity, eligibility, mesh
references, entity identity and model revisions are validated against the bake;
changes bypass stale data until re-baked. Reloading a scene can change entity IDs
or model revisions and require a new bake. Moving dynamic objects does not stale
the static bake. Pending model assets, orthographic views and near/far-clipped
occluder triangles bypass culling. Shadow passes are not occlusion-culled, so
hidden objects can still cast shadows. Eligible static meshes use full detail
while a valid occlusion bake is active, ensuring baked occluder geometry matches
what is rendered rather than a simplified LOD.

The CPU checks have a cost; compare profiler results in your own scene before
leaving the optimization enabled. No hardware-specific FPS increase is promised.

## Manual verification

No automated tests are required for this workflow.

- Preview an asymmetric texture with clear top/bottom markings in the asset hover
  popup and texture picker; compare against the source and check HDR, model and
  material previews separately (their upload orientation differs).
- Drag a Hierarchy object onto TPS Follow target; clear it to return to parent
  follow. Reject self/descendant targets. Save/reload and duplicate the hierarchy.
- Set `tps.lockMouse=true` in LegacyScript, focus Game in Play, orbit without RMB,
  then set false. Repeat camera switch, Pause/Stop, Alt-Tab and external Game view.
  Scene navigation and independent script cursor locks must remain unchanged.
- Request LOD generation explicitly on a detailed mesh with UV seams. Confirm
  new assets and fewer triangles, inspect near/far views and differently scaled
  instances, and verify collision and shadows keep original topology. Repeat
  after import-scale changes and with missing generated files. Skinned meshes
  must retain animation and refuse generation rather than lose weights.
- Bake a large static opaque wall with a small static object behind it. Check the
  count from both sides and from the wall's edge; peeking silhouettes and holes
  must remain visible. Repeat with negative scale, rotated walls, near clipping,
  orthographic camera and more than one viewport.
- Move/hide/delete an occluder, change its material, undo, switch projects/levels,
  and reload: stale data must bypass culling, not retain invisible occluders.
- Verify transparent/animated/water/terrain/physics objects remain unaffected,
  original shadows remain, and Clear bake restores all render candidates.

## Visualizing a selected camera

Open **Windows > Occlusion Culling**, enable **Visualize selected camera**, and
select a perspective Camera in the hierarchy. The Scene view shows that camera's
frustum and a debug overlay: cyan usable baked triangles, red bounds for occluded
static meshes, green bounds for retained static meshes in the camera frustum.
The window reports the selected-camera culling counts separately from the last
rendered view. The overlay does not change the camera, bake or game visibility.
Disabled/stale bakes and orthographic cameras bypass the occlusion overlay.
The existing selected-camera preview continues to render with normal baked culling.
Navigation and Occlusion Culling windows now start at a bounded usable height;
resize them as needed, with content scrolling inside the windows on smaller screens.
