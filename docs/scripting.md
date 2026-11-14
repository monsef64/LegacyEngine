# LegacyScript

[Complete Scripting Reference: every function, method and event with examples](scripting-reference.md)

Create a `.ls` asset, save it, and attach it to a GameObject. Multiple independently
enabled scripts run in isolated Lua states with `self` bound to their owner.
Existing `.lua` assets remain supported. Lifecycle callbacks are `onStart`,
`onUpdate(deltaTime)`, `onFixedUpdate(deltaTime)` and `onDestroy`.

```lua
@export target GameObject
@export speed number 5

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
Declarations must lead a line; `---@export` comments remain compatible. Bare
`@export` is preprocessed before Lua runs, preserving error line numbers.
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

Waits are non-blocking and only supported inside lifecycle/object callbacks, not
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
in `scripts/examples/character_controller_fps.ls`.

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
