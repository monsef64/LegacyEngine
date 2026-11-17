# CameraEffects, Scene zoom, and terrain performance

No automated tests are added/run. A successful Linux build does not validate GLSL,
Windows/MinGW runtime behavior or frame-time gains; use the checks below.

## Scene scroll zoom

Scene's toolbar now has a **Scroll zoom** multiplier (0.1x–5x). It scales ordinary
vertical-wheel zoom only: horizontal pan and right-look/Alt camera speed controls
keep their previous behavior. The multiplier persists in Editor Settings; older
settings files default to 1x. It does not change the game camera or input bindings.

## CameraEffects

Select an object with a Camera and use **Add Component > CameraEffects**. It can
be stored on any object, but renders only when that object's Camera is drawn.
The Game view/window and selected-camera preview apply it; the editor freecam,
asset thumbnails, GUI, gizmos and debug collider overlays are not post-processed.

The component defaults to enabled with **all individual effects off**. Without an
active effect the renderer takes its original path: no effect target allocation
or fullscreen draw. Targets are allocated lazily and retained for reuse (up to
two camera/view keys per Renderer), including separate Game/preview histories.
Most effects combine into one full-resolution pass; bloom alone adds three
half-resolution passes. Shader uniform locations and terrain locations are cached.

| Effect | Fast implementation / controls |
| --- | --- |
| Bloom | HDR bright extraction, half-resolution separable Gaussian blur; intensity and threshold |
| Color grading | Analytic exposure (stops), contrast, saturation, temperature; no LUT allocation |
| Vignette | Analytic radial attenuation; intensity |
| Depth of field | Six depth-weighted color taps, radius capped at six pixels; focus distance/range and strength |
| Motion blur | Four camera-reprojection color taps, clamped screen velocity and frame-time normalization; strength |
| Film grain | Procedural animated noise; intensity |
| Chromatic aberration | Two extra radial color-channel taps; strength |
| Anti-aliasing | Single-frame directional FXAA filter with early-out on low-contrast pixels; no TAA history or MSAA |
| Ambient occlusion | Six depth samples, reconstructed surface normal, screen radius capped at 32 pixels; world radius and intensity |

Effects capture the camera into an HDR color + sampleable depth/stencil target,
then composite to its original viewport. Depth is copied back for overlays.
Bloom targets allocate only when bloom contributes. Two fixed-quality view caches
avoid per-frame resize thrashing between Game and camera-preview resolutions;
additional views evict the least-recently-used entry. Costs scale with resolution
and selected effects, so enable only what you need. No FPS guarantee is implied.

### Limits

- Motion blur uses **camera motion only**, not skinned/object velocity buffers.
  Resize, long gaps, and large camera matrix changes reset its bounded history.
- DOF is a small-radius screen-space approximation, not cinematic bokeh. Focus is
  based on distance from the camera, rather than a physical thin-lens model.
- AO/DOF/motion blur use opaque depth; transparent particles/surfaces do not supply
  their own depth/velocity. Disocclusion, screen-edge and depth-boundary artifacts
  are possible. AO is not GI and cannot see offscreen geometry.
- FXAA may soften detail; it does not provide temporal/shimmer reconstruction.
- Color grading is parameter-based, not a custom LUT editor. The final standard
  output framebuffer clamps highlights as before; bloom extraction happens in HDR.

### Scripting (LegacyScript / Lua)

Attach this to the same Camera object:

```lua
local effects
function onStart()
    effects = self:getComponent(CameraEffects) or self:addComponent(CameraEffects)
    effects.enabled = true
    effects.bloom = true
    effects.bloomIntensity = 0.25
    effects.bloomThreshold = 1.0
    effects.antiAliasing = true
    effects.colorGrading = true
    effects.exposure = 0.2
    effects.vignette = true
    effects.vignetteIntensity = 0.25
end

function onUpdate(deltaTime)
    if isKeyPressed("B") then effects.bloom = not effects.bloom end
end
```

All Inspector parameters are readable/writable properties, with the same names
listed in `scripts/definitions/legacyengine.lua`. Boolean properties require actual
booleans; numeric values must be finite and within their documented ranges.
Standard add/get/remove, parent/child lookup, component aliases and exported
`CameraEffects` references work like other components. Save/load, prefab cloning,
copy/reset/removal and history capture include the component. Effects are not
light-mode controls and do not change Dynamic/Static lights from scripts.

## Terrain changes

- Frustum-cull CPU chunk bounds **before** GPU-cache lookup/allocation. Looking
  away no longer allocates invisible chunk geometry or evicts visible chunks.
- Cache conservative bounds in heightfield data. Only height-dirty chunks rescan
  their heights; paint edits do not recompute bounds. Neighbor-normal invalidation
  and collision updates retain their existing behavior.
- Use squared-distance LOD comparisons and cached shader uniform locations.
- Skip texture-bombing albedo/normal work for zero-weight layers. Derivatives remain
  unconditional/explicit for stable mip selection around blend boundaries.
- Keep the GPU cache at 256 chunks, but recycle already-submitted entries if all
  are used in a view instead of silently skipping remaining visible terrain.
  More than 256 simultaneously visible chunks may still cause upload churn;
  there is no claim that arbitrarily large scenes are free.

## Manual checks

1. Move the Scene zoom multiplier through its range; verify wheel zoom, horizontal
   pan, Alt/right-look speed controls, restart persistence and old settings loading.
2. Add CameraEffects, enable effects one at a time, then in combinations. Compare
   disabled/all-off views against the previous rendering path. Use bright geometry,
   high-contrast edges, foreground/background objects and AO contact corners.
3. Test Game panel, separate Game window, camera preview and non-full camera
   viewports. Check projection/aspect, clearing, resizing, overlays, no feedback
   loops and no post-processing of editor UI/freecam/asset previews.
4. For blur, move the camera, stop, switch cameras, resize, pause/resume and toggle
   effects. Check history resets, focus response, no full-screen streak on a cut,
   and the documented transparent/object-motion limitations.
5. Exercise script fields and exported references, invalid values, add/remove,
   component copy/reset, duplicate-with-children, prefabs and undo/redo. Save/reopen
   projects/levels; old files must load with no CameraEffects by default.
6. Check OpenGL driver logs for compile/link/FBO errors on Windows MinGW. Compare
   GPU/frame time and VRAM at 720p, 1080p and your target resolution, enabling effects
   individually. No automated performance/runtime tests were executed here.
7. Terrain: compare flat/single-layer versus four painted layers; move in/out of
   the frustum and across LODs. Sculpt/paint at chunk edges, import heightmaps,
   undo/redo, save/reload, bake and verify collision. Check no stale bounds or
   missing visible chunks in a scene exceeding 256 visible chunks. Measure upload
   churn rather than assuming the bounded cache removes all large-scene costs.


## Inspector and scripting updates

Camera effects are now individually expandable in the Inspector; expand an effect to
find its Enabled switch and parameters. Right-click individual numeric, boolean,
color and enum controls for Copy/Paste (the OS clipboard uses JSON values).
Component-header reset and component-copy context actions remain available.
Asset selection has an explicit **View: List / Grid** choice.
Camera near clip cannot be smaller than **0.020** metres.

Night vision uses `nightVision`, `nightVisionMode` (0 green, 1 infrared-style),
and `nightVisionIntensity` (0..1). Infrared is a false-color luminance visualization,
not a temperature simulation or a way to see through geometry. Nausea uses `nausea`,
`nauseaIntensity` (0..1) and `nauseaSpeed` (0..5). Both effects default off and share
the existing final postprocess pass, without extra render targets.

```lua
local fx = self:getComponent(CameraEffects)
fx.nightVision = true
fx.nightVisionMode = 1
fx.nightVisionIntensity = 0.8
fx.nausea = true
fx.nauseaIntensity = 0.2
fx.nauseaSpeed = 1
```

The Scene viewport supports right-mouse freecam, wheel zoom/speed and Alt-orbit
while Play runs. It does not move the gameplay camera. Script input is suppressed
while looking around the Scene viewport.

Runtime Lua errors abort the failing callback invocation and log its traceback,
but do not disable future callbacks or unrelated scripts. Lua cannot resume at
the statement after an error; syntax/top-level startup errors still require fixing.
The internal script editor shows line numbers alongside the source.

### Additional manual checks

- Copy/Paste one transform axis, a checkbox, color and dropdown; invalid clipboard
  data must not alter the value. Check undo after pasting in edit mode.
- Expand each effect and verify only its own parameters appear. Check green and
  infrared modes and zero/full nausea intensity in both Game and camera preview.
- Save/reopen the level and access new effect fields from Lua.
- During Play, navigate Scene, then return to Game and verify gameplay input works.
- Trigger a callback error twice and verify both tracebacks appear, other scripts
  continue, and the next valid invocation can still run.
- Check script gutter/caret alignment when scrolling horizontally/vertically,
  editing multiline text, and jumping to the end of a long file.

Scene/Inspector editing is allowed in Play, including transforms, components,
creation, duplication, deletion, parenting, model/prefab drops and terrain sculpting.
These edits are temporary: Stop restores the pre-Play world, selection, editor
camera and edit-mode history. Scene/project saves and autosave are blocked in Play.
Disk-writing asset authoring (material/model import settings, scripts, animation
assets, terrain save and lighting/profile bakes) is disabled rather than silently
writing temporary runtime edits into shared project assets. Scripts/physics may
overwrite a runtime value again on the next tick; pause to inspect it steadily.
Runtime undo/redo remains unavailable; edit-mode history is preserved.


## Fog and color fades

Add **Fog** through Inspector > Add Component. It affects both Scene and Game,
using scene depth and exponential distance attenuation. The first enabled, active
Fog in entity order wins; a Transform is not required. Color is RGB 0..1, density
is 0..10 inverse metres, start is nonnegative, and end must exceed start (up to
1,000,000 metres). Distance is capped at end, including sky pixels. Density zero
is clear. Fog shares the final postprocess pass, independent of CameraEffects'
master switch, and is saved with the scene in Edit mode.

```lua
local fog = self:getComponent(Fog) or self:addComponent(Fog)
fog.enabled = true
fog.color = {x=0.5, y=0.6, z=0.7} -- returned color tables are copies
fog.density = 0.025
fog["end"] = 250 -- end is a Lua keyword
fog.start = 10

local fade = fadeToColor(color.black) -- requires an active Camera
fade:start(1) -- reaches fully visible black after one playing second
-- Call later, e.g. from a callback:
fade:stop(0.5) -- fades back to invisible
```

Fade controllers bind to the active camera at creation; they do not follow later
camera switches. Controllers for the same camera share its fade state. Creation
sets its color but does not start the fade. Start/stop reverse smoothly from current
opacity, accept 0..3600 seconds (default 1), and zero snaps immediately. Pause freezes
progress. Fades are transient, rendered after fog/effects, and are not serialized.
They do not prevent input or hide editor/debug overlays.

### Manual checks for this update

- Right-click Position/Rotation/Scale labels to copy/paste all three axes; test
  narrow and wide Inspector layouts, scalar-axis copy and invalid clipboard data.
- Copy/paste custom exported numbers, booleans, strings and asset paths.
- Save/reopen Fog in Edit mode; duplicate/copy a subtree containing Fog and verify
  all values survive. Try inactive Fog entities and multiple enabled components.
- Change each Fog value from Lua; reject NaN, out-of-range colors and end <= start.
- Start/stop a fade midway, pause/resume, use zero duration and switch cameras.
  A destroyed camera/controller must produce a safe error, not a crash.
- During Play modify/add/remove components, move/rotate/scale, parent, duplicate,
  delete, drag in models/prefabs, and sculpt terrain. Verify Save/Save As/Save Level,
  Ctrl+S and autosave cannot persist these edits; Stop restores the original scene
  and edit-mode undo history. Repeat through the separate Game-window close path.
