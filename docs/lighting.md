# Dynamic lighting and shadows
Use **Create > Light > Directional Light / Point Light / Spot Light**, or add a
light component in the Inspector. All three update from their owning Transform
and component settings every frame, in both Scene and Game views. Directional and
spot lights emit along local **-Z**; rotate the object to aim them. The starter
scene includes a directional light. Older projects without lights receive only
ambient illumination until a light is added.

Inspector controls include enabled state, color, intensity, shadow casting, quality and
bias, plus range for local lights and inner/outer half-cone angles for spots.
**Project Settings > Rendering** defaults to **Cascaded (CSM)** directional shadows.
Four camera-frustum splits concentrate detail near the camera, with texel snapping
and smooth transitions over the final 10% of each split. Perspective and orthographic
views are supported. Directional **Shadow coverage** controls half the maximum CSM
view-depth range. In **Single-map PCF (PCM)** compatibility mode it instead controls
the world-space half extent around the view. Point and spot lights always use PCF.
Objects beyond shadow coverage still receive illumination.

Choose **Shadow resolution** (256, 512, 1024 or 2048) in the same project settings.
Settings persist in `Settings/Rendering.json` and support undo/redo; older projects
use CSM at 1024 by default.
Lights survive save/load, duplication, undo/redo and Play/Stop restoration.

```lua
local lamp = GameObject.new("Moving spotlight")
lamp:setPosition(0, 6, 3)
lamp:setRotation(-65, 0, 0)
local light = lamp:addComponent(SpotLight)
light.intensity = 4
light.range = 25
light.innerAngle = 20
light.outerAngle = 35
light:setShadowQuality(2) -- 0 Off, 1 Low (quarter), 2 Medium (half), 3 High (full)
light:setColor(1, 0.8, 0.6)
light:setIntensity(4)
light:setRange(25)
light:setSpotAngles(20, 35)
light:setEnabled(true)
light:setShadows(true)
-- Move/rotate lamp or change light properties from onUpdate for dynamic lighting.
```

The renderer uses a bounded OpenGL 3.3 forward-lighting path rather than introducing
a second rendering framework. Its cost controls are:

- Up to **16 active lights per view**, directional lights first, then local lights
  prioritized by distance relative to range; off-frustum local lights are culled.
- A persistent **12-layer depth texture array** (approximately 48 MiB at 1024²,
  scaling quadratically with project resolution). CSM directional shadows cost
  four layers, single-map directional/spot shadows one, and point shadows six faces. Shadow
  budgets follow the same priority order; excess lights still illuminate without
  shadows. Disable shadows on fill lights to reserve budget for key lights.
- Shadow-caster sphere/frustum culling, cached mesh bounds/uniform locations, and
  model matrices prepared once per view. Wire geometry does not cast shadows.
- Per-light quality: Off skips shadow passes. Low uses quarter resolution (minimum 64²) with one bilinear
  comparison lookup, Medium uses half resolution with a weighted 3×3 tent (four bilinear
  lookups), and High uses full project resolution with a weighted 5×5 tent (nine bilinear lookups).
  Each hardware lookup interpolates four depth-comparison results, so edges
  transition continuously instead of jumping between nearest-depth texels.
  Lower qualities use a smaller region of their fixed atlas layer: they reduce raster/filtering work but do not
  shrink the shared texture allocation.
- Receiver-plane depth correction adjusts comparison depth across the filter
  footprint on sloped surfaces; degenerate gradients fall back safely. Adjustable
  slope-aware bias remains available. World-space derivatives are evaluated before
  divergent light branches, then projected analytically for each light.
- Directional coverage is texel-snapped to reduce shimmering.

Shadow maps are refreshed for each rendered view, so moving lights **and** moving
casters update immediately. This is not clustered rendering, baked lighting, global illumination or unlimited lights. Two visible
Scene/Game views incur separate shadow passes. Point shadows are more expensive;
profile your target scenes/hardware and tune range, coverage and shadow toggles.
Tent PCF smooths jagged edges; it does not recover geometry smaller than a shadow
texel or simulate contact-hardening area-light shadows. Large directional coverage
can still lose detail, and point-shadow face boundaries are clamped rather than
filtered across faces. Use tighter coverage or High quality where detail matters.
CI checks all three light types with real OpenGL pixel comparisons for shadow
toggles, moving casters and moving lights; no hardware FPS claim is implied.
