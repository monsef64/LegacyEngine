# Terrain chunks and Navigation

## Camera-visible terrain pieces

Terrain remains a sculptable heightfield surface, not a solid volume or a set of
separate GameObjects. Its render subdivision is now **16 × 16 heightfield cells**
per piece instead of 32 × 32. Both color and shadow passes cull individual pieces
against their own view's planes **before** generating/uploading GPU geometry.
Transformed box bounds cover the heightfield surface, including rotated,
non-uniformly scaled and mirrored terrains. Chunks use matching full surface detail
so their boundaries meet without downward LOD skirts. No inner or outer chunk
boundary gets thickness/walls; sculpted hills remain. Only edited pieces
(including shared-edge neighbours) are invalidated. Full-detail rendering can submit
more triangles than the previous automatic distance LODs, while chunk frustum culling remains.

Color and depth caches are bounded to 1,024 pieces each. Smaller pieces reduce
geometry submitted outside a camera/light view but can increase draw calls and
cache pressure when a very large terrain is entirely visible. This is frustum
culling, not hill-to-hill occlusion. Shadow maps use light frusta, not the main
camera: an off-camera terrain piece can still cast a visible shadow. Existing
terrain files, physics and full-resolution bake surfaces keep their format and
shape. Foliage keeps its existing separate culling path.

## Bake a navmesh

1. Mark stationary floors, obstacles, meshes and terrains **Static** in Transform.
2. Open **Windows > Navigation**.
3. Set voxel cell size/height, agent radius/height, maximum climb and slope.
4. Press **Bake Navmesh**. Enable **Show navmesh in Scene** to see walkable polygon
   edges in teal. **Clear Navmesh** removes the saved bake.
5. Add **Navmesh Agent** to a separate GameObject and place its Transform origin
   at its feet near the walkable surface. A visual child can be offset upwards.

Baking uses Recast Navigation v1.6.0: slope classification, voxel rasterization,
low-clearance/ledge filtering, radius erosion, regions, polygons and detail heights.
Imported static meshes and known built-in shapes use full-resolution geometry;
terrains use their exact heightfield surface without LOD skirts or outer walls.
Collider-only boxes are supported; other collider-only shapes need a mesh visual
for baking. Render geometry takes precedence over a collider when both exist.
Materials do not determine walkability; slope and clearance do. Set unwanted
sources non-static before baking.

Triggers, inactive objects, water/rivers, cameras, moving rigid bodies,
controllers, agents, bone attachments and animated hierarchies are excluded.
Stationary zero-mass bodies may be bake sources. All agents use the profile baked
into the mesh: there are not separate per-agent radius/height navmeshes yet.
Editing the window's settings does not alter an existing baked profile until Bake.

Bake data is stored in a level-keyed JSON file under the project's `Settings/`
directory. Source transforms, terrain heights and dependent mesh file stamps are
recorded; changed geometry bypasses the stale bake until rebaked. Validation polls
about once a second, even when the window is hidden. This is an offline snapshot,
**not** live obstacle avoidance: do not move Static objects during gameplay.
Equivalent baked geometry can be reused after reload; changed filesystem stamps
or cross-platform/binary format differences can require a rebake.

Baking is an explicit synchronous editor operation, limited to two million input
triangles, four million XZ voxel cells and a 4,096-cell side. Increase cell size or
split the level if the grid is too large. Small isolated regions may be removed.
Very large/complex bakes, baking cancellation, tiled streaming navmeshes,
area-cost painting, off-mesh jumps and crowd/local obstacle avoidance are not yet
implemented. Scene visualization is capped at roughly 100,000 polygon edges.

## NavMeshAgent scripting

```lua
function onStart()
    agent = self:getComponent(NavMeshAgent) or self:addComponent(NavMeshAgent)
    agent.speed = 3.5
    agent.stoppingDistance = 0.15
    agent:setDestination(12, 0, 18)
    -- Also accepted: agent:setDestination({x=12, y=0, z=18})
end

function pauseNavigation()
    agent.stopped = true       -- keep the route
end

function resumeNavigation()
    agent.stopped = false
end

function cancelNavigation()
    agent:resetPath()
end
```

Writable fields: `enabled`, `speed` (0–100 m/s), `stoppingDistance` (0.01–10 m),
`stopped`, and `destination` (assign a complete world-space vector). Calling
`setDestination` queues a path request and resumes movement; direct destination
assignment queues a request but preserves `stopped`. A request is processed on
the next Play navigation update. `setDestination` is not a synchronous success
result—inspect status after the update.

Read-only fields: `hasPath`, `pathPending`, `remainingDistance`, `velocity` (copy),
and `status`. `resetPath()` clears requests, corners, distance and velocity. The
usual component get/add/remove, hierarchy searches, aliases (`navMeshAgent`) and
exported component references work. Inspector speed/enabled/stopping distance
settings persist in scenes/prefabs, duplicate with objects, and participate in
undo/reset. Paths, destinations and movement state are runtime-only.

Agents use Detour polygon pathfinding and funnel corners. Disconnected/partial or
buffer-truncated paths are rejected rather than walking through a missing route.
Each movement step is constrained along the navmesh and samples its surface height.
Start and destination must be near a walkable polygon; they are projected within
local baked-profile query extents, so use the correct Y on multi-storey levels.
Very long paths can exceed the 512-polygon/corner query budget. This is transform
movement, not a physics controller: do not combine it with RigidBody,
CharacterController, FPSCamera or TPSCamera movement. It does not automatically
rotate visuals or animate a walk cycle. Dynamic objects/other agents do not block
it; implement gameplay reactions separately. Pause stops updates; Stop restores
all runtime movement and edits to the pre-Play scene.

## Manual verification

No automated tests are added/run for this workflow.

- Pan/rotate Scene and Game cameras over sculpted terrain. Check edge chunks,
  hills, seams, small and large terrain resolutions, negative/non-uniform scale,
  and multiple terrains. Confirm no outer walls or missing visible surface.
- Sculpt/paint on a piece boundary, undo/redo and reload; neighbouring geometry,
  material weights, physics and shadows must remain consistent.
- Check directional/point/spot shadows while an off-camera terrain piece casts
  onto visible geometry; light-frustum culling must not use the Scene frustum.
- Bake a plane with static boxes, a ramp, a low ceiling and a sculpted terrain.
  Inspect the overlay, clearance around walls, climb/slope limits and narrow gaps.
- Route an agent around a box and over terrain; try a disconnected island, an
  off-mesh destination, both vector/numeric scripting arguments and multiple floors.
- Test stopped/resume/reset, zero speed, Pause/Stop, component removal, object
  deletion, duplication, prefab save/reload and exported agent references.
- Move a static obstacle, sculpt terrain, change/reimport a source mesh, undo and
  switch projects/levels. Stale bakes should be bypassed (within the poll period),
  never silently used indefinitely. Bake again and issue a fresh destination.
- Compare same-scene warmed-up profiler results before/after terrain subdivision;
  no Windows-specific FPS improvement is claimed without desktop measurement.

## Automatic path-facing rotation and dynamic obstacles

Agents automatically turn their forward direction toward their navigation movement (enabled by default). `angularSpeed` controls the smooth turn rate in degrees per second. `Vector3(x,y,z)` constructs a vector table for destinations and obstacle sizes:

```lua
local agent = self:getComponent(NavMeshAgent)
agent.angularSpeed = 360
agent:setDestination(Vector3(12, 0, 8))

local obstacle = gate:getComponent(NavMeshObstacle)
obstacle.size = Vector3(3, 2, 1)
obstacle.center = Vector3(0, 1, 0)
obstacle.enabled = true
```

Add **NavMeshObstacle** before baking so its mesh is excluded from static geometry. Its local box is transformed into a conservative world-axis-aligned box; this can overestimate rotated shapes. Agents route around box corners on existing walkable polygons and stop if no safe route exists. This is not navmesh carving, crowd simulation or physics collision avoidance. Obstacle changes trigger repaths at most five times per second; immediate movement checks prevent crossing a newly blocked segment. Bounded detour searches support up to 32 obstacles; more complex blocked paths fail safely. Spawn agents outside obstacle boxes. Agents must not carry their own obstacle component.

## Manual regression checks for editor fixes

- In focused Play, lock the mouse through scripting or TPS `lockMouse`; move
  horizontally/vertically and verify FPS/TPS mouselook continues beyond screen
  edges. Unlock, switch focus, and Stop: capture must release. Also check an
  external Game window. Scene right-drag navigation must continue to work.
- Open Navigation and Occlusion Culling on a small display and after restoring
  saved window layouts; resize them and verify all controls are reachable by scrolling.
- Inspect flat and sculpted terrain from low angles and below, including internal
  chunk boundaries. There should be no downward walls, cracks or flattened hills.
- Bake static occluders, select a camera, and enable the selected-camera overlay;
  check red/green bounds against the Camera preview, then move the camera and
  invalidate the bake. Game visibility must not be changed by toggling the overlay.
- Switch between Internal, VS Code and Sublime in Editor Settings, restart, and
  verify the selection persists. Open a project/script containing spaces and
  non-ASCII characters. In Sublime verify `.ls` highlighting, completions and hovers.
- Open a previously saved scene containing the removed Animator component: other
  components and the existing Animation player must still load normally.

## Agent callbacks

Attached scripts can define `onNavMeshAgentStatusChanged()` for every status
change, plus `onNavMeshAgentMoving()`, `onNavMeshAgentArrived()`,
`onNavMeshAgentBlocked()`, `onNavMeshAgentUnreachable()` and
`onNavMeshAgentStopped()`. Events fire once when entering that status, not every
frame. Read the agent's `status` inside the callback. Delivery happens after all
agents update in both editor Play and exported games.
