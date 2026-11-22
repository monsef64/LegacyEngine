# Model imports, previews, loading and chassis COM: manual verification

> Update: anti-roll strength has since been removed. References below to testing
> that control are historical; use `prefab-lighting-and-editor-followup.md` for
> current suspension/removal checks. Center-of-mass settings remain supported.

No automated tests were added or run. Performance changes have not been benchmarked.

## Imports and compatibility

- Import a single untextured static mesh: expect `.mesh`. Import a textured,
  animated, skinned or multi-part source: expect `.model`. No generated sibling
  folders/materials/textures should remain. Source texture files are not modified.
- Expand both container types, assign embedded mesh/material/animation/image
  members, drag them to matching pickers and instantiate the owner. Save/reopen
  both native and JSON scenes. Exercise colliders, terrain trees, prefabs,
  animation playback, source-copy, duplicate, rename, deletion and undo.
- Open older `.mesh`/`.lemesh` assets and scenes without reimporting. Existing
  references must not be renamed. New `.model` assets require this engine version.
- Import clips with meaningful names, duplicate names, Unicode and reserved path
  characters. Display labels must preserve original names; only virtual filenames
  sanitize reserved characters and disambiguate duplicates. Unnamed clips use
  `Animation`. Verify duration, clip selection and cross-fades still work.

## Previews, cache and responsiveness

- Scroll a large texture/model/material folder rapidly. Icons should fill lazily;
  texture and material CPU work should not block the editor. Scroll away during
  decode and back: other assets must not become permanently blocked by an
  offscreen pending request. Switch project/root during pending requests.
- Model icons should be square 128×128 images; hover a model for a larger 384×288
  image. First-time bounds decoding should stream without synchronously creating
  GPU geometry during selection, and final framing should update after loading.
- Restart the editor: completed v2 `.mesh`/`.model` previews should reuse files in
  project `.cache/thumbnails`. Check both owner and embedded mesh members.
  Modify/reimport/scale the source container: metadata must invalidate its previews.
- Delete/truncate cache entries and repeat. A read-only project must still render
  previews. Clearing the cache should regenerate it. Legacy externally linked
  models, prefabs and terrain intentionally do not persist rendered previews yet.
- Keep a MeshRenderer with an embedded material selected for several seconds.
  Confirm its UI no longer reparses the entire model every frame. Edit/save the
  material, change its texture, use undo, and verify Inspector/rendered output.
- Compare cold/warm selection and import times manually. SIMD vertex validation
  must reject NaN/infinite/out-of-range coordinates just like scalar fallback;
  valid geometry/UVs/normals must remain unchanged. Check SIMD picking on thin,
  concave, reversed-winding, nearly parallel and degenerate triangles: holes must
  stay unselectable and the nearest visible part must win. No AVX or Vulkan required.
- Confirm Game rendering is unaffected by preview framebuffer/texture state and
  that cache/preview work remains paused when the engine is unfocused, except for
  the separately supported asset-import servicing.

## Vehicle center of mass and anti-roll

- Existing vehicles must default all three COM offsets to zero. Lower
  `centerOfMassY` and drive/turn on a slope: chassis collision geometry should stay
  in place while tipping behavior changes. No automatic rotation lock is added.
- Check X/Z offsets, scaled/rotated chassis, live offset editing, adding/removing
  Vehicle, and Stop restoring editor values. Test multi-edit, copy/duplicate,
  undo/redo, native/JSON save/reopen and Lua setting the three offsets.
- Compare antiRollStrength zero/nonzero with both wheels grounded, one airborne,
  jumps and flips. The existing axle support behavior remains separate from COM.
- Repeat build/desktop/vehicle checks on Windows MinGW-w64 and Linux OpenGL.
  These interactive checks have not been performed in the sandbox.
