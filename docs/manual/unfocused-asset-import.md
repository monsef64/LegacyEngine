# Asset import while unfocused: manual verification

No automated tests were added or run for this fix.

- Open a project and select an Assets destination folder. Focus the OS file
  manager so neither engine window is focused, then drop files and folders onto
  the editor. Confirm imports are accepted without first refocusing the engine.
- Queue several imports, including a model and a directory with nested files;
  leave the editor unfocused until all jobs finish. On refocus, confirm the whole
  queue completed, destination folders stayed correct, and previews refresh.
- Open Files / Models or Folder import, choose assets, then immediately switch to
  another application. Imports must start/finish while the editor stays suspended.
  The ImGui picker itself pauses when unfocused; refocus to select or cancel.
  An idle picker must not cause background polling or rendering.
- Repeat with an invalid/missing source and inspect the error after refocusing.
  Refocus during a running import and queue more assets; no drop should be replayed
  or duplicated. Drop payloads must be owned before the next SDL event pump.
- Verify Play-mode drops are still rejected and project/level transition guards
  remain in place. Project changes must not redirect old queued import requests.
- During unpaused Play, unfocus both engine windows: simulation/scripts/audio and
  rendering should remain suspended, with no catch-up step on focus recovery.
  A focused separate Game window should still run normally.
- Leave the engine unfocused with no pending imports. CPU usage should
  remain idle (blocking wait). Pending import servicing must not render ImGui,
  upload/delete thumbnail textures or run gameplay/autosave work.
- Repeat OS drag/drop and ImGui pickers on Windows MinGW-w64 and Linux. These
  desktop interaction checks have not been performed in the sandbox.
