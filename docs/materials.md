# Materials
Create a **Material (.mat)** from the Asset Browser's **Create** context menu,
then assign it using the **MeshRenderer → Material** dropdown or drag the asset
onto that slot. An empty slot also offers **Create Material** (seeded with the
mesh's current color/specular). Materials are owned by the project's `Assets`
folder, use numeric duplicate suffixes, and persist as readable JSON.

Expand **Material properties** in MeshRenderer to edit the shared asset:
- **Diffuse** (default): color, albedo texture, UV tiling and specular.
- **Bumped Diffuse**: the same controls plus a tangent-space **Normal map** and
  normal strength. Use OpenGL-style (+Y/green-up) normal maps; this is normal
  mapping, not grayscale height/displacement mapping.
- Texture dropdowns accept PNG, JPEG, BMP and TGA; you can also drag textures
  from the Asset Browser. `None` removes a texture. All built-in solids and the
  plane have UVs. Surfaces remain opaque; texture alpha is not transparency.

Inspector edits are saved immediately to the **shared material asset**, affecting
all users; asset-file edits are not part of scene undo. Script changes are
**per-MeshRenderer overrides**, so Play/Stop restores the scene without modifying
material files. Reset overrides to use shared values again. Old projects without
materials keep their inline colors, checkerboard and specular.

```lua
@export albedo Texture
@export normals Texture

function onStart()
    local mesh = self:getComponent(MeshRenderer)
    mesh:setMaterial("Stone.mat")
    mesh:setMaterialColor(0.8, 0.9, 1)
    mesh:setMaterialTexture(albedo)
    mesh:setMaterialShader("Bumped Diffuse")
    mesh:setMaterialNormalMap(normals)
    mesh:setMaterialNormalStrength(1)
    mesh:setMaterialTiling(2, 2)
    mesh:setMaterialSpecular(0.2)
    -- mesh:resetMaterialOverrides()
end
```

`Texture` is **export-only**: assign its asset in the script Inspector. It is a
project-relative asset reference, not a texture-loading constructor/API. Passing
`nil` to either texture setter clears that instance's texture. Material scripts
and `.ls` autocomplete expose all methods above. `mesh.material` also assigns a
`.mat` path and resets overrides. The older `color`/`specular` fields remain the
inline fallback when no material is assigned.

Rendering uses full-resolution textures with mipmaps and repeat wrapping (not
Asset Browser thumbnails); each image is limited to 32 MiB encoded and
4096×4096 pixels. The GPU texture cache is capped at 128 MiB/128 entries and
checks changed files once per second. Material assets refresh within 250 ms;
missing/invalid assets report errors and fall back safely. Large first-use
texture loads are synchronous. No transparency, custom GLSL, height maps, or
standalone texture-loading scripting API is included.

Asset Browser folders can be renamed with **F2** after selecting them, or with
**right-click → Rename…**, in either List or Grid view. Renaming a folder keeps
nested script, audio, exported asset and material references up to date, including
texture paths stored in `.mat` files. Existing destination names are rejected;
folder deletion remains unavailable.
