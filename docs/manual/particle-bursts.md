# Blood and Explosion particle presets

Select a ParticleSystem in the Inspector and choose **Blood** or **Explosion**
from Preset. Both replace the current particle settings, clear existing particles,
and use one-shot bursts with no continuous emission. Sprite sheets and collision
remain off by default. They reuse the bundled sprites; no texture import is needed.

- **Blood**: 60 small red alpha-blended droplets, cone emission, gravity and a dark fade.
  This is a particle effect, not a persistent decal or surface-staining system.
- **Explosion**: 140 additive fire particles emitted in all directions, expanding
  and slowing rapidly as they fade. It does not apply damage, forces or sound.

```lua
local particles = self:getComponent(ParticleSystem) or self:addComponent(ParticleSystem)
particles:setPreset("Blood") -- or "Explosion"
particles:play()
-- Trigger the burst again later:
particles:restart()
```

As with existing presets, applying one resets its settings and playback state;
`playOnAwake` is enabled. In runtime scripts call `play()` explicitly to trigger
immediately. You can then customize any particle field through Inspector or Lua.
Stop Play discards runtime changes.

## Manual verification

- Verify all five Inspector presets and Lua names remain available.
- Preview Blood and Explosion: one burst, no looping; particles finish and fade.
- Restart each effect several times, and pause/resume midway.
- Confirm sprite-sheet animation remains disabled after applying either preset.
- Save/reopen an Edit-mode scene with each preset and compare its settings.
- Verify invalid Lua preset names report the accepted names without altering settings.
