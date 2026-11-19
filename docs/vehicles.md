# Vehicle physics and scripting

## Quick start

1. Use **Create > Vehicle**. This creates a unit-scale physics root with a 1000 kg
   RigidBody, a 1.8 × 0.6 × 4 metre box Collider, Vehicle, and a scaled chassis
   visual child. Alternatively use **Add Component > Vehicle** on an existing
   object. Missing Transform/RigidBody/Collider dependencies are supplied;
   existing components are not overwritten.
2. Place the root about 1.2 metres above a broad plane or Terrain with collision.
   Local **-Z is forward**, +Y is up. Keep the physics root at unit positive scale
   initially; scale the visual child to fit your model instead.
3. Attach [vehicle_basic.ls](../scripts/examples/vehicle_basic.ls) to the root's
   LegacyScript component. Supply a Game camera overlooking the course, or parent
   a camera to the vehicle for a simple follow view.
4. Play and focus the Game view. **W/S** accelerate/reverse, **A/D** steer,
   **Space** service brake, **Left Shift** rear handbrake.

Select the root in Scene to see orange wheel circles and suspension lines. These
are setup guides, not rendered tires. The preset has a visible chassis but does
not generate tire meshes, an animated steering wheel or a following camera.

## Physics model

Four rays sample the road under the chassis. Suspension compression and relative
contact velocity produce spring/damper support. Tire impulses combine lateral
grip, engine force and braking with a shared load-dependent friction budget.
Impulses at contact points produce pitch/roll/yaw through the existing Jolt solver;
there is no scripted Transform movement. Dynamic ground receives the opposite
impulse. Wheel rays ignore triggers and the vehicle's own descendant colliders.

The basic model is **all-wheel drive**, **front-wheel steering**, with a
**rear-wheel handbrake** that reduces rear lateral grip. Negative throttle drives
in reverse; it does not automatically apply a transmission or service brake.
Wheel commands persist until overwritten. Suspension/traction operate only at
fixed physics steps while simulation advances, not in Edit mode or while paused.

A dynamic, positive-mass RigidBody and non-trigger primitive chassis Collider are
required. A static/kinematic chassis, mesh collider, disabled entity/component,
invalid settings or CharacterController disables the vehicle forces. Do not put
Vehicle and CharacterController on the same root. Removing dependencies through
the Inspector/Lua is blocked until Vehicle is removed. Removing Vehicle leaves
its existing dependencies intact.

## Inspector and Lua API

```lua
local car = self:getComponent(Vehicle) or self:addComponent(Vehicle)
car.throttle = 0.8       -- -1..1, positive is forward
car.steering = 0.25      -- -1..1, positive is right
car.brake = 0           -- 0..1, service brake on all four wheels
car.handbrake = false   -- rear brake/sliding
car.enabled = true
local kmh = car.speed * 3.6
local onRoad = car.isGrounded
local contacts = car.groundedWheels
local compression = car.wheelCompression -- copied 1-based array, each 0..1
```

`speed` is signed forward speed in m/s; `isGrounded`, `groundedWheels` and
`wheelCompression` are read-only snapshots from the latest vehicle step. The
compression array order is front-left, front-right, rear-left, rear-right.
Settings below can be changed from the Inspector or Lua. Invalid/out-of-range
Lua values are rejected. Changing a setting preserves the current input commands.
Vehicle supports normal component lookup/add/remove, parent/child component
searches, `findGameObjectsByComponent(Vehicle)` and exported Vehicle references.

| Setting | Default | Range / units |
| --- | --- | --- |
| `wheelBase` | 2.4 | 0.2–20 m, front/rear separation |
| `trackWidth` | 1.5 | 0.2–10 m, left/right separation |
| `mountHeight` | -0.2 | -10–10 m, mount Y relative to root |
| `wheelRadius` | 0.35 | 0.05–3 m |
| `suspensionLength` | 0.45 | 0.05–3 m, maximum downward extension |
| `springStrength` | 35000 | 100–500000 N/m, per wheel |
| `suspensionDamping` | 4500 | 0–50000 N per m/s, per wheel |
| `engineForce` | 8000 | 0–200000 N, total before grip limits |
| `brakeForce` | 12000 | 0–300000 N, total service brake capacity |
| `tireGrip` | 1.5 | 0.05–5, coefficient relative to wheel support load |
| `maxSteerAngle` | 30 | 0–70 degrees, front wheels |
| `maxSpeed` | 45 | 1–150 m/s, engine cutoff (not a hard velocity cap) |

Dimensions follow root scale; use positive unit scale for predictable tuning.
Defaults target approximately 1000 kg. Tune spring/damping if you keep a very
light/heavy existing RigidBody. Suspension support is capped for numerical safety;
this is not a solver constraint. Road material friction is not a separate tire
compound model; tire grip comes from `tireGrip` and wheel load.

## Persistence and limitations

Configuration is saved with the Vehicle component. Throttle, steering, brakes,
speed, contacts and compression are runtime state and are not serialized. New
projects use legacy text bridge version 30; older files remain readable and have
no vehicle unless one is added. Copy/duplicate preserves configuration, not live
commands. Undo tracks configuration; Play/Stop uses the normal world snapshot
so runtime edits do not leak into saved edit state.

This is a lightweight four-wheel raycast vehicle, not a full automotive simulator:
no swept/physical tire colliders, tire rotation meshes, engine RPM/gears/clutch,
differential, anti-roll bars, ABS, traction control or automatic recovery. Narrow
obstacles/curbs can be missed by wheel rays, and wheels may clip side obstacles.
No airborne steering is applied. Chassis collisions still use normal physics.
Desktop driving feel and performance have not been measured or play-tested here.

## Manual verification

No automated tests are added or run for this feature. On the desktop build:

- Create the preset, attach the sample, check suspension support at rest, then
  accelerate, reverse, steer both ways, brake and use the handbrake.
- Test slopes, ramps, jumps, walls and dynamic platforms. Verify no airborne
  engine/steering force, no self-hits and no trigger-road support.
- Inspect wheel guides, signed speed, compression and grounded count. Test one,
  two and four contacts; ensure a rolled-over vehicle cannot drive upside down.
- Change mass and suspension tuning. Check positive scale and collider offsets.
- Toggle enabled; add/remove Vehicle; destroy/duplicate the root with children.
  Confirm disabled/kinematic/static chassis do not receive vehicle impulses.
- Save/reopen, prefab round-trip, load older scenes, undo/redo configuration,
  Pause/Resume and Play/Stop. Runtime inputs must not be saved.
- Verify Lua type tokens and string names, exported Vehicle references, hierarchy
  searches, read-only fields and rejection of NaN/out-of-range controls/settings.
- Confirm existing rigid-body, river buoyancy and character workflows still work
  on Windows/MinGW; the Linux console build cannot validate desktop driving.
