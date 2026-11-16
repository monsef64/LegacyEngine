# CPU/frame pacing, raycast thickness and particle atlases

## Changes and limits

- Removed the unused full static-scene fingerprint scan from each viewport draw.
  Bake acceptance still validates its scene snapshot; persistent atlas validation
  still checks manifests and assignments.
- Particle undo signatures now append settings values directly rather than
  allocate a JSON DOM and serialize every emitter on every editor frame. Runtime
  particle arrays remain excluded. Undo observation is not throttled.
- Dirty-title content scans run at most five times per second. Project-open and
  exit decisions still perform fresh synchronous dirty checks; the delayed title
  marker must not bypass an unsaved-change prompt.
- Unchanged spotlight cookies reuse their atlas layers rather than blitting every
  visible image every viewport/frame. Texture upload generations invalidate
  cached layers on content reload, even if OpenGL reuses the same texture name.
  Light reordering, project changes, missing images and async loading retain
  their existing behavior.
- Editor Settings > **FPS limit (VSync off)** defaults to 120 (0 = uncapped,
  maximum 240). The limiter sleeps rather than spins. VSync takes priority in
  focused windows to avoid fractional-refresh double pacing/half-rate stutter.
  Unfocused/minimized windows target 30 FPS, including when VSync is on; a focused
  detached Game window retains foreground pacing. The limiter never tries to
  make an overloaded frame run faster. Present profiler time includes waiting.

These remove identifiable redundant work and busy-loop risk. They are **not a
measured FPS/CPU improvement claim** on a representative user scene. Shadows,
physics, scripts, transparent fill-rate and driver synchronization can still
bottleneck a scene. Use Windows > Profiler to distinguish work from Present wait.

## Raycast visualization

Editor Settings > **Raycast line thickness** is saved globally, range 1–8 viewport
pixels. It changes editor visualization only, never query distance/radius, physics
or Game rendering. Widths above one use OpenGL 3.3 geometry-shader screen-space
expansion, avoiding Windows drivers that only support one-pixel `glLineWidth`.
The line/fan/sphere debug outlines all use the setting. Near-plane clipping is
handled before extrusion. A shader failure logs an error and falls back to thin
lines. Collider/grid/camera wire thicknesses are unchanged.

## Particle sprite sheets

New emitters and Fire/Smoke/Sparkles presets keep the complete sprite by default.
In the particle Inspector enable **Texture animation > Use Sprite Sheet**, assign
a texture atlas and set columns/rows/frame rate. Animation starts at the top-left
and loops cells. Frame rate zero freezes the first cell. Disable the switch to
show the full texture again without losing grid settings.

Lua: `ps.useSpriteSheet = true` (or false). Settings persist and participate in
component clipboard/reset, duplicate/prefab copy and undo. Older scene data with
an explicit multi-cell grid migrates to enabled; ordinary old/new single sprites
remain disabled. No project format version bump is needed: particle settings are
backward-compatible optional JSON fields.

## Manual verification — no automated tests added/run

1. Compare a fixed scene/camera before and after using the Profiler and Task
   Manager: empty Edit, many emitters, baked scene and multiple cookies. Compare
   working frame time separately from Present waiting. Record CPU/GPU models,
   resolution, VSync, light/shadow settings and active viewports.
2. With VSync off test foreground 60/120/240/0 caps, background/minimize and
   detached Game focus; check CPU utilization and input responsiveness. With
   VSync on test 60/120/144 Hz displays for accidental half-refresh pacing.
3. Edit a saved scene and immediately Open Project/Open Recent or Quit (before
   the 200ms title refresh). Verify Save/Discard/Cancel, undo back to saved state,
   autosave, external script and animation dirty states.
4. Move/reorder cookie lights; update/delete/restore image contents, clear/reassign
   cookie, rename/move assets and switch projects with identical paths. Check
   that atlas reuse does not retain another light's or old image's pixels.
5. Vary raycast thickness at different viewport sizes, DPI scales, perspective/
   orthographic views and near-plane crossings. Verify saved settings after
   restart and unaffected collision query results/other wireframes.
6. Toggle particle sheets, change rows/columns/frame rate, freeze at zero, switch
   presets, save/reload old and new scenes, copy/paste/reset and undo/redo; use Lua
   and verify runtime particles are still excluded from history snapshots.
7. Manually validate MinGW-w64 compilation and shader execution/visual quality on
   Windows. Headless Linux compilation does not validate these or prove smooth FPS.
