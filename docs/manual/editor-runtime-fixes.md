# Editor/runtime fixes — manual checks

- During Play, print from attached scripts, Clear the console, and confirm old
  entries do not return next frame while new messages still appear.
- Right-click each color widget: only one Copy/Reset/Paste group should appear.
  Copy RGB, paste into another RGB field; repeat with RGBA where supported.
- Scene component badges should be absent; transform handles, orientation cube
  and selected-camera lines should still work.
- Animator: existing toolbar buttons remain. Right-click background to add an
  animation state, link from Any State, unselect or reset the view. Drop a clip
  onto a state created this way. Empty left-click deselects and cancels linking;
  transition clicks still select edges. Confirm runtime disables graph edits.
- Attach navigation callbacks listed in navigation.md, move an agent to a valid
  destination, stop/restart it, and request an unreachable destination. Verify
  callbacks occur on status changes, not continuously, in Play and player.
- Create overlapping opaque mesh receivers on two different named layers. Select
  only one in a Decal's Receiver layers. The excluded foreground receiver must
  not receive the decal or reveal the projection on a surface behind it. Repeat
  with terrain/foliage, save/reload and prefab duplication. Existing decals with
  all layers selected must retain their appearance.
- Reproduce the reported Lua if/else failure with the user's affected script;
  no interpreter-condition fix is claimed without that reproduction.

## Linux startup before window focus

- On a native Wayland session (including a compositor configured not to focus
  newly launched applications), launch the editor normally without --smoke-test.
  Confirm the editor paints its first frame and becomes visible rather than
  remaining as a background process after logging "Editor started".
- Repeat with SDL_VIDEO_DRIVER=x11 under XWayland and with a native X11 session.
- After the first frame, switch focus to another application: the editor should
  still suspend simulation/rendering without busy-spinning. Refocus and verify
  normal rendering/input resumes. Close it while unfocused and verify exit.
- Repeat minimized startup and restore, plus startup while another window holds
  focus. The initial frame must not depend on receiving keyboard focus.

The startup focus gates now permit the first frame before blocking for focus;
Wayland may need the first submitted buffer to map/focus the window. This fixes
that ordering hazard; it does not establish the cause of every invisible-window
report without reproducing the affected desktop session.

## Built-in ImGui picker

- On Linux, launch with an invalid SDL_FILE_DIALOG_DRIVER value and without
  Zenity/portal services. Project Open/Save, Files / Models import, Folder import,
  archive extraction folder, and export icon/player/output selection must all
  open the ImGui picker, not an SDL native dialog or an unsupported-driver error.
- Repeat on Windows MinGW-w64. Navigate between drives and type absolute paths;
  on Linux test mount paths. Test relative directory input, Up and Refresh.
- Choose each file filter; directories must remain navigable. Include paths with
  spaces, Unicode, hidden files, and filenames containing ImGui's ##/### markers.
- Ctrl-select several files/folders for imports. Confirm every selected item is
  returned once; cancel and title-bar close must enqueue nothing and clear busy.
- Save without an extension, overwrite an existing project (confirm and decline),
  create a folder, enter invalid/missing paths and browse unreadable directories.
  Errors must remain inside the picker without terminating the editor.
- Close the originating tool while picking; the picker must remain usable. Open
  another picker request and verify only one modal is handled at a time. Refocus
  after switching applications: the picker should resume with its state intact.
- A pending picker without import jobs must not force periodic background wakeups.

These are manual instructions, not automated tests or claims of desktop execution.

## Picker icons and Play preload completion

- Open each file/folder picker: folders must show the editor's folder glyph;
  files must show their existing type-specific glyph (script, image, model,
  audio, generic file). Check the embedded icon font on Linux and Windows.
- Start asynchronous texture previews in the editor, then immediately press Play
  on a scene that does not use those textures. Loading must finish when the
  scene's dependencies are resident, without waiting for unrelated cache jobs.
- Repeat with required textures/models still decoding: Play must not start early.
  Test first Play, Stop/Play, an empty scene, separate Game, and player startup.
- Try a level whose required texture set exceeds the material cache capacity.
  Loading must report a budget error and return to editing instead of repeatedly
  evicting/reloading required textures until the loading timeout. Check that the
  original edit scene and selection survive. Retry with a smaller working set.
- Missing/corrupt required assets must still report their load error. Check
  window-close cancellation while loading and background focus restoration.

These checks require a desktop and have not been executed in the sandbox.

## OpenGL loader lifetime during Play

The ImGui OpenGL backend now compiles through `src/ui/imgui_opengl3.cpp`, using
GLAD's existing SDL-resolved dispatch instead of ImGui's private dynamic loader.
All editor/loading/scripted-GUI contexts share the engine GL loader lifetime;
shutting down a temporary UI backend must not close OpenGL libraries while the
renderer is still using their entry points. Keep this wrapper when upgrading
ImGui and do not also compile upstream imgui_impl_opengl3.cpp independently.

- On Linux Mesa/AMD (native Wayland and X11) and Windows MinGW-w64, press Play in
  an empty scene and an asset-heavy scene. Repeat Stop/Play, pause/resume, separate
  Game and embedded Game, with and without scripts defining onGUI.
- Exercise successful preload, invalid assets, loading cancellation, repeated
  runtime GUI destruction, and application exit. Verify the editor still renders
  after each temporary backend is torn down.
- Check the terminal's Play preload stages: scene preparation, loading overlay
  initialization, scene prepared, GPU assets ready/overlay closing, overlay closed
  and runtime started. If a native crash persists, capture the final stage plus
  `thread apply all bt` and `info sharedlibrary` from GDB while stopped at the
  original SIGSEGV (before the crash handler runs).

The user's supplied trace confirms a main-thread SIGSEGV at an unnamed address;
it does not prove this loader-lifetime hazard is the cause. Desktop confirmation
on the affected build/system is still required.
