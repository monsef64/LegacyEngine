# Game-mode performance checks

## Changes

- Static bake validation hashes typed values instead of constructing and dumping
  JSON for every static object on every viewport render. A reused record buffer
  avoids per-object serialization allocations. Fingerprints remain order/ID
  independent, include material tint and static poses/lights, and detect motion.
- During Play, when the Game view is visible and the Scene view is unfocused /
  unhovered (or Game runs in a separate window), Scene and its camera preview
  render at approximately 15 Hz. Scene resizing forces an immediate redraw.
  Focus/hover Scene, pause, or stop to restore full-rate Scene rendering.
  The Game viewport keeps rendering every frame. Input/simulation are not throttled.
- Separate Game presentation temporarily disables its swap interval and restores
  the primary interval afterward, avoiding two VSync waits on drivers that would
  otherwise wait for both editor and Game windows. VSync preference is unchanged.
- Starting Play cancels a pending lighting bake to release background CPU work.

Numeric fingerprints differ from the older JSON fingerprints: rebake existing
lightmaps once to restore valid baked lighting with the current format.

## Manual verification (no automated tests added or run)

1. Use the same scene, resolution, light/shadow settings and hardware for before/
   after comparisons. Open **Debug > Profiler** and compare Game, Scene, light/
   shadow setup, editor and presentation times, not just the FPS label.
2. In docked Game Play, verify Game remains full rate while unfocused Scene and
   camera preview refresh at about 15 Hz. Focus/hover Scene; verify smooth editor
   freecam and full-rate redraw. Pause/stop, resize panels and hide/reopen views.
3. Repeat with a separate Game window and VSync on/off. Verify no two-window
   half-refresh-rate presentation cap and that the saved VSync setting is retained.
   Swap interval behavior is platform/driver dependent; validate Windows separately.
4. Start a high-sample lighting bake and enter Play. Confirm it cancels, does not
   publish a partial bake and cleans up unfinished files.
5. Verify moving static objects / changing static lights still invalidates bakes,
   undo restores a matching bake, and save/reload preserves fingerprint validity.
6. If FPS is still low, record the profiler breakdown and scene object/light counts.
   Realtime shadow passes, physics, scripts, skinning and GPU fill cost still exist;
   these changes do not guarantee a particular FPS or replace GPU profiling.

Linux compilation/linking is checked; no runtime performance improvement or
platform-specific presentation behavior has been measured in this workspace.
