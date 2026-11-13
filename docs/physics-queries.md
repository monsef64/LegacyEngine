# Physics scripting, triggers, raycasts and layers

## RigidBody

Existing properties `mass`, `useGravity`, `linearVelocity` and `angularVelocity`
remain available. `velocity` aliases `linearVelocity`. `drag` and `angularDrag`
are nonnegative solver damping. `isKinematic=true` lets scripts move the body
without dynamic force/gravity integration. A body with zero mass is static.

```lua
local body = self:getComponent(RigidBody) or self:addComponent(RigidBody)
body.mass = 2
body.useGravity = true
body.drag = 0.1
body.angularDrag = 0.2
body:addForce(0, 20, 0)
body:addImpulse(0, 4, 0)
body:setVelocity(1, 0, 0)
local velocity = body:getVelocity()
body:addVelocity(0, 1, 0)
body:movePosition(0, 2, 0)
body:moveRotation(0, 90, 0)
body:stop()
```

Methods also accept vector tables. `moveRotation` accepts Euler degrees or a
quaternion table. Forces and impulses accumulate until the next fixed solver
step, then clear. Call `addForce` from `onFixedUpdate` for continuous force;
do not multiply impulses by deltaTime. `movePosition` is a teleport, **not** a
swept character move. `stop` clears velocities and queued force/impulse.

## Trigger colliders

Enable **Is trigger** in Collider, or assign `collider.isTrigger=true`.
Triggers detect overlap but do not physically block objects. The engine sends
callbacks to scripts on both participating active objects when at least one
collider is a trigger. Static trigger objects are supported without RigidBody.

```lua
function onTriggerEnter(collider)
    if not collider then return end
    local other = collider.gameObject
    debug.log("Entered", other:getName(), other.layer)
    local position = collider.transform.position
    local body = other:getComponent(RigidBody)
    if body then body:addImpulse(0, 2, 0) end
end

function onTriggerStay(collider)
    if collider then debug.log("Still inside", collider.gameObject:getName()) end
end

function onTriggerExit(collider)
    debug.log(collider and collider.gameObject:getName() or "Destroyed collider", "left")
end
```

`collider.gameObject` gives generation-safe access to names, layers, activity and
other components. `collider.transform` is the owner's live Transform. Exit can
receive **nil** if the other object or Collider has already been removed.
Callbacks are delivered after physics, once per playing update for each pair:
Enter first, Stay on subsequent overlapping updates, Exit when overlap stops.
Pause freezes delivery. Inactive recipients do not receive callbacks. This uses
discrete collider overlap checks, not continuous swept trigger detection;
very fast objects can cross a thin sensor between updates.

## Layers

Open **Project > Project Settings > Layers** to add/remove named layers (up to 32
including the built-in **Default** layer). The Inspector's **Layer** field assigns
a GameObject. Scripting uses `object.layer = "Enemies"` (the layer must exist).
Deleting a layer resets matching objects in the current level to Default and
clears that bit from current Raycast masks. Layer names persist in
`Settings/Layers.json`; assignments and masks persist with scenes/prefabs.
Layers are stable numeric slots, so reassignment in other saved levels is not
rewritten when editing the current level's settings. Removing/reusing a slot
should be followed by checking other saved levels. Default cannot be removed.
These are **query filters**, not a physical collision matrix.

## Raycast component

Create **Raycast** from Create / Hierarchy context menus, or add its component in
Inspector / scripting. Transform local **-Z** is forward. The query excludes its
owner. Settings:

- **Line**: one forward ray with adjustable distance.
- **LineOfSight**: an adjustable fan on local XZ, default 180° with 25 rays.
  Angle supports 0–360°; samples support 2–181. Each ray stops at its nearest
  selected-layer collider, so nearer objects occlude farther ones along that ray.
- **Sphere**: overlapping colliders inside a radius. This is an overlap query,
  **not a swept sphere cast**.
- **Origin offset** is local-space. Query layers and Include triggers filter
  candidates before selecting hits, so ignored layers are passed through.

```lua
local query = self:getComponent(Raycast) or self:addComponent(Raycast)
query.type = Raycast.Line -- also Raycast.LineOfSight, Raycast.Sphere
query.distance = 30
query.includeTriggers = false
query:setLayers({"Default"}) -- replace mask; {} matches nothing
local hit = query:cast()
if hit then
    debug.log(hit.collider.gameObject:getName(), hit.distance)
    debug.log(hit.transform.position.x, hit.point.y, hit.normal.z)
end
for _, result in ipairs(query:castAll()) do
    debug.log(result.gameObject:getName(), result.distance)
end
```

`cast()` returns the nearest hit or nil. `castAll()` returns distinct objects,
distance-sorted: all intersected colliders for Line; nearest blockers across fan
rays for LineOfSight; overlaps for Sphere. Queries use collider poses most recently
synchronized by physics, not render meshes; objects moved/created during script
Update become queryable after the next physics synchronization.

| Hit field | Value |
| --- | --- |
| `collider` | live Collider handle |
| `transform` | live Transform handle |
| `gameObject` | owning GameObject |
| `point` | world-space surface hit location |
| `normal` | world-space surface normal |
| `distance` | distance from query origin to point |

For Sphere overlaps, point/normal come from a ray toward the overlapping
collider's center. If the query starts inside it, point is the query origin,
normal is zero and distance is zero; these are not penetration contact data.
Line rays starting inside a collider follow the physics backend's inside-ray
behavior (normally no entry hit). Fan sampling can miss narrow objects between
rays. Hit handles become invalid when their objects/components are destroyed.

## Named colors

Use RGB vectors `color.red`, `green`, `blue`, `white`, `black`, `yellow`, `cyan`,
`magenta`, `gray`, `orange`, `purple`, and `pink`:

```lua
local mesh = self:getComponent(MeshRenderer)
if mesh then mesh.color = color.red end
local tint = color.orange
-- For material overrides use mesh:setMaterialColor(tint.x, tint.y, tint.z).
```

Colors are ordinary per-script RGB tables, not integers or HDR/alpha colors.
