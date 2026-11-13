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
