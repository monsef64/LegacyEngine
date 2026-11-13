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
