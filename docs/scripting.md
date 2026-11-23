# LegacyScript

[Complete Scripting Reference: every function, method and event with examples](scripting-reference.md)

Create a `.lua` asset, save it, and attach it to a GameObject. Multiple independently
enabled scripts run in isolated Lua states with `self` bound to their owner.
Existing `.lua` assets remain supported. Lifecycle callbacks are `onStart`,
`onUpdate(deltaTime)`, `onFixedUpdate(deltaTime)` and `onDestroy`.

```lua
expose GameObject target =
expose float speed = 5

function onStart()
    debug.log("Started", self:getName())
end

function onUpdate(dt)
    if target and isButtonDown("Move") then
        target:translate(speed * dt, 0, 0)
    end
end
```

Define `Move` in Project → Input Settings first. Exported fields appear in the
script's Inspector. Drag objects/components/assets into reference slots; **X**
clears them. Supported values include number, boolean, string, GameObject, engine
component types, Asset and AudioClip. Defaults are literals, not Lua expressions.
Declarations use `expose Type name = default` at the beginning of a line and are
preprocessed before Lua runs, preserving error line numbers. Old `@export` and
`---@export` declarations remain readable for existing projects; use `expose`
for new scripts. Integers use Lua integer values; float, bool and string defaults
are supported, as are `Prefab` and `AnimationClip` asset fields.
Exports currently become globals in the attachment's state: do not shadow them
with local declarations. Missing references bind as `nil` at script startup.

The Script Editor's Execute action resolves the open asset's selected owner, or its
unique owner automatically. Shared assets require selecting an owner. Open assets
are not additionally run as global snippets on Play. Save asset edits before Play.

Logging: `debug.log(...)`, `debug.warning(...)`, `debug.error(...)`, or `print(...)`.
`debug.error` logs without throwing. This restricted `debug` table is not Lua's
introspection library. The Console offers **Copy all** and right-click **Copy message**.
Lua has bounded memory/instructions and no filesystem/process/module access.

See also [Audio and input](audio-input.md), [materials](materials.md), and [skyboxes](skybox.md) for more component scripting examples.

## File-backed Script Editor

The Script Editor edits only project `.ls` and `.lua` files. Its left explorer
filters out other assets and keeps script folder structure. Open a script there
or double-click it in Assets. Use **Save** / **Ctrl+S** to save the document;
switching files, saving a level/project, and starting Play also flush pending file
edits. No open script means the document is read-only. New scene saves no longer
embed editor text as an anonymous runtime script; attached assets own executable
code. The loader still understands older project formats and preserves embedded
legacy text unchanged on save, but does not run it as an anonymous snippet. Move
legacy code into an attached script asset to execute it.

Errors include script filenames, line numbers and tracebacks in Console/logs.
A failing lifecycle script stops updating instead of flooding the Console. An
`onStart` failure is reported as execution failure; `debug.error()` remains a
nonthrowing log call.

## Custom events

Events are shared between the active script instances. Event names are strings;
no declaration step is required. Subscribe and keep the returned listener token:

```lua
local scoreToken
function onStart()
    scoreToken = on("scoreChanged", function(score, playerName)
        debug.log(playerName, "has", score, "points")
    end)
    emit("scoreChanged", 10, "Player")
end
function onDestroy()
    off(scoreToken)
end
```

`on(name, callback)` returns a token, `off(token)` removes this script's listener,
and `emit(name, ...)` returns whether the event was queued. Up to eight arguments
may be nil, booleans, numbers or strings; tables, engine handles and functions are
not transferred between isolated Lua states. Names are limited to 128 bytes,
strings to 4096 bytes, each runtime to 128 listeners and the queue to 256 events.

Delivery occurs between script updates at a safe frame boundary. Events emitted
by a listener wait for the next pump, preventing immediate recursive dispatch.
The subscription set is checked at delivery: removing a listener prevents later
calls, and new listeners do not receive the event currently being handled.
A failing listener logs a traceback and is removed; other listeners continue.
Stopping/resetting a runtime removes its listeners. Level changes and Stop clear
queued events so stale messages do not leak into another level/session.

## Level callbacks

```lua
function onLevelLoaded(name)
    debug.log("Loaded", name)
end
function onLevelFailedToLoad(name, reason)
    debug.warning("Could not load", name, reason)
end
```

`onLevelLoaded(name)` runs on destination scripts after they start, including the
initial Play level. `onLevelFailedToLoad(name, reason)` runs in the still-active
level when a requested file cannot be read/validated or a load request is rejected.
These hooks can also be observed with `on("onLevelLoaded", callback)` and
`on("onLevelFailedToLoad", callback)`. Unloading is not a successful load event.
Scene replacement remains deferred; never expect `loadLevel()` to replace
`self` or invoke callbacks inside the calling Lua function.

## Object activity, mouse/audio callbacks, and waits

```lua
function onMouseDown()
    debug.log("Clicked", self:getName())
end

function onAudioFinished()
    debug.log("Primary clip finished naturally")
end

function onStart()
    local audio = self:getComponent(AudioSource)
    waitForSeconds(5)
    if audio then
        audio:play()
        wait(audio.onAudioFinished)
        debug.log("Playback completed; continue here")
    end
end
```

Mouse callbacks are `onMouseDown`, `onMouseUp`, `onMouseEnter`, `onMouseExit`, and
`onMouseOver`. Put a Collider on the object. Picking uses the active Game camera,
its viewport, and the nearest active Collider under the Game image (or separate
Game window). Down is the left-button press; Up goes to its original pressed
object. Cursor lock picks at the center. Over runs every playing frame.

AudioSource owner scripts receive `onAudioStarted`, `onAudioFinished`,
`onAudioPaused`, `onAudioResumed`, and `onAudioStopped`. Natural completion of the
primary clip triggers Finished; explicit stop/fade-out stop triggers Stopped.
Pausing/resuming does not restart the clip. Looping clips do not naturally finish;
one-shots do not produce primary-clip events. Audio decode failure stops the
source with diagnostics rather than claiming successful completion.

Use `self:setActive(false)` and `object:isActive()` to control/query activity.
Reactivation must come from another active script or Inspector. A paused script
keeps its variables and resumes rather than rerunning onStart.

Waits are non-blocking and only supported inside lifecycle/object callbacks or their `task` blocks, not
at file top-level, inside custom/level event listeners, or in onDestroy. Each
callback can have one pending invocation; other callbacks still run. Seconds are
playing-time seconds (0–86400, zero waits until the next update). Pause and
inactivity freeze waits; Stop/runtime reset cancels them. `wait(audio.onAudioFinished)`
listens for the next occurrence after the wait is reached, not a past occurrence.
Destroying/removing the source cancels its waiting continuation. Instruction and
memory budgets remain enforced when a callback resumes.

## Prefabs, spawning, and implicit components

See [Prefab workflow](prefabs.md) for asset creation, isolated editing and updates.
`getComponent(Type)`, `addComponent(Type)` and `removeComponent(Type)` default to
attached `self`; their explicit `(object, Type)` forms still work. They require an
attached object when the object argument is omitted.

```legacyscript
function onMouseEnter()
    getComponent(MeshRenderer):setMaterialColor(1, 0.6, 0.3)
end

function onMouseExit()
    getComponent(MeshRenderer):setMaterialColor(1, 1, 1)
end
```

Put a Collider and MeshRenderer on this object. Enter/Exit follow the nearest
Collider under the pointer in Game, not the mesh's exact visible pixel outline.
Match Collider dimensions to the object: an oversized Collider keeps the mouse
inside its pick area. Callback errors stop the runtime; inspect Console if Exit
does not fire after an earlier failing Enter. Moving into blank Game space or
outside Game sends Exit to the previously hovered active object.

```legacyscript
function onStart()
    local copy = spawn(self, {x=2, y=1, z=0}, {x=0, y=45, z=0})
    destroy(copy, 3)
end
```

Avoid unconditional self-cloning in onStart on a scripted source: its cloned
script will also run and spawn again. Prefer a prefab with a separate spawner
script. Both `destroy` and `destroyGameObject` remove the object and **all descendants**,
including inactive children. `destroyGameObject(object)` is immediate;
`destroy(object, seconds)` optionally delays removal. `object:destroy()` is immediate.
Attached scripts receive `onDestroy` once. To preserve a child, call
`child:setParent(nil)` **before** deleting its parent (this preserves world pose).
Editor deletion follows the same subtree rule and supports undo/redo.

See [Character controllers and FPS cameras](first-person.md) for movement APIs,
state flags, GameObject direction vectors, and the complete attached-script example
in `scripts/examples/character_controller_fps.lua`.

[Physics scripting and queries](physics-queries.md) covers RigidBody methods,
`color.red`-style RGB values, Collider trigger callbacks, Raycast results and layer masks.

## Mesh colliders

```lua
local collider = self:getComponent(Collider) or self:addComponent(Collider)
collider.shape = "Mesh"
collider.meshDetail = 15 -- 2 = simpler, 15 = original triangles
collider.model = "Models/Crate_data/Mesh_0.lemesh"
collider.center = {x=0, y=0, z=0}
```

Mesh colliders use real triangle geometry, including concave surfaces and holes,
not a convex hull. They are **static only**: a RigidBody on the same object will
not make it move. Use Box/Sphere/Capsule/Cone colliders for moving bodies.
Import scale and Transform scale apply to the collision geometry. Triangle
winding controls which surface side collides; mirrored scales reverse winding.

`meshDetail` ranges from 2 to 15 (default 8). At 15 the original non-degenerate
triangles are retained. Lower settings cluster vertices and remove collapsed
triangles; this can remove small features or close small openings. This is not
an exact triangle-count slider. The render model is unchanged. Invalid or empty
triangle sources are disabled and logged. Empty `model` follows the `.lemesh`
source on this same object's MeshRenderer; an explicit `model` overrides it.
Objects without a baked rendered model need an explicit `.lemesh` collision
source. There is no box fallback. Put the collider on a model's mesh child or
assign the complete bundle explicitly when editing its empty parent root.
Source/detail/scale changes rebuild the collider. The Scene/Game collider overlay
shows the simplified collision triangles rather than a placeholder box. Selecting
Mesh in Inspector starts at detail 15 to match the source geometry.

Old saved collider shape 4 (Convex) migrates to static Mesh, retaining its source
and detail setting. Old JSON `convexDetail` is accepted when `meshDetail` is absent.
Lua accepts the old `"Convex"` shape name and `convexDetail` property as migration
aliases, but reports the shape as `"Mesh"`.

`MeshRenderer.model` also accepts a `.lemesh` path. Instantiate a model bundle
through the editor to preserve separate part materials. A single MeshRenderer
assigned a bundle uses its one assigned material. Choosing a primitive clears
its model reference.

## Hierarchy component searches

All four methods include the calling object before searching the hierarchy:

```lua
local body = self:getComponentInParent(RigidBody)
local camera = self:getComponentInChildren(Camera)
local collider = findComponentInParent(self, Collider)
local light = findComponentInChildren(PointLight) -- attached self
```

`getComponentInParent` walks nearest parent first. `getComponentInChildren`
searches descendants breadth-first, so a nearer match wins over a deeper match.
`findComponentInParent` and `findComponentInChildren` are aliases with the same
behavior. Each returns the first live component or `nil`, never a list and never
creates a component. Searches include inactive objects. Equal-depth child matches
use the engine's component traversal order; do not rely on a particular sibling
winning when several carry the same component.

Each supports `object:method(Type)`, `object.method(Type)`,
`method(object, Type)`, and implicit `method(Type)` for an attached script's self.
String component names are also accepted. Returned component handles refer to the
matching owner, not necessarily the object that started the search. Destroyed or
foreign object handles are rejected; cycles cannot cause infinite traversal.


## Property animation

Open **Windows → Animation** to create/edit a project-relative `.leanim` clip. Select the
GameObject to preview, add tracks targeting self (empty path) or named descendants
(`Arm/Hand`), select a property and capture keys from the selected object's current values.
Edit key times/values in the linear timeline, scrub or use Play/Pause/Stop/Reset.
The isolated preview does not execute scripts or physics or modify the scene.
**Apply frame to Scene** is explicit and undoable. Save the clip, then assign it to the
selected object's Animation component. Numeric/vector values interpolate linearly,
rotations use shortest-path normalized quaternion interpolation, and booleans step.

```lua
local animation = self:getComponent(Animation) or self:addComponent(Animation)
animation.clip = "Animations/example.leanim"
animation.loop = false
animation:play()
-- animation:pause(); animation:resume()
-- animation:stop()   -- stop and rewind to the first frame
-- animation:reset()  -- stop and restore the pre-play property values
self.onAnimationFinish = function()
    debug.log("Animation finished")
end
```

`isPlaying`, `paused`, `time` and `error` are read-only. `playOnStart` opts in to automatic
playback. Lifecycle callbacks are `onAnimationStart`, `onAnimationFinish`,
`onAnimationStopped`, `onAnimationPause`, and `onAnimationResume`. Finish is emitted only
at a nonlooping clip's natural end, not on stop/reset or loop boundaries.
### Imported skeletal animation

Reimport previously imported animated models: older `.lemesh` assets were baked to static
geometry and cannot recover their discarded skeletons. New imports preserve node transforms,
bones, weights and all skeletal animation clips (up to the documented import limits).
Each animation generates `<model>_data/Animation_N.leanim`. Assign one of these assets to
an Animation component on the instantiated model root, or use the first clip assigned on
placement. `playOnStart` is opt-in. The same Lua controls and lifecycle callbacks apply.
Static meshes, multiple mesh parts/materials and instanced nodes remain supported.

Click a `.lemesh` in Assets to open **Model Preview**. Select any imported animation in
the dropdown; Play/Pause/Stop/Reset and the time slider operate on an isolated preview.
Stop rewinds the selected clip, while Reset returns to bind pose. Right-drag looks around
and the wheel zooms. Use the asset context menu **Instantiate in level** to place a model.

Skinning runs on the GPU using OpenGL 3.3 transform feedback. Rest vertices and all
bone influences stay in static GPU buffers; the CPU samples/blends node poses and
uploads only a matrix palette. Lit, unlit, transparent and shadow passes consume
the same GPU-skinned vertex buffer. Bounds are conservative palette-transformed
source bounds; no vertex readback is required. Asset-cache invalidation and renderer
shutdown release skinning buffers. Hardware texture-buffer limits are checked.
CPU bind-pose evaluation remains available for import bounds and static colliders.
Morph-target animation, retargeting and animated physics colliders remain unsupported.

### Clip blending

```lua
local animation = self:getComponent(Animation)
animation:crossFade("Character_data/Animation_1.leanim", 0.3)
```

`crossFade(clipPath, seconds)` starts the destination clip at zero and transitions
over the supplied duration (default 0.25 seconds; zero switches immediately).
Duration must be finite and between 0 and 3600 seconds, and is limited to the
incoming clip's length. Skeletal clips must reference the same model/skeleton;
retargeting between different models is not implicit. Two property clips can
also cross-fade: destination properties blend from their current values, rotations
use shortest-path quaternion interpolation, and bools switch halfway through.
Properties absent from the destination retain their current values.

Skeletal blending interpolates local translation/scale and shortest-path quaternions
before accumulating the node hierarchy. Both clips advance during a normal fade.
Interrupting a fade captures the currently blended local pose as the new source,
so successive transitions do not snap back to an earlier clip. Pause freezes the
fade; Resume continues it. Play, Stop and Reset cancel an active fade. Reset still
restores the original pre-play properties across all transitioned clips.
`onAnimationStart` fires for the destination, and `onAnimationFinish` only at its
natural nonlooping end; outgoing clips do not emit synthetic finish events.
Invalid transitions report the error via `animation.error` and preserve playback.

Model Preview has **Blend clip changes** and a duration slider. Change the animation
while playing to preview a fade. Inspector runtime controls also include a
cross-fade target asset and duration. Blend progress is transient, not serialized.


## Independent waiting blocks with task

A normal Lua `if`, `do` or loop is not a separate coroutine. Calling a wait directly
inside it still pauses the **current callback**, including code after that block.
Wrap the independent work in `task(function() ... end)` to pause only that task:

```lua
function onStart()
    local shouldRun = true
    if shouldRun then
        task(function()
            wait(2) -- same as waitForSeconds(2)
            debug.log("This block continues after two playing seconds")
        end)
    end
    debug.log("This runs immediately, without waiting for the task")
end
```

`task` starts immediately and runs until its first wait or completion. The caller
then continues. Nested tasks are supported; closures keep their captured locals.
It takes one function, returns no results/handle, and does not create OS threads.
CPU-heavy code before a wait still runs synchronously and is instruction-limited.
Use separate tasks for independent waits, including `wait(audio.onAudioFinished)`.

Tasks inherit their callback's owner/activity lifecycle. Play pause and inactive
owners freeze timer progress; destroying the owner or Stop/reset cancels pending
work. Removed audio sources cancel tasks waiting on those sources. Tasks and
callbacks share the runtime's 128-live-task cap and memory/instruction budgets;
errors use normal script error reporting. Tasks are not allowed at file top-level,
in custom/level listeners or during onDestroy/shutdown.

Do not spawn an unbounded task every `onUpdate`. Guard a repeating action:

```lua
local busy = false
function onUpdate(dt)
    if not busy then
        busy = true
        task(function()
            waitForSeconds(1)
            debug.log("Delayed action")
            busy = false
        end)
    end
    -- Other update logic keeps running while the guarded task waits.
end
```

Choose `onStart` for one-off work. If a task is canceled, code after its wait
(including clearing `busy`) does not run; reset your application state when needed.

## Camera effects

`CameraEffects` is a scriptable component for Camera objects. All effects default
off. Use `self:getComponent(CameraEffects)` / `self:addComponent(CameraEffects)`
and set boolean effect switches and numeric parameters directly. See
[CameraEffects controls, ranges and manual checks (repository)](../docs/manual/camera-effects-and-terrain-performance.md)
and the editor's generated API catalog. Anti-aliasing uses FXAA; motion blur is
camera-only; depth of field and ambient occlusion are bounded screen-space effects.

## Clouds and curve-based rivers

`CloudSystem` supports a cheap 2D layer and a bounded volumetric mode. `River`
provides an editable Catmull-Rom path and lightweight animated water. Both expose
validated fields to Lua; vector and point-list reads return copies. See the
[clouds and rivers guide](../docs/manual/clouds-and-rivers.md) (repository) for examples,
Scene editing, performance controls and rendering limitations.

## Lua file extension and old-project migration

New script assets use **`.lua`** throughout Assets and Script Editor creation.
The component and API remain named LegacyScript/LuaScript; only the default file
extension changes. Existing `.ls` files still load without conversion.

To convert an old project, stop Play, save your work, then choose **Project > Fix
Project**. It plans `.ls` → `.lua` renames, validates scenes/prefabs, backs up
changed originals as `.before-fix.bak`, updates saved level/prefab references and
the current world's primary/additional script attachments and exported paths,
then saves the current project. Direct quoted asset paths in scripts are updated;
dynamically assembled paths cannot be inferred and should be reviewed manually.
If both `name.ls` and `name.lua` exist, conversion stops before making changes
instead of overwriting either. Resolve the conflict and run Fix Project again.
Older undo/snapshot references can read a matching migrated `.lua` if their `.ls`
file is absent. Keep backups until you have reopened and checked the project.

VS Code and Sublime packages support both extensions. Reinstall/update the package,
then select **LegacyScript** as language mode if a standard Lua extension owns
`.lua` on your system.

## 100 attached-object Lua examples

The indexed collection is in
[`scripts/examples/lua100/README.md`](../scripts/examples/lua100/README.md).
There are exactly 100 numbered standalone scripts covering transforms, hierarchy,
keyboard/gamepad input, rigid bodies/triggers, materials/lights, audio, animations,
particles, navigation/obstacles, vehicles and small gameplay patterns. Copy a chosen
file into project Assets, read its setup header, attach it and focus Game in Play.
Use only one movement/vehicle driver per object. Audio/animation assets and navmesh
bakes must be supplied where requested; examples do not include extra model/audio
assets. Source syntax is checked, but individual scenes require manual testing.

### Keyboard sampling and fixed-step presses

Gameplay input focus is resolved from the current Game tab, including child windows.
Held keys are reconciled with SDL's physical keyboard snapshot so an initial missed
or repeated event cannot leave a held key permanently unrecognized. Press/release
edges are also queued until a fixed physics step consumes them: a render frame with
no physics step no longer loses a jump/action in `onFixedUpdate`. An edge is consumed
once, not repeated across catch-up fixed steps. `onUpdate` still sees its ordinary
per-render-frame edges. Do not trigger the same action in both callbacks.

Manual checks: click Game and hold W immediately, change tabs/focus with W held,
test press/release in an FPS/TPS setup and test a Space action in onFixedUpdate at
frame rates above/below physics frequency. Typing in editor text fields must not
drive gameplay; focus loss and Stop must clear held and pending key state.

## In-game UI

Add a `UI` component, assign its RML and RCSS assets, and use
`self:getComponent(UI)` or an exported `UI` reference. Call
`gui:setText(id, text)` and register `gui:onClick(id, callback)` once in onStart.
See [In-game UI](game-ui.md) for component setup and the interactive example.

### Two runtime interface systems

Use **UI** components for RML/RCSS documents and scoped `ui:onClick(...)` /
`ui:setText(...)`. Use the **GUI** namespace for script-authored ImGui controls,
drawn each rendered frame inside `function onGUI() ... end`. GUI is not a component
or export-reference type. See [in-game interfaces](game-ui.md).

### Advanced runtime GUI

`GUI` is the ImGui scripting namespace; `UI` is the RML/RCSS component. Define
`function onGUI() ... end` for per-rendered-frame drawing. Use the generated scripting
reference for images, tables, menus, popups, combos, radio buttons, style/font
scopes, typed drag/drop, custom drawing and mouse/key queries. Window flags combine
with Lua's `|`, for example `GUI.WindowFlags.MenuBar | GUI.WindowFlags.NoCollapse`.
Table column indices are zero-based; `GUI.combo` selections are one-based.

Always pair `beginWindow`/`beginChild`, even if they return false. Pair table,
menu, menu-bar, popup, combo and drag/drop scopes **only when begin returns true**.
Pop styles/fonts in reverse push order. Unbalanced scopes are unwound after each
subscriber, including script errors. Script payloads cannot spoof editor asset or
object drags. Images use project-relative paths, load asynchronously, and return
false until ready. Custom draw coordinates are Game-surface pixels, not Scene
coordinates. Drawing outside a window uses the background draw list.

## Named script components

Each attached `.ls` or `.lua` script is addressed by its filename **without the
extension**. Names are case-insensitive and unique across the entire project's
Assets tree, including subfolders and both extensions: `Health.lua` conflicts
with `health.ls`. Creation, import and rename reject collisions. Older projects
with collisions must rename the conflicting assets and update attachments before
using them; ambiguous scripts do not silently select an arbitrary file.

Retrieve an enabled, active attachment through `object:getScript("Health")` or
`object:getComponent("Health")`. Built-in component names take precedence with
`getComponent`; use `getScript` to disambiguate. Missing attachments return `nil`.
Resolve other scripts in `onStart` or later, not at file scope: top-level code
loads sequentially, while initial `onStart` callbacks run after all new script
attachments have loaded their globals.

```lua
-- Health.ls (attached to the target GameObject)
health = 100
function damage(amount)
    health = math.max(0, health - amount)
    return health
end
```

```lua
-- Another script on the same object (use an exported GameObject for other owners)
function onStart()
    local healthScript = self:getScript("Health")
    if healthScript then
        healthScript.health = 80
        local remaining = healthScript:damage(10)
        debug.log("Remaining health", remaining)
    end
end
```

Only public **global** scalar fields/functions are exposed; Lua `local` variables
remain private. Read/write/call transfer nil, boolean, finite numbers and strings
up to 4096 bytes, with at most eight arguments/results. Tables, functions and engine
handles cannot cross the isolated-state bridge. Leading-underscore names and
reserved lifecycle/runtime members are inaccessible. Calls are synchronous,
bounded to depth four and 256 named calls per frame; cyclic/reentrant calls into
another currently executing script are rejected. A retained proxy checks its owner
and enabled attachment on every access; stale/disabled access raises a Lua error.
Runtime values are not serialized back into project files; Stop restores edit state.

### Animation function events

In the Animation window expand **Script function events**. At the timeline time,
enter the Script component name, Function, and optional Child path (empty means
the animated GameObject itself), then add the event and save the clip. The named
function takes no arguments. A suitable callback in `Health.ls` is:

```lua
function animationHit()
    damage(5)
end
```

Events fire when playback crosses their time, including loop crossings, not while
scrubbing the editor timeline. The target must have an active, enabled script
attachment. Clips serialize their events alongside component/transform tracks.

## Typed exposed fields and prefab assets

```lua
expose int score = 10
expose float speed = 4.5
expose bool canFire = true
expose string title = "Player"
expose Prefab enemy = "Prefabs/Enemy.prefab"
expose AnimationClip walk = "Animations/Walk.leanim"

function onStart()
    if enemy ~= "" then spawn(enemy, {x=0,y=1,z=0}, {x=0,y=0,z=0}) end
    local animation = self:getComponent(Animation)
    if animation and walk ~= "" then animation:playClip(walk) end
end
```

Asset fields use the shared filtered picker and accept Asset browser drops.
`Prefab` fields hold project-relative `.prefab` paths (not instantiated scene
objects); pass them to `spawn`. `AnimationClip` fields hold `.leanim` path values,
which can also be made with `AnimationClip("Animations/Walk.leanim")`.
`animation:playClip(clip)` selects and restarts that clip without cross-fading.
The existing `play()` uses the assigned clip; `crossFade(clip, seconds)` transitions
from the current pose. First-time clip decoding runs on a worker; a new clip starts
when ready while the outgoing animation can continue. Stop/Reset cancel pending
playback and retain existing rewind/restore semantics. `animation.error` reports
load errors. Clip caches live for the animation session and clear on Stop Play.

`int`/`integer` fields become Lua integer values; `float`/`number` fields become
Lua numbers; `bool`/`boolean` and `string` are also supported. Numeric Inspector
fields/defaults retain the engine's ±1,000,000 limit. Defaults are literal values,
not expressions or function calls. Scene-component types continue to hold scene
references and are assigned separately from prefab asset paths.

## Random helpers

Use `Random.randomFloat(min, max)`, `Random.randomInt(min, max)`,
`Random.randomBool()`, and `Random.randomString(length)`. The same functions are
available globally as `randomFloat`, `randomInt`, `randomBool`, and `randomString`;
Integer bounds are inclusive. Floating bounds must be finite with min ≤ max.
Booleans are equally likely. Strings contain ASCII letters/digits, with a length
of 0–4096. These helpers are for gameplay randomness, not passwords, encryption,
or deterministic multiplayer/replays.
