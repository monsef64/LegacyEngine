# Manual verification: component headers, export settings and hover previews

No automated tests added or run. Verify on desktop Linux/OpenGL and Windows
MinGW-w64 with the editor's supported themes/font sizes and display scaling.

## Header layout

- Resize Inspector from wide to narrow. Test long titles (CharacterController,
  DirectionalLight, LegacyScripts), open/closed headers and different font scales.
  Checkbox stays inside the header, centered vertically; text must not overlap it.
- In a narrow panel, only the checkbox is drawn and hover says Enabled. Expand
  again and the label returns. Click the checkbox immediately after moving onto
  it: the enabled/active state changes but the header must not collapse.
- Click the title/arrow: collapse toggles normally. Check keyboard navigation,
  checkbox context reset/copy/paste, header context actions, drag reorder, undo,
  Camera active exclusivity and disabled editor contexts. Unsupported components
  must not gain enabled state.

## Saved export information

- Edit every Export Game field, including optional metadata, start scene, chunk
  size, icon/output/player chooser results. Close/reopen the window, save/reopen
  the project and restart the editor. Values should return without exporting.
- Save an untitled project, then Save As to another folder; export fields travel
  with it. Switch between two projects with distinct metadata; no values leak.
- Close the export window while a chooser is pending. Its result still persists.
  Switch projects while a chooser is pending: discard results for the old project.
- Verify successful export does not erase saved information. Failed exports retain
  fields. A completed job from an old project must not display its status/errors
  as the new project's export result.
- Read-only Settings directory / malformed or oversized Export.json should show
  an actionable error rather than crashing. Move across machines/OSes and check
  stale absolute paths are editable. Existing export folders remain protected.
- Inspect package contents: Settings/Export.json must not ship. Other runtime
  Settings files and ExportAssets dependency inclusion still work.

## Hover identity and throttled previews

- With a cold preview cache, move quickly between textures, models, scripts,
  prefabs, materials, packed subassets and audio in browser grid/list views.
- Each asset gets its own 1.5-second hover delay, even immediately after another
  tooltip was visible. Scroll away/back; change folders/search; close/reopen the
  browser; switch projects with identical asset names.
- Open Inspector and picker details while using browser tooltips. A throttled
  request must show no old image/text/waveform from a different asset or revision.
  Once work is ready, the correct preview appears and normal cache reuse works.
- Repeat while importing assets, while background thumbnail generation is busy,
  and after deleting/replacing an asset. Missing-asset errors belong to the
  requested file, never a previously previewed file.
