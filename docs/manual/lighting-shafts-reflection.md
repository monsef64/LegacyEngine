# Lighting/editor follow-up: v24

## Hierarchy and component values

Select a GameObject and use **Create child...** above the hierarchy, or
right-click its row > **Create child**. The full create menu is available; the
new object is parented to the selected object with an identity local transform.
A collapsed parent opens to reveal it. With no object selected, **Create...**
creates a root object. Existing empty-space Create keeps its root behavior.
Child creation is disabled during Play/edit-blocking operations.

Expanded component panels now show **Copy values** and **Paste values** buttons.
These operate on one component, not a GameObject or its hierarchy. Paste only
accepts the same component type. The existing header right-click copy/paste and
Reset remain available. Runtime particle/laser state and baked-light membership
are not copied as authored values. Paste/reset/mode changes on a previously
baked light detach its combined atlas assignments to avoid stale-plus-realtime
light being added twice; atlas files remain available for undo and saved scenes.

## Light modes — editor only

Each light component has **Mode: Dynamic / Static** independent of Transform
Static. New lights default to Dynamic. Mode and internal bake linkage are **not
exposed to Lua**, by design; existing scripting of light color/intensity/etc.
continues to work.

- Dynamic: realtime direct lighting/shadows.
- Static, not baked: also realtime, until a successful bake is accepted.
- Static, baked: baked diffuse on receivers assigned to that light's accepted
  bake; realtime fallback on unbaked/missing/not-yet-streamed receivers. Specular
  remains realtime. A new or duplicated Static light is not silently treated as
  already baked. Copying values likewise clears bake membership.

A bake records the actual participating lights, not just a global Static flag.
Mode switches detach the combined atlas because a combined RGB atlas cannot
subtract a single light's old contribution losslessly. Other static edits still
need an explicit rebake; moving a baked static occluder leaves its old shadow
until rebake, as before. Older files migrate their Transform Static light flag
and retain legacy bake compatibility; rebake to adopt the new direct-only mode.

## Direct-light baking with soft shadows

New bakes contain **direct diffuse lighting only**. Indirect bounce controls are
removed; the bake entry point also forces indirect bounces to zero. Existing
v3/v4 TGA/GI bake files still load; new manifests are v5.

Set **Source radius** (metres) on point/spot lights, or **Sun angular softness**
(degrees) on directional lights. **Soft shadow samples** in Lighting controls
bounded deterministic disk sampling (4–128 samples). Radius zero uses one hard
shadow ray. Increasing the radius widens the penumbra; increasing samples reduces
banding/noise. This is sampled visibility around the existing analytic light,
not full physical area-light integration.

UVs remain automatically generated, isolated per-triangle charts. Atlases fit
within 2048px by adapting chart density. Existing 32M texel / 500k triangle caps
remain, with a 500M estimated shadow-ray budget. No fixed blur is applied across
chart boundaries, so unrelated surfaces cannot bleed together.

New atlases are **lossless PNG/DEFLATE**. The existing square-root-encoded 8-bit
RGB irradiance values are preserved exactly through compression: no reduced
resolution, palette conversion, JPEG loss or additional quantization. This saves
disk/download space; it does not reduce decoded GPU texture memory or add HDR
precision. Compression ratio depends on image content. Old TGA files are not
rewritten automatically.

To keep baked soft shadows soft, real-time static occluders are not reapplied to
new soft-direct receivers. Where the existing 12-layer shadow budget has spare
layers, a dynamic-only shadow pass overlays moving objects. Ordinary unbaked
receivers keep full realtime shadows. If spare layers run out, soft baked
receivers omit that light's dynamic overlay rather than harden the penumbra.
The overlapping-light shadow mask remains an approximation.

## Diffuse Reflection material shader

Choose **Diffuse Reflection** in a material asset or MeshRenderer's material
settings. It combines the ordinary lit diffuse surface with a Schlick/Fresnel
reflection of the active skybox (cubemap or HDR panorama). Existing Specular acts
as base reflectivity: at zero the base reflectance is 4%, rising toward a mirror
at one. Without a loaded sky texture there is a tinted sky-gradient fallback.
The current source sky is used during skybox transitions.

This is a cheap environment-reflection shader, **not** SSR, planar reflection,
local reflection probes or inter-object raytracing. Nearby objects do not appear
in the reflection. No extra reflection render is performed. Save/load, material
instance overrides and `mesh:setMaterialShader("Diffuse Reflection")` use the
existing material path. Other shader IDs remain unchanged.

## LightShafts

Use **Create > LightShafts** or **Add Component > LightShafts**. Controls include:
Enabled, local Direction (combined with Transform rotation), Height, Start/End
width, Color, Intensity, Density, Edge falloff, Streaks, Start fade and Camera
near fade. Dimensions are world metres and do not multiply by Transform scale.

This is an intentionally fast, camera-facing tapered ribbon: six vertices and
one draw per visible shaft, frustum culling, at most 128 visible shafts per view,
additive blending and depth testing without depth writes. It does not raymarch,
cast light, sample light shadow maps, or automatically react to smoke/occlusion
along its volume. Opaque geometry can occlude the ribbon's visible fragments;
intersections can be hard. Long/wide overlapping shafts can still cost fill rate.

**Bake profile to PNG** writes a unique 256px grayscale profile under project
`LightShafts/` and enables **Use baked profile texture**. Cached shading replaces
per-fragment power/sine/fade calculations with one texture sample. This is a
profile bake only, **not a bake of scene lighting or world occlusion**. Direction,
height, widths, tint, intensity, density and camera near-fade remain adjustable.
Rebake after changing edge/streak/start-fade controls, or disable Use baked
profile texture to return to live procedural shading. Custom project image
profiles may also be selected. Missing/pending images use procedural fallback.
Texture assets remain after undo so other saved objects do not lose references.

LightShafts supports save/load, prefabs, subtree duplication, component
copy/paste/Reset, undo, asset remapping, script exports and Lua properties:

```lua
local shaft = self:addComponent(LightShafts)
shaft.direction = {0, -1, 0}
shaft.height = 12
shaft.endWidth = 4
shaft.density = 0.25
shaft.color = {1, 0.9, 0.7}
-- Assign whole vector tables; reads return copies.
```

## Manual validation (no automated tests added or run)

1. Create children from toolbar/context menus, including under rotated/scaled and
   collapsed parents, root creation, prefab editing and Play-disabled creation.
   Undo/redo child creation and verify duplication still copies full hierarchies.
2. Copy/paste/reset individual component values on different objects; check type
   mismatch disabling, undo, and absence of copied transient particle/laser data.
3. Set Dynamic/Static light modes; verify unbaked Static fallback, baked diffuse
   without doubling, streaming fallback and unbaked objects. Add/duplicate lights
   after a bake; switch modes and undo; clear assignments and save/reload. Verify
   Lua cannot read/write Mode or bake membership.
4. Bake hard/soft sun, spot and point shadows at multiple source sizes/sample
   counts, including cookies and concave/imported geometry. New output should
   contain direct PNG atlases and no new indirect atlas. Compare moving dynamic
   caster overlays with and without spare shadow layers and CSM enabled.
5. Inspect the PNGs with an independent viewer: dimensions, RGB orientation,
   shadow gradients and file sizes. Compare old TGA loading. Exercise cancellation,
   source/project changes during a bake, ray-budget errors and save/reload.
6. Check Diffuse Reflection against asymmetric cubemap/HDR skies, rotated objects,
   specular 0/1, material copy/overrides, missing skies, sky transitions and Lua.
7. Check LightShafts from multiple camera angles, viewport/docking setups and
   near/far distances. Move/rotate/resize, duplicate, hide and disable shafts.
   Compare procedural and baked profiles, clear/move/rename/remove profile assets,
   switch projects, exercise Undo/Reset/exported references and Lua range errors.
8. Check Windows MinGW-w64 compilation, actual shader execution/visual quality,
   GL state restoration with transparency/particles/lasers, and frame times.
   Linux headless compile/link validation is not visual or performance validation.
