# Manual verification: recoil, level preloading and animation waits

No automated tests should be added/run for this checklist. Verify on Linux
OpenGL and Windows MinGW-w64, both editor Play and exported player.

## Recoil

- Add Cameras > Recoil to an ordinary camera, FPS camera and TPS camera. Play and
  trigger via Inspector and script. Try pitch/yaw/roll, back/up/side, randomness,
  damping below/equal/above one, distinct frequencies, recovery and strength.
- Trigger rapidly: offsets stack but stay inside maxAngle/maxDistance. Compare
  30/60/144 FPS; long frames must not explode the spring or fire unbounded shots.
- Stop firing: spring recovers; Reset clears immediately. Disable/re-enable,
  deactivate/reactivate owner, pause/resume Play, switch active camera, reparent,
  delete owner and Stop Play. Transform/controller aim must not drift.
- Burst delay, spacing, finite count, finite duration, unlimited mode and
  playOnStart. Sequence with simultaneous keys, loop gap, delay, edited ordering,
  empty keys, 256-key limit and setSequence rejecting unsorted/nonfinite values.
- Save/reload level/prefab; undo/redo, copy/duplicate, header Reset and multi-edit.
  Runtime offsets/timers must not appear in saved settings or leak to duplicates.
- Invalid script fields, negative burstCount/seed, out-of-range numbers and removed
  component handles must report script errors, not crash. Unknown properties are
  errors. Recoil without Camera is allowed but visually inert.
- Read data/examples/recoil/README.md, define Fire, attach WeaponCamera.lua and
  assign its animation reference/clip. Verify named reload and previewBurst calls.

## Animation event tokens

- In lifecycle/task callbacks, play an assigned clip then wait(animation.onStarted),
  wait(animation.onFinished), wait(animation.onStopped), wait(animation.onRestarted),
  wait(animation.onPaused) and wait(animation.onResumed), using independent tasks
  where necessary. Tokens are next-occurrence events, not completed-state latches.
- Natural nonloop completion only emits Finished; looping emits Restarted rather
  than Finished. Replay emits Started and Restarted, new clip emits Started.
  Stop/Reset while active emits Stopped; no Finished for interruption.
- Test loaded and asynchronously loaded clips, zero-duration crossfades, invalid
  clips, deleted owners, removed/re-added Animation, inactive owners, runtime reset,
  project/level changes and Stop Play. Old tokens must not wake on replacement
  components. Existing onAnimation* callbacks and audio waits must continue working.
- Syntax/type/completion docs show the new tokens and Recoil properties/methods.
  Expose Recoil fields and verify component-reference injection/drag-drop.

## Level preloading

- Transition between levels with cold/warm model and texture caches, assigned
  prefabs (including nested references and exposed overrides), audio and animation
  clips. Until the preload gate finishes, current world/scripts remain active.
  Destination onStart/onLevelLoaded occur only after cache publication.
- Start the exported player and enter editor Play: centered Game-only Loading.. text precedes
  scripts. Missing/invalid assets, a full worker queue and resource-budget failures
  must report an error and preserve the old level/editor world. Close during the
  loading text; the close event must remain available to normal quit handling.
- Cancel by switching project/stopping runtime, then request a different level.
  No worker may publish an abandoned world. Prefab caches are keyed by canonical
  path, file metadata and built-in mesh handles; edit/reimport and load again.
- Profile first camera movement, first spawn, first animation and first sound.
  Compare frame times against the previous engine. ECS instantiation, GPU driver
  uploads, terrain/physics/UI work and dynamic paths may still incur costs; a build
  passing is not proof of zero lag spikes.
- Large models, many textures and levels beyond cache budgets should fail rather
  than hold the preload gate forever. Check the two-minute watchdog and responsive
  close handling. Verify UI/focus/cursor and editor Play snapshot restore afterward.
- Export dependency closure must still include all registered levels by default;
  the level-preload-only dependency mode must not change export package contents.
