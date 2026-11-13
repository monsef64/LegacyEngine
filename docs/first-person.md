# Character controllers and FPS cameras

## Quick start

1. Create **Character Controller** from the Create menu or Hierarchy context menu.
   Place its center at `(0, 2, 0)`. It has a Transform, a capsule Collider (radius
   0.35, cylinder height 1.1), a dynamic RigidBody, and CharacterController.
2. Create a Plane with its Collider at ground level. Keep the character upright,
   uniformly scaled, and without a parent for predictable physics.
3. Copy [`scripts/examples/character_controller_fps.ls`](../scripts/examples/character_controller_fps.ls)
   into the project's Assets scripts folder and attach it to the character.
4. Play and focus Game. The script creates/activates a linked FPS camera.
   **WASD** moves, **Left Shift** sprints, **Space** jumps, and the mouse looks.
   **Escape** releases the cursor; click Game to recapture it.

You can also create **FPS Camera** from either creation menu. It includes the
normal Camera component and AudioListener. Adding FPSCamera to an existing object
through Inspector or scripting adds its Camera and Transform dependencies.

## CharacterController scripting

```lua
local controller = self:getComponent(CharacterController)
controller:move(0, -1) -- local forward (-Z), direction not displacement
controller:sprint(true)
controller:jump()
controller.canMove = false -- stop horizontal motion
controller.canJump = false
controller.canSprint = false
```

`move(x, z)` stores local horizontal direction; `move({x=..., y=0, z=...})` and
`move(x, 0, z)` are also supported. Diagonal input is clamped to unit length.
**Do not multiply by deltaTime**: physics applies speed at its fixed timestep.
Call `move(0, 0)` to stop. Commands persist between frames until replaced.
`sprint(enabled)` stores sprint intent, defaulting to true when omitted.
`jump()` queues one request, consumed at the next fixed step only when grounded
and allowed. Use `isKeyPressed`, not `isKeyDown`, to avoid automatic repeated jumps.

| Property | Access | Meaning |
| --- | --- | --- |
| `isGrounded` | read-only | Ground probe hit a surface with upward normal > 0.55 and the body is not ascending |
| `isJumping` | read-only | Airborne and ascending; false while falling |
| `isMoving` | read-only | A nonzero movement command is permitted; not an actual-distance measurement |
| `isSprinting` | read-only | Moving with sprint requested and permitted |
| `canMove`, `canJump`, `canSprint` | read/write | Independent ability gates |
| `speed` | read/write | Normal horizontal metres/second (4.5) |
| `sprintMultiplier` | read/write | Speed multiplier, at least 1 (1.7) |
| `jumpSpeed` | read/write | Upward launch velocity (6) |
| `groundProbe` | read/write | Distance below feet for ground detection (0.12) |

State updates at physics steps, so an Update script sees the last fixed-step state.
This is a dynamic upright capsule, not a navigation/animation controller. Collision
resolution and gravity come from the existing physics solver, with rotation locked
against physics torque. It does not implement stair stepping, moving-platform
attachment, crouching, or configurable slope climbing. A downward center ray is
used for grounding; narrow ledges can report airborne. CharacterController requires
Transform, Collider, and a positive-mass RigidBody. Remove the controller first to
remove those dependencies. Use scripts to supply input; it has no automatic WASD.

## FPSCamera scripting

```lua
local character = self:getComponent(CharacterController)
local camera = Camera.new("Player FPS Camera")
local fps = camera:addComponent(FPSCamera)
fps:linkToCharacter(character, {x=0, y=0.7, z=0})
fps.isFreecam = false
camera:setFieldOfView(75)
camera:setNearClipPlane(0.05)
camera:setActive(true)
lockCursor()
hideCursor()
```

`linkToCharacter(characterController, offset)` accepts a component or its GameObject.
It parents the camera and sets the **local** position offset, default `(0, 0.7, 0)`.
The offset is measured from the capsule center, not its feet. Character yaw follows
mouse look; camera pitch stays local, clamped to ±89°. Linking resets local rotation
and disables the Camera's separate target/follow mode. Other Camera features
(FOV, projection, clips, viewport, target/follow/shake, conversions) remain accessible
through the owning camera GameObject, its Camera component fields, or FPSCamera
(e.g. `fps:setFieldOfView(75)` and `fps.fieldOfView`). Native FPS mouse look
owns orientation while enabled; set `fps.mouseLook=false` when driving rotation with
scripts or target tracking. There is no duplicate mouse-look code needed in scripts.

An **unparented** camera with `fps.isFreecam=true` moves using native WASD and Q/E
while Game input is focused. Parented cameras never move freely. `isFreecam=false`
disables translation, not looking. `fps:setIsFreecam(bool)` and `fps:getIsFreecam()`
are method equivalents. `fps.speed` controls freecam speed (6 m/s),
`fps.sensitivity` controls degrees per mouse pixel (0.12), and `fps.mouseLook`
enables/disables native look. Looking requires Game mouse input and a locked cursor.
Only the active, effectively enabled camera updates. Pause freezes both systems.

Configuration survives level/prefab save/load, duplication and undo/redo; transient
movement requests, camera look accumulation and controller state are not serialized.

## GameObject direction vectors

`getForward()`, `getBackward()`, `getRight()`, `getLeft()`, `getUp()`, and `getDown()`
return normalized **world-space** axes derived from world rotation, ignoring scale.
Forward is local -Z, backward +Z, right +X, left -X, up +Y, down -Y.

```lua
local forward = self:getForward()
local position = self:getPosition()
debug.log(forward.x, forward.y, forward.z)
```
