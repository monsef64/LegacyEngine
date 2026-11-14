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
- Linux OpenGL editor and Windows MinGW-w64 builds/interactive runs need separate verification.

## GPU skinning and blending
- On an OpenGL 3.3 device, verify the skinning shader compiles and links at first playback.
- Verify Scene/Game, transparent/unlit materials and cascaded shadows all use the animated pose.
- Check nonuniform/negative bone scale, unweighted vertices, meshes with more than four influences, and multiple independent instances.
- Profile playback: only palette uploads should be dynamic; no per-frame CPU vertex deformation/readback.
- Cross-fade between walk/run/idle clips with 0, 0.25 and longer durations; verify local rotations do not collapse.
- Interrupt a fade with another transition: the pose must remain continuous.
- Pause/resume mid-fade; stop/reset mid-fade; loop during a fade and verify event semantics.
- Cross-fade property clips (including bool and quaternion tracks), then reset; original values must restore.
- Try invalid/missing clips, negative/NaN durations and clips for different models; prior playback must survive.
- Switch projects, reimport models and close the engine; verify skinning resource cleanup.
