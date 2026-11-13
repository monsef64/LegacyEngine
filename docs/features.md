# Current features
- Dockable Scene, Game, Hierarchy, Inspector, Assets, Script Editor and Console panels.
- Empty GameObjects, cube/sphere/capsule/cone primitives, cameras and audio sources.
- Transform gizmos and labeled X/Y/Z fields; Inspector rotation is limited to ±360°.
- Physics colliders and rigid bodies, parenting, duplication, clipboard, Undo/Redo.
- Play, Pause/Resume and Stop with restoration of the edit-time scene.
- Hierarchy search, object icons and a nonmovable level root that supports renaming.
- Dynamic directional, point and spot lights with bounded shadow maps and PCF filtering.
- Camera projection, tracking/following, shake and screen/world conversion APIs.
- AudioSource and AudioListener, WAV/MP3 playback, looping, pitch, volume, pan,
  Spatial Blend, range attenuation, fades and one-shots.
- Project-owned Assets with folder browsing, file import/rename/trash, drag-and-drop
  script/reference assignment and listener-independent audio previews.
- Internal Lua editor with highlighting or external VS Code editing; Dark/Grey themes.
- Three-slot named keyboard/mouse/gamepad-button mappings, raw input APIs and cursor controls.
- Structured JSON projects, separate settings files and native Save/Open dialogs.

- Project-owned Diffuse/Diffuse Bumped material assets and six-face/HDR skyboxes.
- Debug menu with FPS display and collider wireframes in the Game view.

## Editor and scripting additions

- Settings Undo/Redo in Edit mode, with Console logging.
- Play focuses Game, with an optional separate native Game window.
- Compact transform toolbar and icon-only asset actions; folders sort before files.
- Material sphere thumbnails and enlarged hover previews.
- GameObject activity inherited through parenting, saved with levels.
- Targeted mouse callbacks and AudioSource playback callbacks.
- Non-blocking `waitForSeconds(seconds)` and `wait(audio.onAudioFinished)`.
- VS Code language-server error reporting and bounded automatic recovery.
- [Complete offline scripting reference](scripting-reference.md) with signatures,
  examples, callback requirements and wait restrictions.

See [editor controls](editor.md#settings-history-and-play-destination),
[mouse/audio callback setup](audio-input.md#gameobject-mouse-callbacks), and
[activity and waits](scripting.md#object-activity-mouseaudio-callbacks-and-waits).

## Prefabs and asset inspection

- Drag hierarchy objects into Assets to create linked prefab files; use isolated
  [Prefab Window editing](prefabs.md) to save and update instances.
- Spawn prefab paths or object trees, and schedule delayed subtree destruction.
- Skyboxes use HDR panoramas or reusable cubemap assets with a six-face editor.
- Scene tools have a dedicated bar below the Scene tab, outside the viewport.
- Material selection uses preview tiles; material/texture/HDR hover previews use
  higher-resolution thumbnails.
- Offline documentation highlights LegacyScript/Lua and other fenced code blocks
  without JavaScript or network resources.

### Transform and hierarchy editing

The Scene move/rotate/scale toolbar includes a **World / Local** axis switch.
Child Transform fields in the Inspector are **parent-local** position, rotation
and scale; a child at local `(0, 0, 0)` sits at its parent's origin. Reparenting
preserves world pose. Runtime ECS, physics and Lua Transform values remain
world-space. Zero parent scale collapses that local axis (inverse conversion uses
zero rather than dividing by zero). Component sections use accented headers;
inactive Hierarchy rows are gray but remain selectable and editable. The Hierarchy
search has an `x` clear button.

Deleting a parent GameObject deletes its entire subtree, including inactive children.
Unparent children first to preserve them; editor Undo restores the deleted hierarchy.

- Foldable, multi-select Hierarchy with group delete/reparent and blank-space unparent drops.
- Hard/Soft project shadow filtering, independent of CSM/single-map mode and resolution.
- Scripted upright capsule CharacterController and FPSCamera components, creation menus,
  persisted configuration, direction helpers and a [first-person example](first-person.md).
