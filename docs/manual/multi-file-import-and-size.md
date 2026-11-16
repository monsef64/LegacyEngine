# Manual checks: queued imports and compact releases

No automated tests were added or run. Linux engine compilation and executable
size measurements do not establish Windows GUI/runtime compatibility.

## Multi-file import

- Select several files in the desktop file manager and drop them on the engine.
  Confirm every path imports, one worker at a time, rather than reporting that an
  import is already running. More drops while importing append to the queue.
- Try a mixture of images, audio, scripts, models and directories. Verify normal
  model conversion, texture dependencies, folder structure and existing filename
  collision handling still apply. Include paths with spaces and Unicode.
- Use Import > Files / Models and multi-select files in the native dialog. The
  project Open/Save and folder pickers remain single-selection dialogs.
- Include an invalid model between valid files: the remaining files must still
  import, and the failed filenames/errors should remain visible at completion.
- Confirm the Assets panel shows the active source and number of waiting files.
  Edits and project saving remain blocked until the import queue drains.
- Change projects while work is pending: discard queued requests for the old
  root. An already-running import may finish in its captured old destination;
  it must never write into the new project's root or alter its selection.
- Check resulting files after the last import, then save/reopen the project.
  Cancel a multi-file picker and confirm no work is enqueued.

The queue captures the destination folder and asset root when each path is
received, not when its background job starts. Closing the engine waits for its
active async worker as before; pending in-memory requests are not persisted.

## Size / feature preservation

- Build Release with `LEGACY_SMALL_RELEASE=ON`, `BUILD_TESTING=OFF` on MinGW-w64.
  Package using the workflow commands and compare executable/install/archive
  sizes separately against a build with `LEGACY_SMALL_RELEASE=OFF`.
- Launch the packaged copy (not just the build copy). Check models/animated glTF,
  Lua/LegacyScript, physics, audio, particles, terrain, light baking, shadows,
  editor icons/fonts, native dialogs and save/load of old projects and meshes.
- Verify shaders, particle assets, runtime DLLs, docs and license files remain
  bundled. Debug build symbols must remain available; packaging must not modify
  the build-directory executable.
