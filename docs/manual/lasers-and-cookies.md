# Laser beams and spotlight cookies

## Laser component

Choose **Create > Laser** (also in Scene/hierarchy create context menus), or add
**Laser** to an existing GameObject. Transform is added automatically. Emission
follows local **-Z**, independent of Transform scale; dimensions are in metres.

The engine queries the first active physics collider along the beam every frame,
excluding the laser's own collider. Misses extend to Range. Scene and Game views
share the same query result; rendering another camera does not advance it.

Inspector settings:
- Enabled, RGB beam color and Intensity.
- Range, Core width, Glow width (at least Core width), Impact size.

The depth-tested additive ribbon has a bright core and softer outer glow; an
impact adds a surface-aligned radial glow and white-hot centre, offset slightly
to avoid z-fighting. It disappears on misses. The effect works without external
textures. Normal scene geometry occludes it; it does not write depth or change
ordinary material blending. Settings participate in save/load, prefabs,
subtree duplication, component copy/paste/Reset, Inspector reordering and undo.
Hit state is transient and never serialized.

### Lua

```lua
local laser = self:addComponent(Laser) -- or self:getComponent(Laser)
laser.color = {1, 0.05, 0.02}
laser.intensity = 4
laser.range = 30
-- Increase the outer width before increasing the core beyond its current bound.
laser.glowWidth = 0.2
laser.width = 0.02
laser.impactSize = 0.3
laser.enabled = true
-- Read-only snapshots from the latest query (scripts run before the next query).
local didHit = laser.hit
local endpoint = laser.endPosition
local normal = laser.hitNormal -- meaningful only on a hit
```

Laser supports the existing component type tokens/string lookup, hierarchy
lookup, removal and exported component references. RGB reads return copies:
assign a whole table instead of mutating `laser.color.x`.

This is a visual, continuous-beam effect, **not** full volumetric scattering or
an automatically illuminating light. The impact is additive surface glow, not
a spawned PointLight. It does not apply damage, impulses, reflection/refraction,
particle sparks or decals. Raycasts inherit existing collider limitations,
including rays starting inside a collider. A laser with no collider on a visible
mesh will pass through it; add the appropriate collider. The emitter's own
collider is excluded, not all child colliders. Layered transparent effects are
not globally depth-sorted.

## Spotlight cookies

On a SpotLight select an image in **Cookie**, or drag an image from Assets onto
that field. **Clear cookie** restores an ordinary cone. Supported formats:
PNG, JPG/JPEG, TGA and BMP (up to 4096 x 4096 source pixels).

White transmits the light; black blocks it; RGB tints it and alpha multiplies the
transmission. Projection fills a square at the outer half-cone angle, clipped by
the existing circular cone/range/falloff. Images are not tiled. Local X points
right and local Y up; rolling the light rolls the cookie, including when aimed
straight up/down. Non-square images stretch into the square projection.
Shadows and cookies are independent: disable Cast dynamic shadows and the
cookie still projects. Cookies modulate both diffuse and specular light.

```lua
local spot = self:getComponent(SpotLight)
spot.cookie = "Textures/window-cookie.png" -- project-relative, validated
-- spot.cookie = "" -- remove projection
```

Cookies use the asynchronous material-image cache. A missing/pending/corrupt
image falls back to a plain spotlight, with load failures in the existing
material error reporting. Visible cookies are linearly resampled to a shared
**512 x 512, 16-layer** atlas, using just one additional OpenGL texture unit.
The existing 16 visible-light limit still applies. Texture edits reload through
the cache; asset rename/move remaps cookie references, including saved JSON.
Switching projects cannot reuse the previous project's image cache.

Static cookies participate in direct-light soft-shadow baking. Historical
multi-bounce GI atlases are still readable, but new bakes no longer generate GI.
Each unique image is decoded once per bake and sampled at the same 512px
resolution/orientation. A missing/invalid image fails the bake rather than
silently baking the wrong pattern. Cookie edits, like other static-light edits,
require an explicit rebake; existing baked lighting remains a snapshot.

Settings are saved in text v24 / JSON v18. Older supported projects without
cookies or lasers continue loading with an empty cookie/no laser. Current files
are not expected to load in older engine binaries.

## Manual verification (no automated tests)

1. Create a Laser aimed at a Box collider. Move/rotate emitter and target; check
   nearest-hit clipping, range misses, and the surface glow on floors/walls and
   sloped surfaces. Move an opaque object between camera and beam: depth testing
   should hide the occluded portion. Check near-parallel viewing and wide/narrow
   cores in perspective and orthographic cameras.
2. Repeat with sphere/capsule/concave mesh and moving Rigidbody colliders, inactive
   targets, multiple colliders in line, and a self collider. Check Scene, docked
   Game, detached Game, multiple cameras, Play pause and stop.
3. Save/reload laser levels and prefabs; duplicate parent/child hierarchies;
   copy/paste/Reset/reorder/remove components; undo/redo. Exercise Lua settings,
   destroyed handles, invalid values, read-only hit data and exported references.
4. Give a spotlight an asymmetric RGB/alpha image (letters/arrows are useful).
   Verify orientation, cone edges, roll and vertical aim, intensity, specular,
   range, and shadows off/on. Try multiple lights/images and exceed the existing
   16-light visibility limit; no cookie should leak onto a different light.
5. Remove/replace/rename the image, clear/reassign Cookie, and switch between
   projects with identical relative filenames. Verify async fallback/reload,
   no previous-project leakage, and no stale pattern after clearing.
6. Save/reload older scenes without cookies; save new scenes with cookies, use
   component Reset/copy/paste and undo/redo; check project dirty tracking.
7. Bake an asymmetric cookie onto a static floor/wall with soft shadows enabled;
   compare orientation/tint to realtime, including alpha and rolled lights.
   Move the light or edit its cookie: old baked pattern must remain until rebake.
   Test bake cancellation and missing-image errors.
8. Validate shaders, image appearance, GL state restoration with particles/
   transparency, performance, and MinGW-w64 on Windows manually. Headless Linux
   compile/link validation alone does not establish visual correctness or FPS.
