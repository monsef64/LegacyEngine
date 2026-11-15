# Animation manual verification

These are manual checks, not automated tests. Build success alone does not verify visuals.

## Hierarchy and property clips
- Verify self-inclusive parent/children component lookup: self, nearest parent, then BFS children; missing returns nil.
- Create a clip in Windows > Animation with Transform, light color/intensity and bool tracks.
- Capture at least two keys, edit values/times, scrub, and verify linear numeric and stepped bool interpolation.
- Verify preview does not dirty the level; explicit Apply frame is undoable.
- Save/reopen clip and level, prefab-clone an animated object, and verify persistent Animation settings.
- In Play mode check play/pause/resume/stop/reset and the five lifecycle callbacks.
- Stop must rewind; reset must restore original property values; only a natural nonlooping end fires finish.
- Reset, change original properties, replay/reset again: the new pre-play values must restore.

## Imported animated models
- Reimport a rigged glTF/FBX with multiple animations and multiple materials.
- Click the asset: check Model Preview lists every clip and shows the bind pose initially.
- Check Play/Pause/Stop/Reset, scrubbing, duration, looping and clip switching.
- Check skinning, normals and nonuniform bone scale against the source model.
- Place via context menu; assign generated Animation_N.leanim clips on the root.
- Play on start is opt-in; verify Lua playback and event callbacks as for property clips.
- Duplicate the model and play different clips/times: instances must remain independent.
- Save/reload the level; stop/start Play mode; switch projects and revisit previews.
- Confirm old static models, node-instanced meshes and multiple mesh/material imports still render correctly.
- Confirm Blend files and morph-animation models give explicit errors.
- Check animated visuals/shadows in Scene and Game; colliders intentionally remain static.

## Platforms
- Linux and Windows Vulkan editor builds and interactive runs need separate verification.

## Skinning and blending
- Verify CPU-skinned geometry is uploaded and rendered correctly on a Vulkan device.
- Verify Scene/Game, transparent/unlit materials and cascaded shadows all use the animated pose.
- Check nonuniform/negative bone scale, unweighted vertices, meshes with more than four influences, and multiple independent instances.
- Profile playback, including CPU deformation and vertex uploads.
- Cross-fade between walk/run/idle clips with 0, 0.25 and longer durations; verify local rotations do not collapse.
- Interrupt a fade with another transition: the pose must remain continuous.
- Pause/resume mid-fade; stop/reset mid-fade; loop during a fade and verify event semantics.
- Cross-fade property clips (including bool and quaternion tracks), then reset; original values must restore.
- Try invalid/missing clips, negative/NaN durations and clips for different models; prior playback must survive.
- Switch projects, reimport models and close the engine; verify skinning resource cleanup.

## Viewer performance and dope-sheet regression checks
- With a large animated model selected in Assets, compare FPS with the selection cleared. The import Inspector should not reparse JSON every frame.
- Open, pause, minimize, close and reopen Model Preview. A paused view should reuse its rendered texture, while scrubbing/camera motion/clip changes should redraw.
- Check nonuniform-scale/reflected/sheared bind transforms and files with repeated node names after reimport. Verify bone attachment and deformation against the source application.
- Check existing imported clips (without stored node names) still open using fallback bone labels.
- Open Windows > Animation: check the top transport bar, frame/sample controls, shared ruler, frozen property column, scrolling/zoom, collapsible target groups and diamond keys.
- Scrub the ruler and lanes; select and drag a property key. Neighbouring keys must remain ordered; read-only imported keys must not change.
- Double-click a property lane / Capture key, edit its value, switch to Curves, save/reopen and verify keys persist.
- Expand the optional isolated preview and pause: confirm it does not keep cloning/rendering the world while idle, and updates after editing keys.
- Verify animated shadows, normal maps and per-instance poses after switching between Scene, Game and both previews.
- Verify preview lighting is brighter without changing lights in the edited scene.

## Saving and position channels
- Edit several tracks, focus the Animation window and press Ctrl+S: all clip keys and values must persist after reopening, without opening the project Save As dialog.
- Verify Ctrl+S while a numeric key field or clip-settings popup is focused, as well as the Save animation toolbar button.
- Save Project / Save Level / autosave with a dirty clip: verify the clip is written too. A clip-save failure must remain visible and must not report a successful project/level save.
- Verify unsaved clip edits participate in the project's unsaved-change/exit prompt.
- Add Transform.position: verify X, Y and Z subchannels show their sampled values. Select a key and edit each labeled axis, save/reopen and scrub to verify all three values.
- Position axes share vector-key times: dragging a diamond on any axis must keep the vector key ordered and update its time on all three lanes. Curves should display the selected axis.

## Imported bind geometry versus default bone pose
- Import a rigged arms/weapon FBX whose default joint transforms differ from its
  authored skin bind pose. Without starting playback, compare the mesh against
  the source application's bind geometry: the importer must not deform it using
  default bone transforms.
- Check Assets thumbnails, Model Preview before Play and after Reset, normal scene
  placement, import-scale changes, static collider geometry and calculated bounds.
  These paths must all use original mesh-local vertices plus the mesh-node transform.
- Play/scrub an actual clip and verify bone weights still deform it. Reset should
  restore the undeformed bind mesh, not frame zero or the default skeleton pose.
- Check both compressed and legacy JSON .lemesh assets. Current skeletal imports
  retain raw vertices, so this loader fix does not require reimporting those assets;
  restart/reopen the editor to clear already-built geometry/preview caches.
- Verify transformed mesh nodes (including nonuniform scale), multiple mesh parts,
  unweighted vertices and ordinary non-skinned models remain correctly placed.
- Visual confirmation on the user's affected source FBX/glTF is still required.

## Copy/paste property keyframes

- Click an editable dope-sheet key and press **Ctrl+C** (or **Copy key**).
  This copies the complete property's value, including all X/Y/Z lanes.
- Select the destination property track, then click an empty point on its timeline
  or set Frame to choose the playhead time. Press **Ctrl+V** or **Paste at playhead**.
- **Copy track keys** copies every key on the selected track. The earliest copied
  key lands at the destination playhead; the others retain their relative timing.
- Paste supports another time, another target with the same property, or another
  clip. Save/discard pending edits before opening another clip as usual. Clipboard
  data owns its values and survives switching clips and deleting the source track.
- Existing keys at matching times are replaced; unrelated keys remain. The clip
  extends when necessary, up to 86400 seconds. Paste is atomic and respects 4096
  keys per track and 16384 keys per clip. Successful paste marks the clip unsaved
  and refreshes its isolated preview; Ctrl+S saves it normally.
- Different property types/paths are deliberately not converted: paste is enabled
  only for the same property (for example Transform.position to Transform.position).
  Imported skeletal channels remain read-only. This is an editor-local key clipboard,
  not the OS text clipboard, arbitrary multi-selection, or a clip-edit Undo feature.
- Manually verify single keys, full tracks, replacing existing keys, pasting at
  time zero/end, duration extension, cross-target/cross-clip copies, deleted source
  tracks, quaternion/boolean values, key limits and disabled Play-mode pasting.
  Confirm Ctrl+C/V in text fields still copies/pastes text instead of keyframes.
