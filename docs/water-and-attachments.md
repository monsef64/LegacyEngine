# Water meshes, wave shaders and bone attachments

## River waves and shader styles

River and Water share water controls. **Wave height** (0–3 metres), **Wave length**
(0.1–100 metres) and **Wave speed** (0–10 multiplier) now control actual geometric
vertical displacement, using three directional wave scales plus filtered normal
detail. **Flowing**, Flow amount and Flow speed control phase progression; turn
Flowing off to freeze the pattern, or set Wave height to zero for a flat surface.
Legacy scenes missing Wave height retain zero displacement. New components start
with gentle 0.08-metre waves. Wave phase uses the fixed simulation clock and pauses
with simulation; clouds keep their independent existing clock.

The shared **Water shader** list keeps Classic and Clear and adds six styles:

| `shaderStyle` | Style | Appearance |
| --- | --- | --- |
| 0 | Classic | Existing colored water with bank foam |
| 1 | Clear | Lower face-on opacity with grazing reflection |
| 2 | Ocean | Deep blue-green body |
| 3 | Lake | Calmer low-frequency detail and smoother highlights |
| 4 | Rapids | Finer detail, rougher highlights and broken-up surface foam |
| 5 | Tropical | Clear turquoise tint |
| 6 | Marsh | Dark olive tint and patchy surface detail |
| 7 | Industrial canal | Murky brown/olive, tight highlights and muted yellow-gray sky tint, inspired by the supplied reference screenshot |

These are procedural shader variants sharing one optimized pass, not separate
reflection render pipelines. The screenshot-inspired style approximates the
water's color, ripple detail and highlights. It **does not reflect buildings or
other actual scene objects**, sample scene depth or perform true refraction.
Reflection uses an analytic sky approximation rather than the active scene's
reflection image. Opacity is alpha-blended. Tune color, opacity and wave settings
for your scene; visual equivalence to the screenshot is not claimed.

River ribbons are refined across their width/length to support displacement.
Water meshes refine coarse triangles within a bounded vertex budget. Very coarse
or huge meshes may still show faceting: supply suitable source geometry and use
wavelengths appropriate to its density. These are visual analytic waves, not a
fluid solver or horizontally displacing Gerstner simulation.

## Water on arbitrary shapes

Use **Create > Environment > Water** for a plane, or **Add Component > Environment
> Water** on an existing mesh object. Water reuses River settings but replaces the
curve with mesh geometry. Leave **Water mesh** empty to use that object's
MeshRenderer, or choose an imported `.mesh`/legacy `.lemesh` asset explicitly.
Built-in plane, box, sphere, capsule and cone are supported. Imported custom
geometry is static/bind pose; skinned/deforming water is not supported. Prefer
one component, Water or River, per surface.

The normal opaque mesh/shadow pass is suppressed on Water objects; geometry is
rendered with the shared transparent water shader instead. Water is a virtual
surface: its own Collider/RigidBody do not create a solid simulated obstacle.
Move the surface via its Transform. Other colliders interacting with it retain
normal solid physics against the rest of the world.

### Surface physics, contacts and limits

Water supports the existing primitive-collider contact rings/foam and opt-in
**RigidBody > Float on rivers** buoyancy. Upward-facing triangles define the
surface footprint, including holes. Nearly vertical and downward-facing triangles
are excluded from physics; a closed mesh's upper faces can act as water, but the
mesh is **not** treated as a sealed liquid volume. For predictable results use
open, mostly horizontal surface meshes with correct winding.

Buoyancy selects the highest eligible surface under the body's center of mass,
within the existing bounded depth. It does not stack forces from overlapping
River/Water surfaces. Physics uses the same wave-height function and phase as
rendering, evaluated on the underlying surface plane; it does not solve a
continuously curved collision surface or wave-normal hydrodynamics. Objects whose
centers are outside a triangle footprint receive no buoyancy even if an edge
overlaps. Float on rivers is still off by default and requires a dynamic body,
gravity and a non-trigger primitive collider.

Contact foam uses conservative primitive bounds; it is not exact triangle-vs-box
intersection or persistent splash particles. At most 128 eligible colliders are considered and 16 contacts are shaded per
Water object. Geometry is cached (up to 32 entries, with separate render/surface variants) and capped at 120,000 vertices
per source, including refinement. Large meshes and many buoyant bodies increase
CPU cost; target-hardware performance has not been measured.

```lua
local water = self:getComponent(Water) or self:addComponent(Water)
water.shaderStyle = 7
water.waveHeight = 0.06
water.waveLength = 3
water.waveSpeed = 0.7
water.flowing = true
-- water.mesh = "Models/pond.mesh" -- optional, otherwise use this MeshRenderer
```

## Bone Attachment

Add **Bone Attachment** to the object you want to attach, such as a sword or a
flashlight. In its Inspector:

1. Choose the animated model owner in **Animated model**.
2. Choose a skeleton **Bone** from the model's nodes. Labels include node indices
   so identically named bones can be distinguished. The picker includes skeleton
   transform nodes as well as skin joints.
3. Adjust **Position offset** and **Rotation offset** (Euler degrees). Offsets are
   bone-local, transformed with the imported model's scale.

The object's Transform position and rotation follow the evaluated skeleton pose,
including paused animation poses and cross-fades. With no active clip it follows
the model's bind/rest node pose. The object's own scale is preserved. Use the
attachment offsets rather than editing the driven world Transform; its transform
gizmo is disabled while attachment is enabled. Disable the component to place the
object freely.

Attachments evaluate in Edit and Play and before physics synchronization, allowing
static/kinematic attached colliders. **Dynamic RigidBody attachments are rejected**
to avoid the animation and physics fighting over a Transform. Missing models,
invalid nodes, zero-scale bones, self/descendant sources and dependency
cycles/chains produce a diagnostic and leave the current Transform unchanged.
Source selection supports imported mesh owners and model roots with mesh children.

Configuration, source references and offsets are saved. Source references remap
when duplicating a whole hierarchy or loading scenes/prefabs. A bone is stored by
node index: reselect it after a model reimport changes skeleton ordering.
Attachment chains and inheriting animated bone scale/shear are not supported.

```lua
local attachment = self:addComponent(BoneAttachment)
attachment.source = character -- animated GameObject
attachment.node = 12          -- use the index shown by the Inspector
attachment.offset = {x=0.05, y=0, z=0}
attachment.rotation = {x=0, y=90, z=0}
attachment.enabled = true
-- attachment.error is read-only; vector getters return copies.
```

## Manual verification

No automated tests were added or run. Linux compilation and shader validation do
not verify desktop appearance or attachment alignment. Check manually:

- Compare all eight water styles near/far, above/below water, on sloped ribbons
  and imported meshes. Check height/length/speed, still water and Pause/Stop.
- Use a pond with a real hole and a closed mesh: confirm footprint holes remain
  dry for buoyancy and vertical/back faces do not produce a water top.
- Drop opted-in boxes/spheres on River and Water, test overlapping surfaces,
  moving the surface, disabling it and changing shader modes. Check contact foam
  separately from buoyancy and make sure the water itself is not a solid wall.
- Attach objects to hands/head on animated glTF/GLB models; test bind pose,
  playback, pause, cross-fades, offsets, rotated/scaled model owners and meshes
  below a model root. Check duplicate bone names and invalid-source diagnostics.
- Save/reopen, duplicate whole character hierarchies, prefab round-trip,
  undo/redo settings and switch scenes/projects. Play-time edits must revert on
  Stop. Check previous vehicle/grass/cloud/river workflows remain functional.
