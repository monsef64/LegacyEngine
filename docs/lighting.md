# Dynamic lighting and shadows
Use **Create > Light > Directional Light / Point Light / Spot Light**, or add a
light component in the Inspector. All three update from their owning Transform
and component settings every frame, in both Scene and Game views. Directional and
spot lights emit along local **-Z**; rotate the object to aim them. The starter
scene includes a directional light. Older projects without lights receive only
ambient illumination until a light is added.

Inspector controls include enabled state, color, intensity, shadow casting, quality and
bias, plus range for local lights and inner/outer half-cone angles for spots.
Directional **Shadow coverage** is the world-space half extent around each view;
objects outside that region still receive light but are not shadowed by that map.
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
light:setShadowQuality(2) -- 0 Off, 1 Low (256), 2 Medium (512), 3 High (1024)
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
- A persistent **12-layer, 1024×1024 depth texture array** (approximately 48 MiB).
  Directional/spot shadows cost one layer; point shadows cost six faces. Shadow
  budgets follow the same priority order; excess lights still illuminate without
  shadows. Disable shadows on fill lights to reserve budget for key lights.
- Shadow-caster sphere/frustum culling, cached mesh bounds/uniform locations, and
  model matrices prepared once per view. Wire geometry does not cast shadows.
- Per-light quality: Off skips shadow passes. Low uses 256² with one bilinear
  comparison lookup, Medium uses 512² with a weighted 3×3 tent (four bilinear
  lookups), and High uses 1024² with a weighted 5×5 tent (nine bilinear lookups).
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
casters update immediately. This is not clustered rendering, cascaded directional
shadows, baked lighting, global illumination or unlimited lights. Two visible
Scene/Game views incur separate shadow passes. Point shadows are more expensive;
profile your target scenes/hardware and tune range, coverage and shadow toggles.
Tent PCF smooths jagged edges; it does not recover geometry smaller than a shadow
texel or simulate contact-hardening area-light shadows. Large directional coverage
can still lose detail, and point-shadow face boundaries are clamped rather than
filtered across faces. Use tighter coverage or High quality where detail matters.
CI checks all three light types with real OpenGL pixel comparisons for shadow
toggles, moving casters and moving lights; no hardware FPS claim is implied.
