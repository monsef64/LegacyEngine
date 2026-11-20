# Third-person camera and environment/editor follow-up

## TPS Camera

Use **Create > Cameras > TPS**, or **Add Component > Cameras > TPS** on a separate
camera object. It requires Transform and Camera and cannot be added alongside
FPSCamera. Drag the player/vehicle root from Hierarchy onto **Follow target**, or parent the
camera under that root and leave the target empty. Set its Camera **Active** if
another Game camera is already active. Newly created cameras activate only when
there is no existing active camera, matching the other camera presets.

During Play, focus Game and **hold right mouse** to orbit (a script-locked cursor
also enables orbit). **Mouse wheel** changes desired distance. There is no
automatic character movement/rotation: attach your existing controller to the
target. Scene editing continues to use the editor camera, not TPS controls.

| Setting | Purpose |
| --- | --- |
| Enabled | Enable the TPS pose driver |
| Follow target | Hierarchy drag-and-drop target; Clear target returns to Transform parent |
| Lock mouse in Play | Optional cursor lock; scripting: `lockMouse`, default false |
| Target offset | World-space offset, default `(0, 1.5, 0)` |
| Distance / min / max | Desired distance and zoom limits |
| Yaw / pitch | Orbit angles in degrees, independent of target rotation |
| Min / max pitch | Vertical orbit limits, within -89..89 degrees |
| Sensitivity | Mouse rotation sensitivity |
| Zoom speed | Metres per wheel unit |
| Smoothing | Exponential position response; zero snaps |
| Shoulder | Screen-right offset in metres; negative swaps shoulders |
| Collision radius | Minimum sphere radius for obstruction checks |

Position smoothing is checked for obstruction **after** smoothing, so pulling
in is immediate when necessary; recovering distance remains smoothed. A Jolt
sphere sweep from the look-at pivot to the requested position covers camera
radius and near-plane size. Triggers, the target hierarchy and Water surfaces
are ignored. Obstructions must have physics colliders (terrain counts). This is
not render-mesh collision: mesh-only walls need colliders. If the look-at pivot
starts inside solid geometry, the camera can collapse toward that pivot and
cannot manufacture a clear view. Choose a sensible target offset. Avoid very
large near clips or a target that is inside/behind walls.

TPS controls the world-space camera pose. Its own Transform scale is preserved;
Camera projection, viewport, shake and other camera APIs remain on the Camera
component. Camera tracking/following does not override an enabled TPS driver.
Do not put dynamic RigidBody or Bone Attachment on a driven camera. Following
self or a descendant is ignored to avoid feedback. Missing targets leave the
current camera pose unchanged. Camera state pauses with Play; Stop restores the
edit scene. Settings and target references persist and remap in hierarchy copies
and prefabs; smoothing history and obstruction distance are runtime-only.

### LegacyScript example

Attach this to the camera and assign `player` by lookup or an exported reference:

```lua
function onStart()
    local player = findGameObjectByName("Player")
    local tps = self:getComponent(TPSCamera) or self:addComponent(TPSCamera)
    tps.target = player
    tps.offset = {x=0, y=1.5, z=0}
    -- Defaults support this distance. When changing limits, widen them before
    -- assigning a distance that was outside the previous interval.
    tps.distance = 5
    tps.shoulder = 0.4
    tps.smoothing = 10
    tps.collision = true
    tps.lockMouse = true -- set false to release the TPS-owned cursor lock
    self:getComponent(Camera).active = true
end
```

TPSCamera supports `get/add/removeComponent`, component searches and exported
component references. All Inspector settings above are writable fields using the
camelCase names in `scripts/definitions/legacyengine.lua`. Assign offset as a
**complete vector**; its getter returns a copy. `resolvedDistance` is read-only.
Setting `target=nil` returns to parent following. Use `enabled=false` to release
the camera Transform. `mouseLook=false` allows scripted yaw/pitch with optional
wheel zoom; input is read only while the Game input context is enabled.

## Skybox-colored environment light

On Skybox, enable **Affect environment** and tune **Environment intensity**
(0..5, default 1). It is **off by default**, including for old scenes. In Lua:

```lua
local sky = self:getComponent(Skybox)
sky.affectEnvironment = true
sky.environmentIntensity = 1.2
```

The renderer samples the decoded sky image at load/reload time and computes a
small upper-sky-weighted average. LDR samples are converted approximately to
linear light; HDR averages use bounded exposure compression. The average, Skybox
color and intensity provide colored ambient light on regular lit meshes, terrain
and terrain foliage. A sunset sky can therefore give surfaces a warm ambient
cast instead of changing only the background. An empty sky uses its color as the
ambient tint. Active Skybox fades blend this ambient contribution too.

This is a lightweight ambient approximation, **not** physical global illumination,
spherical-harmonic irradiance, local reflection probes or a sun-light extractor.
It does not generate directional shadows, rebake lightmaps, tint unlit materials,
particles or procedural water, or simulate indoor occlusion. Use a DirectionalLight
for sunset direction/shadows; existing direct lights remain independent. Existing
baked light can already contain illumination, so avoid excessive ambient intensity.
Multiple skyboxes use the first enabled active one, as the background already does.

## Vehicle steering stability

**Vehicle > stabilityAssist** (also `vehicle.stabilityAssist` in Lua) defaults to
1 and reduces the maximum commanded steering angle as speed rises. The limit
estimates a conservative cornering acceleration from wheelbase, track width and
suspension-derived center-of-mass height. Front wheel visuals use the assisted
angle. Zero restores unrestricted steering; intermediate values blend the limit.

This addresses steering-induced rollover without locking chassis rotation or
teleporting it upright. Collisions, curbs, jumps, extreme slopes, unsuitable
geometry/mass/grip settings or reducing the assist can still flip a real simulated
vehicle. Keep the physics root at unit scale and model dimensions consistent.

## Terrain and Scene selection

Terrain retains its sculpted heightfield, hills, materials and surface collision.
Outer render skirts are removed so exposed boundaries are open, plane-like edges,
not tall cube walls. Internal LOD seam skirts remain to hide cracks between chunks;
they are not collision geometry. This does not flatten existing terrain.

Scene clicks now also intersect visible mesh triangles and terrain surfaces,
without requiring an extra Collider just to select them. Clicking visible child
meshes selects their enclosing empty assembly/model root; **Ctrl-click** keeps the
individual hit child. Use Hierarchy for exact selection, inactive objects, or a
broken/missing asset. Imported mesh picking uses static/bind geometry rather than
GPU-skinned animation poses; animated objects should have suitable colliders when
accurate animated picking is required. Selection is nearest-hit, not a GPU ID pass.

## Editor controls and defaults

- **Middle-drag** pans the Scene camera vertically/horizontally.
- **Shift + middle-drag** uses horizontal movement to pan and vertical movement to
  zoom. It works over Scene and does not move selected objects.
- New editor settings use **FPS limit = 0 (unlimited)** and new rendering settings
  use **VSync off**. Existing saved caps/VSync choices are preserved: set FPS limit
  to 0 in Editor Settings and disable VSync under Rendering for an existing project.
  Unfocused/background throttling remains enabled to avoid wasting resources.
- Right-click component numeric/vector/color/boolean controls for **Copy**, **Paste**
  and **Reset value**. Direct component fields reset to component defaults within
  allowed bounds; Transform scale resets to 1 and position/rotation to 0. Text
  inputs reset to empty. Non-component temporary controls fall back to zero/clamped
  zero. This complements the existing whole-component header Reset action.
- Newly created primitives, cameras, effects, imported models and prefabs appear
  about **3 metres in front of the Scene camera**, independent of its faraway orbit
  target. Primitive collision placement adjustments are bounded to about one metre.
  Very large assets/terrain still need appropriate scaling/framing.

## Manual verification

No automated tests were added or run. Compilation/shader checks do not establish
visual appearance or driving behavior. On the desktop build:

1. Compare sunset/day/HDR/empty skies with Affect environment off/on at 0/1/5;
   inspect lit objects, terrain, foliage, shadows and baked objects. Test sky fades,
   missing textures, scene/project switches and material previews.
2. Drive fast on flat ground and steer abruptly at assist 1 and 0; repeat with
   brakes, handbrake, low grip, hills, curbs and external impacts. Inspect wheel
   angle alignment and confirm airborne/collision rotation remains physical.
3. Create TPS by both menus and Lua; follow a walking character and vehicle. Test
   parent/default/explicit/missing targets, orbit, pitch limits, zoom, shoulder,
   smoothing, narrow doors, wall corners, terrain and thin obstructions with
   colliders. Try large near clips, pause/resume and a separate Game window.
4. Save/reopen scenes/prefabs; copy a target/camera hierarchy and verify remapped
   references. Change settings during Play and confirm Stop restores them. Check
   FPS, Camera tracking/shake, component searches and exports still work.
5. Select terrain from above/below, imported mesh children and empty model roots;
   Ctrl-click children. Check inactive/broken assets remain selectable in Hierarchy.
   Inspect terrain boundaries, internal LOD seams and surface collision.
6. Test middle/Shift-middle navigation without unwanted selection/gizmo drags;
   create small objects with the editor orbit target very far away. Check default
   uncapped behavior and existing saved caps/VSync. Reset individual fields, undo
   them, and verify sibling fields/components are unchanged.


`TPSCamera.lockMouse` is saved with the camera and can be changed by scripts at
runtime. Only the active Game camera requests this lock, while Play is running
and Game has input focus. Disabling the driver, switching cameras, pausing,
stopping or losing application focus releases its owned lock. Restoring Game
focus can reacquire it while `lockMouse=true`. This does not change Scene freecam
or a cursor lock requested independently by a script before TPS takes ownership.
