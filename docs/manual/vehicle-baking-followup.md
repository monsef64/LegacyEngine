# Vehicle, baking and editor follow-up: manual checks

> Update: anti-roll strength has since been removed. References below to testing
> that control are historical; use `prefab-lighting-and-editor-followup.md` for
> current suspension/removal checks. Center-of-mass settings remain supported.

No automated tests are added or run for this change.

- Load an old vehicle containing keepUpright. Confirm the option is gone from the
  Inspector and scripting completion, and old saved values do not lock rotation.
  Park on slopes, corner, cross uneven terrain, jump, collide and land; tune
  antiRollStrength and confirm it only transfers loads between paired contacts.
  Try dynamic ground, different body masses/scales, Undo and Play/Stop rollback.
- Enable selected-camera occlusion visualization. Confirm the selected camera's
  frustum lines remain visible while offscreen/occluded geometry is filtered.
- Close Terrain tools, select terrains repeatedly, and confirm the panel stays
  closed until Open terrain tools is clicked. Check explicit terrain creation too.
- Bake a normal-mapped static receiver under a static light. Compare mapped detail
  before/after baking, with moving shadow casters and multiple overlapping lights.
  Save and reopen the project/level/prefab with PNG lightmap assignments; also load
  an older TGA bake. Verify Undo/Redo retains valid bake references.
- Set Diffuse Transparent Alpha to 0, 0.5 and 1, with/without an RGBA texture.
  Check texture-alpha multiplication, packed/shared material saving, scene/prefab
  instance serialization, setMaterialAlpha and Stop rollback. Old materials must
  default to opacity 1; opaque shaders must ignore the Alpha setting.
- Attempt to delete an Assets folder where Trash storage is unavailable. Verify
  Cancel preserves it and the separate permanent-delete confirmation removes only
  the named folder/contents. Normal successful Trash deletion must retain Undo.
  Switch projects with a warning open; deletion must be disabled for the new root.
- Reimport the failing main GoldSrc IDST v10 MDL with all texture/sequence-group
  companions beside it. Check single-frame poses, zero-FPS sequences, animations,
  multi-blend models, mesh/materials and resulting clip durations. If it still
  fails, capture the full new import error and retain the model for diagnosis.

Linux compilation does not verify Windows MinGW builds, interactive rendering,
driving feel, Trash behavior or the user's particular MDL file.
