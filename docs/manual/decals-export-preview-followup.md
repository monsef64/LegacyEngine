# Manual verification: decals, export UX, mounted data and previews

No automated tests are added/run. Verify on Linux and Windows MinGW-w64 desktops.

- Add Decal, assign an alpha PNG, position/rotate its projection box across a wall,
  terrain and animated opaque mesh. Verify tint, opacity, width/height/depth,
  sorting overlap, camera-inside-box, partial viewports and disable/reactivate.
- Verify transparent surfaces do not receive decals; shallow depth avoids marking
  unrelated surfaces. Check camera effects and debug overlays still render correctly.
- Duplicate hierarchy, create/reload prefab, save/reopen project, undo, reset/remove,
  run scripting example and exported Decal reference; Stop must restore edit values.
- Export with Company/Description/Copyright empty; name/version/icon remain required.
- Test all three native Browse buttons and cancellation. Output Browse selects a
  parent and appends Game Name. Try spaces, another Windows drive, existing output,
  an Assets child and a valid Project/Builds/Game destination.
- Export and launch with chunk sizes 1, 16 and 64 MiB. Remove source project access
  before launch; verify shaders, skybox and built-in particle textures work. Tampered
  chunks must fail authentication. Old exported games retain legacy chunk defaults.
- Clear the default preview cache and import many models/textures/materials/prefabs.
  Verify focused idle frames fill both icon/hover caches, navigation/dragging stays
  responsive, no repeated GPU wait per save, and warm-up pauses during interaction.
- Switch projects and close editor while reads/writes are pending; no GL errors,
  stale images or partial cache files. Reopen and verify cached previews are reused.
- Windows: build with MinGW-w64 and verify game export uses the game's chosen icon.

Record CPU/GPU, driver, asset set and frame times; preview performance and desktop
rendering are not considered validated by compilation alone.
