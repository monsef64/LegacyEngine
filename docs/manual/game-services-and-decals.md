# Manual verification: game services and decals

No automated tests were added or run for this change. Run this checklist on a desktop
OpenGL build; repeat player-specific items on Windows MinGW-w64 and Linux.

## Decals and assets

- Create an opaque cube/plane and a Decal. Select a texture from the Inspector
  picker, including an embedded texture from an imported packed model. Confirm
  the projector outline surrounds the receiving surface; inspect both editor and
  Game views. Rotate/resize the projector, overlap decals, change opacity and
  camera viewport. Confirm the original surface remains visible outside the box.
- Compare projection with CameraEffects enabled/disabled, multiple cameras and
  different render sizes; check log for shader/texture errors. Test AMD/Intel/NVIDIA
  if available. Compilation alone does not verify this rendering fix.
- Undo texture/size edits, save/reload the project, and Stop Play after runtime
  property edits. The original scene must be restored.
- Right-click an embedded model texture and copy it into a chosen Assets folder.
  Verify the output decodes, has a unique filename on repeated copies, and can be
  used independently after removing the model. Cancel the picker and try a folder
  outside Assets (including a symlink pointing outside); no file should be written.

## Screen and Game

- Log every read-only Game property in editor and exported player. Check actual
  pixel dimensions on a high-DPI display and resize docked/separate game windows.
- Register OnResolutionChanged, OnFocus, OnFocusLost and OnUrlOpened both as global
  callbacks and with on(). Verify event values and focus loss while minimized.
- Enumerate resolutions. Invalid dimensions must not resize; valid dimensions
  resize a separate Game window/player. Test fullscreen and return to windowed.
  Docked calls must not resize/fullscreen the editor.
- Compare brightness 1, 0.5 and 0, then restore 1. Only the game image changes.
- Capture Screenshots/view.png and inspect orientation, dimensions, colors, UI
  inclusion and brightness. Reject absolute paths, traversal and non-PNG names.
- Try caps -1, 30 and 60 with VSync on/off. Invalid caps/properties must error.
- Open an HTTPS URL; reject file/javascript protocols and control/NUL characters.

## Persistence and sandbox

- Save and reload all six types across Stop/Play and player restarts, including
  nested/sparse tables, integer keys, false, zero and empty strings. Missing keys
  and mismatched saved types return defaults. Existing saves update repeatedly.
- Reject cyclic tables, nonfinite numbers, functions, oversized data, excessive
  nesting and malformed save files without silently replacing the previous save.
- Open/write/flush/seek/read/close files. Check all allowed modes, EOF, line
  iteration from a path and a caller-owned handle, early iterator abandonment,
  append after seek, rename collisions, remove and directory rejection.
- Reject ../, absolute/drive paths, virtual # members, and symlink escapes on every
  File operation and screenshot destination. Exercise 64-handle and 16-MiB limits.

## Graceful quit

- In OnGamePreQuit start task(function() wait(0.2); Saving.saveInt("quit",1) end).
  Game.quit and player window-close must allow the task to finish. Repeat while
  unfocused/minimized. Editor Game.quit must Stop/restore, not close the editor.
- Register the same task through on("OnGamePreQuit", ...); confirm task ownership
  and completion. Keep Game.blockGameKilling true or start a very long task and
  verify shutdown proceeds after ten seconds. Repeat requests must not restart
  the deadline or redeliver the pre-quit event.
- Editor Stop remains immediate. Forced OS termination is outside this contract.
