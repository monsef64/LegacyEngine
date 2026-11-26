# Animator state-machine controllers

Animator is a component that drives the existing Animation player using a
single-layer state machine stored in a project-relative `.controller` asset.
It supports both property-animation clips and model/skeletal `.leanim` clips.
The Animation component remains available for direct clip playback without a
controller. AnimatorSheet, additive animation, blend trees and animation layers
are not part of this system.

## Create and edit a controller

1. Select a GameObject and choose **Add Component > Animator**.
2. Use **Create controller**, or assign an existing `.controller` using the picker
   or asset drag-and-drop and click **Edit controller**.
3. The graph also opens through **Windows > Animator** or by double-clicking a
   `.controller` asset in Assets.
4. Drop `.leanim` clips onto the grid to create states. Alternatively, use
   **Add animation state** and assign the selected state's clip in the properties
   panel. Each state has a unique name and a **Loop** checkbox.
5. Right-click a state and choose **Set as entry**. The green Entry node points to
   the state entered automatically when **Play on start** is enabled.
6. Right-click a source state > **Create transition**, then click the destination.
   **Link from Any State** creates a transition that can match any current state.
7. Click an arrow, or select it under **All transitions**, to edit its settings.
   Use **Priority up/down** to change evaluation order.
8. Choose a project-relative file path such as `Animations/Player.controller` and
   **Save**. Controller creation from an Animator component assigns the saved file
   back to that component. Save Project/Save Level/Play also flush dirty controller edits; invalid drafts prevent Play until saved or discarded.

Drag nodes to arrange them; middle-drag empty grid space to pan. Layout is saved
with the controller but has no effect on playback. A controller may be saved empty
as a placeholder. Every state added to it must have a valid `.leanim` asset path
before it can be saved. Unsaved drafts are retained when the window is hidden;
save or discard before opening another file. Asset saves are not scene undo actions.

## Parameters and transitions

Add **Bool**, **Int** or **Float** parameters in the properties panel. Defaults are
stored in the controller; runtime values belong to each Animator instance, not the
shared controller. During Play, the panel shows/edit values for the bound instance
and highlights its active state. Graph authoring and file saving are disabled in Play.

Transitions contain:

- **Fade seconds**, from 0 to 3600. Zero cuts immediately. Fades use the existing
  Animation cross-fade backend; their effective duration is capped to the target
  clip's duration. Model clips must belong to the same model/skeleton; property
  clips fade their compatible property tracks.
- **Has exit time** and a normalized **Exit time** from 0 to 1. With exit time enabled,
  the source must reach that point in its first playback cycle before the transition
  can match. A completed nonlooping state holds its final pose and can still transition
  later when its conditions become true. This gate stays open after the first cycle.
- Zero or more typed **conditions**. All conditions must pass (AND). Bool supports
  `==`/`!=`; Int and Float also support `<`, `>`, `<=`, `>=`. Prefer inequalities for
  Float thresholds rather than relying on exact floating-point equality.

The first matching transition in file/list order wins. Any State is not implicitly
higher priority; place its transitions earlier when needed. Any State does not
continually restart its current destination. At most one automatic transition is
entered per update. Automatic transitions wait for an in-progress fade to finish;
an explicit scripted `play` or `crossFade` can interrupt it. Empty conditions with
exit time disabled create an unconditional transition, so avoid accidental cycles.
New transitions default to exit time 1 to avoid immediately leaving a new state.

## Scripting

Attach the script to the Animator's GameObject or resolve an Animator through
`getComponent`, `getComponentInParent`, `getComponentInChildren`, or a
`global Animator character` Inspector reference. Declare the parameter names/types
in the controller first. Missing parameters, wrong types, unknown states and invalid
fade values produce script diagnostics rather than silently creating parameters.

```lua
local animator
function onStart()
    animator = self:getComponent(Animator)
    animator:setBool("Grounded", true)
    animator:setInt("Mode", 0)
    animator:setFloat("Speed", 0)
end

function onUpdate(dt)
    if isKeyDown("w") then
        animator:setFloat("Speed", 4)
    else
        animator:setFloat("Speed", 0)
    end
end

-- Call from your own lifecycle/object callbacks when needed:
-- animator:crossFade("Jump", 0.15)
-- animator:play("Idle")             -- hard cut / restart
-- animator:pause()
-- animator:resume()
-- animator:stop()                   -- stop evaluation; backend's first pose
```

Additional methods: `getBool(name)`, `getInt(name)`, `getFloat(name)` and
`hasState(name)`. State methods accept a state's display name or stable ID.
`crossFade` defaults to 0.2 seconds. Parameter numbers are finite and limited to
+/-1,000,000; Int values must be integral. Pending commands are bounded to 64.

Writable properties: `controller`, `enabled`, `playOnStart`.
Read-only properties: `currentState`, `stateTime`, `isPlaying`, `paused`, `error`.
`isPlaying` means the state machine is running, enabled and unpaused; it may be
holding a finished nonlooping clip while waiting for a transition. `stateTime`
measures completed playback samples, including loops, and freezes while paused.

Do not drive the same object's Animation backend directly while Animator owns it.
The backend is created as needed at runtime and is hidden from Animator's Inspector;
the existing animation callbacks continue to come from that player. Disabling the
Animator or its GameObject pauses its backend; re-enabling resumes it. Stop Play and
level replacement cancel runtime controller state along with the rest of the world.

## Assets, persistence and compatibility

Scenes/prefabs save the Animator's controller path, enabled flag and play-on-start
setting. Duplication/prefab instantiation start with fresh runtime state. Runtime
parameter changes do not modify controller defaults. Saving a shared controller
changes the definition used on the next Play/controller load; it is not live hot-reloaded.
Parsed controllers use a bounded, metadata-checked cache and immutable definitions;
per-object parameter values and commands are not shared.

Export/dependency preloading follows `.controller` files and all their referenced
clips, including model dependencies; controller parsing is warmed on the preload worker.
The existing level preload budget of 64 distinct clips still applies. Keep controller/clip paths project-relative.
Rename/copy operations retarget controller references through the normal asset tools.
Malformed controllers, missing clips and incompatible fades are reported through
Animator's error field and the existing script/animation diagnostics.

Controller files use `LegacyAnimator`, version 1. Old removed Animator/AnimatorSheet
settings are not migrated: old scene data is consumed without recreating that old
system. The new component uses the reserved Animator serialization slot, preserving
existing scene schema compatibility without reviving AnimatorSheet.

See `data/examples/animator/` for a self-contained controller, three property clips
and a script exercising Bool/Int/Float conditions and fades.

## Graph navigation and history

Use the mouse wheel for pointer-anchored zoom (25–250%), middle-drag or
**Alt + right-drag** to pan, and plain right-click for context actions. Existing
toolbar buttons remain. Undo/Redo buttons and Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z edit
the focused controller rather than the level. History includes nodes, positions,
parameters, transitions and conditions, coalesces a drag, and is bounded to 100
controller snapshots. Opening another controller resets its history.

Transitions use node-aware orthogonal routing, distinct endpoint ports and a
shared-segment penalty to separate reciprocal/parallel connections. Click any
drawn segment to select its transition; self-transitions route around their node.
Route geometry is cached and is unaffected by pan/zoom. Dense non-planar graphs
can still require crossings, and overlapping nodes should be moved apart.
