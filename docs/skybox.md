# Skybox component
Choose **Create → Skybox** (also in the Hierarchy context menu), or select a
GameObject and choose **Add Component → Skybox**. Its **Skybox type** is:

- **Cubemap asset**: assign a `.cubemap` file. Create it with **Assets context
  menu > Create > Cubemap**, then double-click it to open **Cubemap Editor**.
  Assign six matching square PNG/JPEG/BMP/TGA textures: Right (+X), Left (-X),
  Top (+Y), Bottom (-Y), Front (-Z), Back (+Z), then **Save Cubemap**.
  The Skybox Inspector has one asset slot, not six texture slots.
- **HDR panorama**: assign a Radiance `.hdr` equirectangular image, up to
  4096×2048. HDR uses Reinhard tone mapping and gamma correction in the renderer.
  HDR files also have Asset Browser thumbnails and enlarged hover previews.
  OpenEXR is not supported.

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

```legacyscript
function onStart()
    local sky = getComponent(Skybox) or addComponent(Skybox)
    sky:setCubemap("Sky/Evening.cubemap")
    -- Or switch to an HDR panorama:
    -- sky:setHDR("Sky/Studio.hdr")
    sky:setColor(1, 1, 1)
    sky:setEnabled(true)
end
```

`setCubemap(path)` selects Cubemap mode; `setHDR(path)` selects HDR mode.
`sky.cubemap` and `sky.hdr` expose their project-relative paths; when assigning
fields directly also choose `sky.type` (0 Cubemap, 1 HDR). Nil clears a slot via
its setter. Legacy scenes with inline faces and old `setFace`/`setTextures` calls
remain readable for compatibility, but new authoring uses cubemap assets.

A `.cubemap` is JSON with format `LegacyEngineCubemap`, version `1`, and a `faces`
object containing `right`, `left`, `top`, `bottom`, `front`, and `back` paths.
Paths cannot escape Assets. Faces must be equally sized squares up to 2048².
Use the Cubemap Editor rather than hand-editing filenames where possible.

## Smooth scripted crossfades

Create a second Skybox GameObject, configure its six textures or HDR panorama,
and disable it until needed. During Play:

```lua
function onStart()
    local destination = GameObject.find("Evening Sky")
    fadeSkybox(destination, 0.5)
    -- Equivalent when you have the active component:
    -- self:getComponent(Skybox):fadeTo(destination, 0.5)
end
```

`fadeSkybox(destination, speed)` uses the first enabled skybox as the source.
`sourceSky:fadeTo(destination, speed)` selects the source explicitly. Destination
may be a GameObject or Skybox component. Speed is normalized progress per second:
`0.5` takes two seconds, `2` takes half a second. Values must be finite and positive
(up to 1000). Smoothstep easing avoids abrupt start/stop; separate cached renderers
blend the source and destination images, including transitions between HDR and
six-face skies. This is a visual crossfade, not dynamic environment lighting.

At completion the source is disabled and the destination is enabled. Neither
component's textures, colors or identity is overwritten, so you can fade back.
Use `fadeSkybox(skybox1, skybox2, speed)` to specify both objects explicitly.
Rendering uses snapshots captured at fade start; avoid editing either sky during
a transition. Deleting the destination or removing its Skybox cancels the fade
and retains the source. Only one skybox fade may run in a world at a time.
The effect pauses with Play and is discarded on Stop. Destination assets are prepared before progress advances. Failed decoding cancels
the pending transition, keeps the source sky, and reports the error through
existing skybox diagnostics. Use valid, imported textures.
