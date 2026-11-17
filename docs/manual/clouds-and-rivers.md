# CloudSystem and River components

Both components support Add/Remove/Reset, copy/paste, duplication with children,
scene/prefab persistence, Inspector editing and Lua get/add/remove/find/export
operations. They require Transform. Disabling the component or its GameObject hides
it. Play-mode edits are temporary and Stop restores the pre-Play scene.

## Clouds

Add **CloudSystem** through Inspector > Add Component. Move its Transform above
the level (for example Y=20); `size` multiplies its scale. A 2D cloud layer is the
cheapest mode: one horizontal plane with animated procedural noise and soft edges.
Enable **Volumetric** for a noise-filled box with genuine view-ray integration,
4..24 fixed-budget samples (default 12), edge falloff and early opacity termination.
No external cloud textures are needed.

```lua
local clouds = self:getComponent(CloudSystem) or self:addComponent(CloudSystem)
clouds.volumetric = true
clouds.size = {100, 15, 100}
clouds.coverage = 0.6
clouds.density = 1.2
clouds.speed = 0.3
clouds.steps = 12
clouds.color = {x=0.9, y=0.95, z=1}
```

Clouds are visual, not a weather/physical atmosphere simulation: no cloud shadows,
multiple scattering or scene-light integration. Volumes depth-test at their ray
entry point rather than sampling opaque scene depth along the ray. Keep volumes
in sky regions without intersecting opaque objects; intersections and overlapping
transparent objects can show sorting artifacts. Mirrored scales are supported.
Large screen-filling volumes still cost fill rate: prefer 2D, lower steps, fewer
layers or smaller coverage on low-end GPUs. Coverage or density zero is clear.

## Rivers and Scene curve editing

Add **River** to a GameObject. It starts with three local-space curve points.
Enable **Edit curve in Scene** in the Inspector. Cyan point handles and a sampled
Catmull-Rom centerline appear in Scene; drag a handle on its world-horizontal plane.
Use the Inspector point XYZ controls to edit elevation, or to position points when
the camera is too close to horizontal for plane dragging. Add/remove the final point
with Inspector buttons (2..64 points). Turn curve editing off to use the object's
normal Move/Rotate/Scale gizmo. The Transform moves the whole river.

```lua
local river = self:getComponent(River) or self:addComponent(River)
river.points = {{-12,0,0}, {0,0,5}, {12,-1,2}, {20,-2,8}}
river.width = 4
river.speed = 1.5
river.waveScale = 2
river.waveStrength = 0.15
river.foam = 0.4
river.color = {0.03, 0.28, 0.36}
-- Vector/list reads are copies; reassign after modifying:
local points = river.points
points[2].z = 7
river.points = points
```

Water uses a cached triangle strip, scrolling procedural ripples, normal-based
shading, a simple Fresnel tint and edge foam. Waves change shading, not geometry.
It has no reflection/refraction cameras, render-target copies, texture dependencies,
terrain carving, depth-based shore foam, collision, buoyancy or gameplay current.
Keep points reasonably spaced and avoid very sharp turns or self-intersections;
a fixed-width strip may overlap there. Rivers can slope, but are not waterfall meshes.
Raise the surface a little above terrain to avoid z-fighting.

## Performance and limits

Geometry buffers are reused; rivers rebuild only when points, width or subdivisions
change. Transform, color, flow and opacity changes are uniforms, not mesh rebuilds.
A river has at most 63 spans x 32 subdivisions x 2 triangles (4032 triangles).
Clouds use one quad or a 12-triangle volume. Effects are frustum-culled, sorted
back-to-front by object origin and capped at 256 drawn/cached component instances
per Renderer. These are cost controls, not benchmark results. Transparent objects
from different effect systems and intersecting rivers/volumes are not globally sorted.
Animation uses renderer time in both Edit and Play; Pause does not freeze the shader.
Transparent effects do not contribute opaque depth, baked lighting or fog depth.

## Manual verification

- Switch cloud modes in Scene/Game and camera preview; move/rotate/scale them,
  try mirrored scales and view from inside a volume and below a 2D layer.
- Test density/coverage/opacity zero, volume steps 4/12/24 and inactive parents.
- Edit river handles with a translated, rotated and scaled parent hierarchy;
  change Y in Inspector, append/remove points and undo/redo curve edits in Edit mode.
- Confirm river centerline, width, slopes, flow direction, wave scale and foam.
- Save/reopen and duplicate/prefab both components; check settings and local points.
- Set every field from Lua, including whole vector/point arrays; reject invalid ranges,
  non-finite values, malformed vectors and point counts outside 2..64.
- During Play change clouds/river curve, then Stop; verify the original scene returns.
- Check OpenGL visuals, transparent overlap and GPU timings on target hardware;
  verify steady geometry is not re-uploaded when only color or flow changes.
