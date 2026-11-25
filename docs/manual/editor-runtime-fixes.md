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
