# Lean Jolt configuration — manual verification

Jolt remains the physics backend. Its debug renderer, built-in profiler,
ObjectStream metadata and C++ RTTI are disabled. Required engine collider
wireframes, editor profiler, JSON serialization, collision shape registration,
queries, vehicles and character code remain intact. Physics-only LTO is enabled
when supported; section garbage collection was already enabled.

## Checks (not executed in this sandbox)

- Load old project/level files and save/reopen; verify all collider shapes,
  layers, triggers, body types and physics component fields round-trip.
- Exercise static/dynamic/kinematic collisions, mesh/terrain/hole collisions,
  raycasts and sweeps, joints, vehicles, FPS/TPS movement and slopes/steps.
- Check contact/trigger enter/stay/exit Lua events, scene switching, Play/Stop,
  runtime rollback, undo/redo, and quit while physics workers are active.
- Enable editor/Game collider overlays: these must still render using engine
  wireframes. Verify the engine frame-profiler UI remains functional.
- Build Release on Windows MinGW-w64 and Linux. Repeat with
  `LEGACY_PHYSICS_LTO=OFF` if the platform linker cannot handle IPO. No MSVC path
  or CPU baseline change is introduced.
- Compare identically stripped standalone players before/after using the same
  compiler/options. Do not compare the LTO static archive (it contains compiler
  IR), or interpret whole-player savings as Jolt's isolated on-disk size.

No automated tests were added or run for these changes. Native compilation and
size measurements do not establish that runtime physics behavior is unchanged.

## Native Linux measurement

The existing Release build used GNU C++ 12.2.0 on x86-64 Linux. Copies of the
standalone player were stripped with `strip --strip-unneeded` for both readings:

| Player | Bytes | MiB |
| --- | ---: | ---: |
| Before this task | 9,191,680 | 8.77 |
| After this task | 8,634,624 | 8.23 |

Reduction: **557,056 bytes (6.1%)**. This is the complete player,
including this task's first-use loading changes, not an isolated Jolt symbol-size
measurement. The launcher is editor-only. Windows and compressed release-package
sizes were not measured. Both native editor/player targets built successfully;
these measurements are not runtime physics validation.
