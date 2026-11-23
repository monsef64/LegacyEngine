# Camera recoil

Add **Cameras → Recoil** from the Inspector's Add Component menu. Attach it to the
same GameObject as a Camera (FPS, TPS, or an ordinary camera). The component can
exist without a camera, but then has no visible effect. It is independent of
Camera Effects and ordinary camera shake.

Recoil is a six-axis local-space, damped spring offset applied only to the game
camera's rendered pose. It does not rotate/move the saved Transform, change an FPS
controller's aim angles, affect physics, or move children such as a weapon mesh.
This avoids controller feedback and accumulated Transform drift. Camera-driven
weapon raycasts should deliberately incorporate recoil if gameplay spread/aim is
meant to follow it; do not assume the Transform was changed by a visual effect.
The level-editor freecam is unaffected.

## Triggering from scripts

```lua
expose Recoil recoil

function onStart()
    recoil = recoil or self:getComponent(Recoil)
    if recoil then
        recoil.pitch = 2.4
        recoil.yaw = 0.15
        recoil.roll = -0.3
        recoil.back = 0.025
        recoil.frequency = 8
        recoil.damping = 0.85
        recoil:trigger(1)
    end
end
```

`trigger(multiplier = 1)` adds one impulse. Multiplier is 0–10. `start()` begins a
repeat/burst timer; `stop()` cancels further shots and lets the springs settle;
`reset()` stops timers and clears offsets/velocities immediately. Calling `start`
again restarts the timer. Disabling the component clears transient recoil. Parent
activity, editor Pause and game time suspend simulation. Inspector trigger/start
controls require Play mode to advance the simulation.

## Settings

All settings are editable in Inspector and scripts. Invalid script values report
an error without partially applying the edit. Settings, not running timers or
spring offsets, are serialized in scenes/prefabs and tracked by undo/multi-edit.
Copying a component through a prefab/duplicate resets its transient state.

| Setting | Meaning |
| --- | --- |
| enabled | Enable simulation and visual offset. |
| pitch / yaw / roll | Angular kick, -45–45 degrees per axis. Positive pitch lifts the view. |
| back / up / side | Positional kick, -1–1 metres. Positive back is camera-local +Z. |
| rotationRandom | Independent signed variation on each angular axis, 0–45 degrees. |
| positionRandom | Signed positional variation as a fraction of absolute back kick, 0–2. |
| frequency / positionFrequency | Angular/position spring frequencies, 0.1–40 Hz. |
| damping / positionDamping | Damping ratios, 0.1–3: below one oscillates, one is critical, above one is overdamped. |
| recovery | Multiplies spring frequencies during recovery, 0.1–4. |
| strength | Global impulse multiplier, 0–10. |
| maxAngle / maxDistance | Combined angular/positional displacement caps, 0–89 degrees / 0–2 metres. Outward velocity is removed at the cap. |
| seed | Nonnegative per-component seed for variation. Reset or start a new instance to reseed. Not cryptographic randomness. |
| interval | Repeating shot spacing (0.01–60 seconds); also gap after the final scheduled key when looping. |
| burstCount | 0 repeats until stopped; otherwise 1–100000 shots. |
| duration | Burst duration after delay, 0–3600 seconds; 0 is unlimited. Earliest duration/count limit wins. |
| delay | Start delay, 0–3600 seconds. |
| playOnStart | Automatically start the burst, or sequence when keys exist, on the first active simulation update. |
| sequenceLoop | Loop the scheduled sequence with interval as the final gap. |
| isFiring | Read-only: a burst or sequence is running. Recovery may continue after false. |

Spring integration uses the analytic under/critical/overdamped solution and bounded
timeline substeps, rather than frame-dependent lerp. Long frame time follows the
engine's 0.25-second simulation clamp; it does not replay minutes of queued shots.
Repeated impulses stack within the displacement caps.

## Timed bursts

```lua
function fireBurst()
    local recoil = self:getComponent(Recoil)
    if not recoil then return end
    recoil.interval = 0.09
    recoil.burstCount = 3
    recoil.duration = 0
    recoil:start()
end
```

Do not trigger every rendered frame unless a frame-rate-dependent kick is
intentional. Starting/stopping recoil only drives this effect; it does not
fire weapons, emit muzzle particles or apply damage.

## Scheduled sequences

Author keys in the Inspector or replace them from scripts:

```lua
function firePattern()
    local recoil = self:getComponent(Recoil)
    if not recoil then return end
    recoil:setSequence({
        {time = 0.00, strength = 1.0},
        {time = 0.09, strength = 1.1},
        {time = 0.18, strength = 1.3}
    })
    recoil.sequenceLoop = false
    recoil:playSequence()
end
```

Up to 256 keys, sorted by time (0–3600 seconds), strength 0–10. Simultaneous keys
are allowed. `setSequence` validates the entire list, replaces it atomically and
stops firing; it does not reset the recovery spring. `playSequence()` restarts the
sequence after `delay`. An empty sequence stops on its next update. Burst and
sequence timers can run together if both are explicitly started; `stop()` cancels
both. The Inspector sorts edited keys by time.

See `data/examples/recoil/WeaponCamera.lua` for an input-driven example and an
animation-finish wait. Define **Fire** in Project → Input Settings first.
