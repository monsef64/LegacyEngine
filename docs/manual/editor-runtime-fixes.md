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
