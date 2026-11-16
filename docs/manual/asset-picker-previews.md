# Asset picker previews

All existing `assetPicker` consumers share a preview-enabled selection window:
Inspector components, script-export asset fields, material/cubemap editors,
Animation's Open Clip and component clip fields, and Terrain tools.

- Switch Grid on/off and adjust Preview size. Only matching asset types appear;
  search still matches the full project-relative path. None clears the assignment.
- Tiles/rows use the asset browser's icons and cached image/3D thumbnails.
- Hover or keyboard-focus a candidate for a larger right-hand preview. At narrow
  widths the detailed preview appears in the hover tooltip instead.
- Images/HDR show image previews; materials, imported meshes, prefabs, cubemaps
  and terrain have rendered previews. Scripts/text show text excerpts, audio
  shows duration/waveform, and animation clips show duration/track/key metadata
  (not animated playback). Other formats retain a generic icon and explanatory
  fallback text. Malformed/missing assets show a preview error.
- Click a candidate to assign immediately, matching previous picker behavior.
  Close the picker without choosing to leave the current assignment unchanged.

Thumbnails are generated only for visible rows with existing bounded upload and
3D-icon budgets. Picker details have an independent render slot so Inspector and
asset-browser tooltip images cannot overwrite the same render target. Preview
scenes do not modify the selected level or write asset assignments. Choosing an
asset continues to use each caller's existing validation and undo behavior.

## Manual verification

No automated tests were added/run. Linux compilation cannot validate GUI/GL output.

1. Open texture, normal-map, model, material, audio, script, cookie, animation,
   skybox/cubemap and terrain asset fields from the relevant component/window.
2. Compare thumbnails/details with the asset browser. Check rendered images are
   upright and audio/text/animation metadata are readable.
3. Switch list/grid, resize to narrow/wide, scroll a large list, change thumbnail
   size, filter by subfolder and inspect Unicode filenames. Verify no unsupported
   types leak through any field's filter.
4. Choose/clear assets, close without choosing, drag assets into the original
   fields, then undo/redo assignments. Existing field interactions must remain.
5. Hover one candidate while an Inspector/asset-browser preview is visible:
   previews must stay independent. Switch between pickers and project roots;
   verify stale candidates and previews do not carry across projects.
6. Try corrupt/deleted assets and an asset type without visual support; errors
   must not prevent selection or crash the editor. Check Windows/MinGW and GL
   memory/performance manually with a large asset library.
