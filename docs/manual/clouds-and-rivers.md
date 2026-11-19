# CloudSystem and River components

Both components support Add/Remove/Reset, copy/paste, duplication with children,
scene/prefab persistence, Inspector editing and Lua get/add/remove/find/export
operations. They require Transform. Disabling the component or its GameObject hides
it. Play-mode edits are temporary and Stop restores the pre-Play scene.

## Clouds

Add **Clouds** through Inspector > Add Component > Environment. The Lua and saved-scene type remains `CloudSystem` for compatibility. Move its Transform above
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

Add **River** through Add Component > Environment to a GameObject. It starts with three local-space curve points.
Enable **Edit river curve** in the Scene overlay (or **Edit curve in Scene** in the Inspector). Cyan point handles and a sampled
Catmull-Rom centerline appear in Scene; drag a handle on its world-horizontal plane.
Ctrl-drag a handle vertically to edit height. Shift-click empty Scene to append a
point on the last point's horizontal plane. Scene **+ Point** extrapolates the end;
**− Point** deletes the selected point (or the last point). Right-click a point to
insert after it or delete it. The Inspector XYZ and add/remove controls remain
available, including for near-horizontal views where plane dragging is difficult
(2..64 points). Turn curve editing off to use the object's
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

Water uses a cached triangle strip, two-scale flowing noise for subtle surface
normals, derivative filtering, Fresnel sky tint, a soft sun highlight and broken-up
edge foam. The repeating sine-wave bands have been removed. Inspector **Ripple
scale** / **Surface detail** retain the existing Lua `waveScale` / `waveStrength`
fields for compatibility; surface detail changes shading, not geometry. Sky and
sun are inexpensive analytic approximations, not actual scene reflections.
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
Cloud animation uses renderer time. River/Water wave and flow animation now uses the fixed simulation clock and freezes while paused.
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

## Still/flowing water and transparent mode

River Inspector controls (also available as Lua properties):

- **Flowing** (`flowing`) freezes the base water animation when disabled.
- **Flow amount** (`flowAmount`, 0–2) multiplies **Flow speed** (`speed`). Zero
  gives still water; negative speed reverses flow. Surface detail remains a
  separate control, so still water can retain subtle static roughness.
- **Transparent water shader** (`transparent`) selects the clearer water mode,
  with low face-on opacity, increasing grazing-angle Fresnel reflection and sun
  highlights. **Opacity** and **Water color** still apply. The original mode is
  the default for existing scenes. Transparency is alpha-blended transmission,
  not scene refraction, depth-based absorption or real scene reflections.
- **Collider contact effects** (`collisionEffects`) and **Contact strength**
  (`collisionStrength`, 0–2) control animated contact rings and foam. They work
  in either shader mode and can animate even when the base water is still.

Contact effects intersect the sampled river ribbon with transformed primitive
collider bounds, including collider offsets, rotation and scale. Boxes are
supported; sphere/capsule/cone contacts use conservative bounds. Triggers and
triangle-mesh colliders are excluded. Each rendered river considers at most 512
eligible colliders and displays at most 16 contacts. Effects follow current
surface intersections and disappear when contact ends; they are not persistent
splash particles, buoyancy, rigid-body forces or fluid simulation. No contact
state is serialized, and no components are modified by rendering.

The added settings have backward-compatible defaults and are saved with the
River component. Lua can set, for example, `river.flowing = false`,
`river.transparent = true` or `river.collisionStrength = 0.8`.

### Manual verification: water modes and contact

1. Toggle Flowing, set Flow amount to zero, and reverse Flow speed. Confirm
   base flow stops/reverses independently of contact effects.
2. Toggle transparent mode over opaque underwater objects. Inspect face-on and
   grazing angles and Opacity values. Check both modes on curved/sloped rivers.
3. Drop a box through the surface in Play. Move/rotate/scale boxes, including
   offset colliders, across banks and endpoints. Check rings/foam only during
   surface overlap, not when fully above or below it.
4. Check spheres/capsules, disabled objects and trigger exclusion; disable
   contact effects and set Contact strength to zero.
5. Save/reopen new settings, load an older scene, and verify Lua properties.
   Make temporary changes in Play then Stop: normal edit-state restoration
   must preserve the original component settings.

## Scene camera capture and empty origins

Scene right-drag freelook and Alt-left-drag orbit now use relative mouse input.
While captured, mouse interaction with ImGui panels is suppressed; releasing the
navigation button or losing window focus releases capture. Runtime/game cursor
locking remains separate.

Alt-click without motion does not re-aim the camera. During an orbit, the gizmo
is visible but non-interactive at the selected Transform origin, including empty
objects and particle emitters. An empty hierarchy with no meshes falls back to
its own origin in Center mode rather than inventing a center from child positions.

Manual checks: create an empty at a nonzero position, with and without children;
compare Inspector position before/after repeated Alt-drags and Alt-clicks. Repeat
in Origin/Center modes and with particles. During long right-drags, move toward
Hierarchy/Inspector, click other buttons and verify no selection or editing occurs.
Release, Alt-Tab and refocus; confirm the pointer and normal UI interaction return.
Check Windows/MinGW and the desktop SDL backend: the console build does not
validate native capture or rendered appearance.


## Geometric waves and mesh Water follow-up

River now has geometric Wave height/length/speed controls and eight shader styles,
including five additional natural-water looks and an Industrial canal style
inspired by the supplied screenshot. The separate Water component applies shared
shading to arbitrary static mesh geometry and supports surface-based contact
foam and opt-in buoyancy. See the [water and attachment guide](../water-and-attachments.md)
for the current implementation, compatibility rules and manual checks. Earlier
normal-only shading notes describe the zero-Wave-height setting; no full fluid
simulation or actual scene reflection/refraction is introduced.
