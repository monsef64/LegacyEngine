# Weapon camera recoil example

1. Add a Camera (ordinary/FPS/TPS) and **Cameras > Recoil** to its GameObject.
2. Attach `WeaponCamera.lua`; create a **Fire** binding in Project > Input Settings.
3. Optionally drag an Animation component into `weaponAnimation` and a valid
   nonlooping `.leanim` into `reloadClip`. These fields are declared in the example.

4. Play, hold/release Fire, then invoke the named component's `reload()` or
   `previewBurst()` from another script's lifecycle/task callback.

The effect is visual; this example does not fire projectiles, damage targets or
rotate a weapon mesh. Timed impulses use game time and pause with the game. Prefab
and animation assets assigned in exposed fields participate in level preloading.

The reload coroutine waits for the next natural finish. Interrupting playback,
looping the clip, or a runtime clip-load error will not emit Finished. Production
weapon logic should explicitly cancel/recover its reload state on interruption
and report `animation.error`. Stop Play/destruction cancels the waiting task.

For finite bursts set `burstCount`, for repeated patterns use `setSequence` and
`sequenceLoop`. `stop()` stops future kicks but allows recovery; `reset()` also
zeros the current offset. Stop firing before removing just this script from a
still-live camera, because the independent Recoil component continues simulating.
