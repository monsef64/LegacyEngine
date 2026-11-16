# Particle systems

## Editor

- Select a GameObject and choose **Add Component > Particle System**. It requires
  a Transform (added automatically). Choose **Fire**, **Smoke** or **Sparkles**
  from the component's Preset dropdown. Selecting a preset resets its settings.
- The selected emitter previews in the Scene viewport in Edit mode. A small
  bottom-right **Particle Effect** panel provides Play/Pause/Resume, Stop,
  Restart, playback speed (0.05–4x), cycle time and live count. It disappears when
  another object is selected or the Scene view is hidden/too small. Runtime
  controls are disabled in this editor panel while Play mode is active.
- Presets use generated soft-alpha sprites bundled under `data/particles`.
  Override Texture with a project image for rain, snow, dust, debris or another
  billboard effect. Sprite-sheet columns/rows and frame rate animate custom sheets,
  starting at the top-left and looping frames. Empty override uses the bundled
  sprite selected by Sprite. Custom textures stream asynchronously with a bundled
  sprite fallback while unavailable; sprites do not cast shadows or receive GI.
- Settings include duration/loop/delay, rate and one burst per cycle, capacity,
  deterministic seed, Point/Box/Sphere/Cone shapes, radius/box size/cone spread,
  direction, speed range, extra velocity, acceleration/gravity, drag, local/world
  space, lifetime range, size range/end size, start/end RGB and alpha, initial
  rotation/angular velocity, alpha/additive blending, and sprite-sheet animation.
  Size/color/alpha interpolate linearly over life. The end size is an absolute
  size, not a multiplier. Velocity/gravity use local axes in local space and world
  gravity in world space; initial direction/velocity are rotated by the emitter
  when spawning world-space particles. Emitter max scale controls billboard size.
- Settings persist in levels/prefabs and participate in undo/redo, Reset, component
  value copy/paste, object duplication and subtree clipboard copying. Live particles
  and playback state are not serialized; copies start fresh. Entering Play clears
  editor simulation and honors Play on awake. Pausing Play freezes simulation.

## Scripting

`ParticleSystem` works with add/get/remove, scene and hierarchy component lookup,
component aliases (`object.particleSystem`) and typed script exports:

```lua
@export effect ParticleSystem

function onStart()
    local ps = self:getComponent(ParticleSystem)
    -- Or assign effect by dragging a component/GameObject to the export.
    ps:setPreset("Sparkles")
    ps.playOnAwake = false
    ps.rate = 0
    ps.loop = false
    ps.shape = ParticleSystem.Sphere
    ps.blend = ParticleSystem.Additive
    ps.color = {1, 0.7, 0.2}
    ps.gravity = {x = 0, y = -1, z = 0}
    ps:emit(50)
end
```

Methods: `play()`, `pause()`, `resume()`, `stop(clearParticles=true)`, `restart()`,
`clear()`, `emit(count)` (0–10000), and `setPreset("Fire"|"Smoke"|"Sparkles")`.
`stop(false)` stops emission and lets remaining particles drain. `clear()` removes
live particles without disabling future emission. Read-only values: `isPlaying`,
`paused`, `particleCount`, `time` (emitter cycle time).

Writable properties use the settings names:

- `loop`, `playOnAwake`, `localSpace`, `duration`, `startDelay`, `rate`,
  `maxParticles`, `burstCount`, `seed`.
- `shape` (Point=0, Box=1, Sphere=2, Cone=3), `radius`, `boxSize`, `coneAngle`,
  `direction`, `speedMin`, `speedMax`, `velocity`, `gravity`, `drag`.
- `lifetimeMin/Max`, `sizeMin/Max`, `endSize`, `color`, `endColor`, `alpha`,
  `endAlpha`, `rotationMin/Max` (degrees), `angularVelocity` (degrees/sec).
- `blend` (Alpha=0, Additive=1), `sprite` (Fire=0, Smoke=1, Sparkles=2),
  `texture`, `sheetColumns`, `sheetRows`, `frameRate`.

Assign complete vector tables `{x,y,z}` or `{x=...,y=...,z=...}`. Returned vector
values are copies: editing `ps.gravity.y` alone does not modify the emitter.
Values are validated; raise a range's maximum before raising its minimum past
that maximum. Changing localSpace or seed restarts existing simulation.

## Rendering and bounds

CPU simulation updates once per application frame, never once per viewport.
Instanced OpenGL billboards use depth testing without depth writes. Alpha
particles sort back-to-front within each emitter; different emitters and other
transparent objects are not globally sorted. Additive blending is available only
for particles; this does not change animation blending.

Limits: 10000 live particles per emitter, 50000 globally / per rendered view,
rate <=10000/sec, lifetime <=120 sec, dt capped at 0.1 sec. These bounds prevent
unbounded catch-up/allocation but are not an FPS guarantee. High counts and large
smoke quads can be fill-rate expensive. This is a billboard system, not collision,
mesh-particle, trails, subemitter or arbitrary curve/noise simulation. Particle
rendering does not participate in lightmap baking.

Legacy text schema is version 22 / JSON version 16; older supported scenes load
without particle components. New particle settings are stored as validated JSON.

## Manual checks (no automated tests added or run)

1. Add an emitter and apply each preset. Inspect the generated sprites visually
   for orientation, tinting, soft alpha, and absence of rectangular borders.
2. Check every shape, local/world transform behavior, lifetime/speed/size ranges,
   gravity/drag, fades, rotation, duration/delay, loop, burst and capacity limits.
3. Play, pause, resume, stop and restart the selected Scene preview; change its
   speed, deselect/reselect, hide Scene, and move/resize/dock the viewport. Verify
   playback clicks do not select/move objects or break Scene asset drag/drop.
4. Enter/stop/pause/resume Play and test docked/separate Game views and multiple
   cameras. Simulation speed must not multiply when more views are rendered.
   Edit preview should not appear in the Game view outside Play mode.
5. Save/reload levels and prefabs; duplicate/copy/paste emitter hierarchies; use
   component Reset/copy/paste and undo/redo. Verify settings persist, internal
   ParticleSystem exports remap, and runtime particles do not persist to disk.
6. Exercise every Lua method, settings type/range errors, destroyed/removed handles,
   `self.particleSystem`, scene/hierarchy lookup, and component export assignment.
   Verify extension completions and export diagnostics recognize ParticleSystem.
7. Try a custom alpha texture and sprite sheet; move/rename it, switch projects,
   and remove/restore the file. Verify streaming fallback and root-cache isolation.
8. Stress the emitter/global budgets and compare frame times with alpha smoke and
   additive sparks. Verify visuals and Windows MinGW-w64 manually; Linux compile/
   link validation does not establish shader execution, image quality or FPS.
