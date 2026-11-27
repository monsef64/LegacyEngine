# Manual verification: global fields and orthographic transform handles

No automated tests added or run. Verify Linux/OpenGL desktop and Windows MinGW-w64.

## Global Inspector declarations

- Attach a script with global float speed = 5.0, global int health = 100,
  global bool ready = true, and global string title = "Player". Check field values,
  integer Lua values, Inspector edits, save/reload, duplicate, undo and Stop rollback.
- Assign global Prefab, AnimationClip, Texture, AudioClip, GameObject and Recoil
  fields using pickers/drop targets. Confirm valid references reach the script.
- Type global in engine editor/VS Code: find its help/snippet, highlighting,
  diagnostics, symbol completion and hover. Update the installed VSIX first.
- Old expose declarations remain readable with saved overrides.
  Keywords inside strings/block comments, mid-line or as identifier prefixes must
  not create fields. Errors keep their original source line numbers.
- Verify bundled recoil/ImGui examples use global and load successfully.

## Orthographic editor movement

- Select a cube, switch the Scene center cube to orthographic, and drag each
  visible axis arrow and plane handle. Small pointer motion must produce smooth,
  bounded movement instead of large positive/negative jumps.
- Repeat with objects far from the camera target, after pan/zoom, near the view
  edges, at several camera distances and in front/back/left/right/top/bottom views.
  Exactly view-aligned (screen-degenerate) axes should not cause runaway movement.
- Repeat World/Local, rotated/scaled/parented objects, origin/center pivot,
  Ctrl snapping, multi-selection, rigid bodies and camera/light objects. Confirm
  hierarchy undo/redo and restoring the rigid body after release/focus loss.
- Rotate/scale in orthographic and perspective views; projected handles must stay
  aligned with objects. Perspective dragging and collider face editing must retain
  existing behavior. Toggling projection must not alter scene Camera components.
