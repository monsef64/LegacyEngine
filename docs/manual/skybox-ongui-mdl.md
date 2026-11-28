# Skybox and MDL follow-up: manual verification

No automated tests were added or run.

- Load six-face and HDR skyboxes. Move/rotate cameras, resize Game, open the
  separate Game window and compare sky orientation with the previous build.
  Verify cubemap wrapping, reflections and sky ambient colors. Inspect frame
  time and loading responsiveness; no benchmark result is claimed by this change.
- Switch HDR assets rapidly, switch projects during decoding, edit cubemap face
  files/descriptors externally and repair a missing/invalid image. Only the current
  request should upload; unchanged resources should not repeatedly parse/decode.
- Toggle Compress HDR (RGB9_E5). Compare color/bright highlights against RGB16F
  and check driver-reported texture memory where available. The source HDR must
  remain unchanged. Check Undo/Redo, prefabs, save/reopen and Stop rollback, plus
  old JSON/text projects. New format versions are JSON 27 / legacy text 36.
- Crossfade between HDR/cubemap skies while the target is still loading. Fade time
  must wait for both textures, and Stop/project switches must discard stale work.
- Check Classic Light / Classic Dark theme labels; stored numeric preferences
  should still select the same palettes.
- Import the failing GoldSrc model pack. Test its main model, selecting its T.mdl
  texture companion, a numbered sequence group, and directory import. Companion
  files should not produce standalone conversion failures. Test mismatched case
  in companion filenames and a genuinely missing companion. Verify meshes,
  textures, bones and animations after conversion/save/reopen. The specific
  user-reported file remains unverified without its error and source files.
- Repeat on Windows MinGW-w64/OpenGL. Linux compilation does not validate driver
  behavior, Windows filename handling or the user's particular model pack.
