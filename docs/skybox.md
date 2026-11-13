# Skybox component
Select a GameObject and choose **Add Component → Skybox**. Its **Skybox type** is:

- **Multi Texture Skybox**: assign six PNG/JPEG/BMP/TGA faces using dropdowns or
  Asset Browser drag/drop: **Right (+X), Left (-X), Top (+Y), Bottom (-Y),
  Front (-Z), Back (+Z)**. Images must be equally sized squares, up to 2048².
  Faces use the standard OpenGL cubemap layout (no automatic vertical flip).
- **Skybox HDR**: assign a Radiance **`.hdr` equirectangular panorama**, up to
  4096×2048. OpenEXR is not supported. HDR uses Reinhard tone mapping followed
  by gamma correction for display; it is not clamped to an LDR input texture.

**Skybox color** multiplies the texture (white preserves its color). Disable the
component to turn it off. The first enabled Skybox in component iteration order supplies the
background in Scene and Game views; disable it to select another. With no enabled
Skybox, the existing clear background remains. Missing or invalid images produce
an error and a solid tint fallback. Each encoded image is limited to 64 MiB;
only the active sky's texture is retained on the GPU. Files refresh within one
second. Initial image loading is synchronous.

The background follows camera orientation, not position, and stays behind scene
geometry. The owning object's Transform does not rotate or move it. Skyboxes are
backgrounds only: they do not provide image-based lighting, reflections or ambient
light. Components persist in projects and participate in duplication, undo/redo,
Play/Stop restoration and Asset Browser path renames.

```lua
@export panorama Texture
@export right Texture
@export left Texture
@export top Texture
@export bottom Texture
@export front Texture
@export back Texture

function onStart()
    local sky = self:getComponent(Skybox)
    if sky == nil then sky = self:addComponent(Skybox) end
    sky:setEnabled(true)
    sky:setColor(1, 1, 1)
    sky:setType("HDR")
    sky:setHDR(panorama)
    -- Or use six face textures:
    -- sky:setType("Textures")
    -- sky:setTextures(right, left, top, bottom, front, back)
    -- sky:setFace("front", front)
end
```

`Skybox` can itself be exported as a component reference. Direct fields are
`type` (0 = textures, 1 = HDR), `color`, `enabled`, `hdr`, `right`, `left`, `top`,
`bottom`, `front` and `back`. Texture fields are project-relative paths; `Texture`
exports now also accept `.hdr` for skyboxes, still without a texture-loading API.
Use `setHDR(nil)` or `setFace(name, nil)` to clear a slot. The scripting methods
and properties are included in `.ls` editor/VS Code completion.
