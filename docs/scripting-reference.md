# Scripting Reference

Complete engine API reference: global functions, constructors, component/object methods,
lifecycle events, level events, custom events and exposed fields. Generated from the
engine scripting definitions; every callable below has a usage example.

[TOC]

## How to use the examples

Examples are independent fragments for an attached `.ls` or `.lua` script, where `self`
is its GameObject. Place setup/calls inside `onStart()` unless an example defines a
callback itself. They are not a single script to concatenate. Vector arguments can be
tables; object methods also accept numeric coordinates as shown. `:` supplies the
receiver; constructors and `debug` logging use `.`.

Import the example asset paths before running audio/material/skybox examples. Configure
named input buttons in Project > Input Settings and create/register the `Main` level
before level examples. Find/get functions can return nil; check before dereferencing.

See [LegacyScript guide](scripting.md), [input/audio details](audio-input.md),
[prefab workflow](prefabs.md), [level storage](projects.md#levels), and [skybox transitions](skybox.md#smooth-scripted-crossfades).

## Events and runtime rules

- `onStart()` runs once when an attached runtime starts; `onUpdate(dt)` runs per playing frame.
- `onFixedUpdate(dt)` runs before fixed physics steps; `onDestroy()` runs on teardown.
- `onLevelLoaded(name)` runs in destination scripts after startup, including initial Play.
- `onLevelFailedToLoad(name, reason)` runs in the still-active level on a rejected/failed load.
- `on(name, callback)` subscribes, `off(token)` unsubscribes, `emit(name, ...)` queues custom events.
- Custom event arguments are nil/boolean/number/string only, at most eight per event.
- Delivery is deferred; events emitted by listeners wait for the next pump. Failing listeners
  are removed and logged; runtime stop/reset removes subscriptions. 
- Custom event limits: 256 queued events, 128 listeners per runtime, 128-byte names and
  4096-byte strings. Check the boolean returned by `emit` and remove unneeded listeners.
- Level transition functions return acceptance and an optional error, not completion.
- Lifecycle failures stop the failing script. Errors retain source filename/line and traceback.
- Mouse callbacks target the nearest active Collider under the Game camera in the Game view.
  Enter/Exit track hover, Over runs every playing frame, Down targets a left press and
  Up targets the object that received Down, even if released outside it.
- Audio callbacks target scripts attached to the AudioSource owner. Finished means natural
  primary-clip completion, not pause, explicit stop, fade-out stop or one-shot completion.
- `waitForSeconds` and `wait(audio.onAudioFinished)` suspend lifecycle/object callbacks only.
  Do not use them at file top-level, in custom event listeners or onDestroy. Each callback
  has at most one pending invocation. Other callbacks keep running; Play pause or object
  deactivation freezes timers, and Stop cancels waits. Audio waits observe the next event,
  not one that happened before subscribing. A looping clip never naturally finishes.
- `self:setActive(false)` hides an object, disables its physics/audio and pauses attached
  script updates/events/waits. Parents propagate inactivity; another active script or the
  Inspector can reactivate it. State persists in project/level saves and undo history.
- `debug.error()` logs without throwing. Memory/instruction limits still apply.

The sandbox also exposes Lua 5.4 base, table, string, math and utf8 computation libraries.
It does not expose filesystem/process/module loading, coroutines, pcall/xpcall or the
standard introspection debug library. Those standard computation-library APIs are not
engine functions; this reference covers the engine-owned API.

## Callback and wait quick guide

| API | Where to define / call it | Requirement |
| --- | --- | --- |
| `onMouseDown/Up/Enter/Exit/Over()` | Global function in the target object’s attached script, or `object.onMouseDown = function() ... end` | Active Collider and Game camera |
| `onAudioStarted/Finished/Paused/Resumed/Stopped()` | Global function in the AudioSource owner’s attached script, or object callback | AudioSource on that owner |
| `waitForSeconds(seconds)` | Inside a running lifecycle/object callback | Finite 0–86400 seconds |
| `wait(audio.onAudioFinished)` | Inside a running lifecycle/object callback | Live AudioSource; next event only |

AudioSource event properties are opaque `AudioEvent` tokens. Define the callback as
`function onAudioFinished() ... end`; pass `audio.onAudioFinished` to `wait` without
parentheses. Do not assign a function to the AudioSource event property.

Waits are not supported at file top-level, in custom/level event listeners, or in
`onDestroy`. Play pause and object inactivity freeze timer progress. The engine allows
at most 128 pending callbacks per runtime and coalesces repeated invocations of the
same waiting callback. Shutdown/reset cancels continuations; variables survive a pause.

```lua
function onStart()
    local audio = self:getComponent(AudioSource)
    if not audio then return end
    waitForSeconds(5)
    audio:play()
    wait(audio.onAudioFinished)
    debug.log("Continue after natural playback completion")
end
```

For complete setup and troubleshooting, see [mouse and audio events](audio-input.md#gameobject-mouse-callbacks)
and [object activity and waits](scripting.md#object-activity-mouseaudio-callbacks-and-waits).

## Global functions and callbacks

### onStart

`onStart() - called when Execute succeeds`

```lua
function onStart()
    debug.log("Script started", self:getName())
end
```

### onUpdate

`onUpdate(deltaTime) - once per frame, seconds`

- `param deltaTime number Seconds since the previous frame (capped at 0.25).`

```lua
function onUpdate(deltaTime)
    self:translate(deltaTime, 0, 0)
end
```

### onFixedUpdate

`onFixedUpdate(deltaTime) - before each 1/60 s physics step`

- `param deltaTime number Fixed physics interval, 1/60 seconds.`

```lua
function onFixedUpdate(deltaTime)
    debug.log("Fixed step", deltaTime)
end
```

### onDestroy

`onDestroy() - object destruction or runtime shutdown/reset`

```lua
function onDestroy()
    debug.log("Script stopping")
end
```

### createGameObject

`createGameObject(name) -> GameObject`

- `param name string`
- `return GameObject`

```lua
local object = createGameObject("Player")
object:setPosition(0, 1, 0)
```

### destroyGameObject

`destroyGameObject(gameObject) - immediately destroy object and all descendants`

Immediately destroys the GameObject and all descendants, including inactive children. Attached scripts receive onDestroy once. To keep a child, detach it with child:setParent(nil) before deleting its parent. Editor deletion also removes the entire subtree and supports undo/redo.

- `param gameObject GameObject`

```lua
local object = createGameObject("Temporary")
destroyGameObject(object)
```

### getComponent

`getComponent(Type) / getComponent(gameObject, Type) / object.getComponent(Type) -> live component or nil`

Use getComponent(Type) for attached self, getComponent(object, Type) for an explicit owner, or object:getComponent(Type). Missing components return nil. The implicit form requires an attached live self.

- `param gameObject GameObject`
- `param componentType ComponentType`
- `overload fun(gameObject:GameObject,componentType:'Transform'):Transform|nil`
- `overload fun(gameObject:GameObject,componentType:'RigidBody'):RigidBody|nil`
- `overload fun(gameObject:GameObject,componentType:'Collider'|'BoxCollider'):Collider|nil`
- `overload fun(gameObject:GameObject,componentType:'MeshRenderer'):MeshRenderer|nil`
- `overload fun(gameObject:GameObject,componentType:'ObjectName'):ObjectName|nil`
- `return Transform|RigidBody|Collider|MeshRenderer|ObjectName|LuaScript|nil`
- `overload fun(componentType:ComponentType):any`

```lua
local transform = getComponent(self, Transform)
if transform then debug.log(transform.position.x) end
```

### addComponent

`addComponent(gameObject, componentType) -> live component`

- `param gameObject GameObject`
- `param componentType ComponentType`
- `overload fun(gameObject:GameObject,componentType:'Transform'):Transform`
- `overload fun(gameObject:GameObject,componentType:'RigidBody'):RigidBody`
- `overload fun(gameObject:GameObject,componentType:'Collider'|'BoxCollider'):Collider`
- `overload fun(gameObject:GameObject,componentType:'MeshRenderer'):MeshRenderer`
- `overload fun(gameObject:GameObject,componentType:'ObjectName'):ObjectName`
- `return Transform|RigidBody|Collider|MeshRenderer|ObjectName|LuaScript`

```lua
local collider = addComponent(self, Collider)
collider.radius = 0.5
```

### removeComponent

`removeComponent(gameObject, componentType)`

- `param gameObject GameObject`
- `param componentType ComponentType`

```lua
addComponent(self, Collider)
removeComponent(self, Collider)
```

### findGameObjectByName

`findGameObjectByName(name) -> GameObject or nil`

- `param name string`
- `return GameObject|nil`

```lua
local player = findGameObjectByName("Player")
if player then debug.log(player:getName()) end
```

### findGameObjectsByComponent

`findGameObjectsByComponent(componentType) -> GameObject[]`

- `param componentType ComponentType`
- `return GameObject[]`

```lua
for _, object in ipairs(findGameObjectsByComponent(Camera)) do
    debug.log(object:getName())
end
```

### setParent

`setParent(child, parentOrNil) - keep world pose; reject cycles`

- `param child GameObject`
- `param parent GameObject|nil`

```lua
local child = GameObject.new("Child")
setParent(child, self)
```

### getParent

`getParent(child) -> GameObject or nil`

- `param child GameObject`
- `return GameObject|nil`

```lua
local parent = getParent(self)
if parent then debug.log(parent:getName()) end
```

### createCamera

`createCamera(name) -> Camera GameObject`

- `param name string`
- `return GameObject`

```lua
local camera = createCamera("Player Camera")
setActiveCamera(camera)
```

### setActiveCamera

`setActiveCamera(cameraOrNil)`

- `param camera GameObject|nil`

```lua
local camera = Camera.new("Player Camera")
setActiveCamera(camera)
```

### getActiveCamera

`getActiveCamera() -> Camera GameObject or nil`

- `return GameObject|nil`

```lua
local camera = getActiveCamera()
if camera then debug.log(camera:getPosition().y) end
```

### setCameraPosition

`setCameraPosition(camera, {x,y,z})`

- `param camera GameObject`
- `param position Vec3`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
local result = setCameraPosition(camera, {0, 2, 6})
```

### setCameraRotation

`setCameraRotation(camera, Euler degrees {x,y,z} or quaternion {x,y,z,w})`

- `param camera GameObject`
- `param rotation Vec3|Quaternion Euler degrees or quaternion`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
local result = setCameraRotation(camera, {0, 180, 0})
```

### lookAt

`lookAt(camera, worldPositionOrGameObject) - one-shot aim`

- `param camera GameObject`
- `param target Vec3|GameObject`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
local result = lookAt(camera, {0, 0, 0})
```

### setFieldOfView

`setFieldOfView(camera, degrees)`

- `param camera GameObject`
- `param degrees number`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
local result = setFieldOfView(camera, 60)
```

### setProjectionMode

`setProjectionMode(camera, Perspective | Orthographic)`

- `param camera GameObject`
- `param mode 'Perspective'|'Orthographic'|'perspective'|'orthographic'`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
local result = setProjectionMode(camera, "Perspective")
```

### setNearClipPlane

`setNearClipPlane(camera, distance)`

- `param camera GameObject`
- `param distance number`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
local result = setNearClipPlane(camera, 0.1)
```

### setFarClipPlane

`setFarClipPlane(camera, distance)`

- `param camera GameObject`
- `param distance number`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
local result = setFarClipPlane(camera, 100)
```

### setViewport

`setViewport(camera, x,y,width,height) - normalized top-left viewport`

- `param camera GameObject`
- `param x number Normalized top-left origin`
- `param y number`
- `param width number`
- `param height number`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
local result = setViewport(camera, 0, 0, 1, 1)
```

### setCameraTarget

`setCameraTarget(camera, pointOrObjectOrNil) - continuous aim`

- `param camera GameObject`
- `param target Vec3|GameObject|nil`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
local result = setCameraTarget(camera, self)
```

### setFollowTarget

`setFollowTarget(camera, pointOrObjectOrNil, worldOffset)`

- `param camera GameObject`
- `param target Vec3|GameObject|nil`
- `param offset Vec3|nil World-space offset`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
local result = setFollowTarget(camera, self, {0, 2, 5})
```

### setCameraShake

`setCameraShake(camera, intensity, durationSeconds)`

- `param camera GameObject`
- `param intensity number World units`
- `param duration number Seconds`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
local result = setCameraShake(camera, 0.2, 0.5)
```

### worldToScreen

`worldToScreen(camera, worldPosition) -> normalized {x,y,zDepth}`

- `param camera GameObject`
- `param worldPosition Vec3`
- `return Vec3 Normalized top-left screen x/y, depth 0 near to 1 far`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
local result = worldToScreen(camera, {0, 0, 0})
```

### screenToWorld

`screenToWorld(camera, normalized {x,y,zDepth}) -> world position`

- `param camera GameObject`
- `param screenPosition Vec3 Normalized x/y and depth`
- `return Vec3`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
local result = screenToWorld(camera, {0.5, 0.5, 0.5})
```

### AudioClip

`AudioClip(projectRelativeAudioPath) -> AudioClip path`

- `param path string Project-relative WAV or MP3 asset`
- `return AudioClip`

```lua
local clip = AudioClip("Audio/example.wav")
debug.log(clip)
```

### createAudioSource

`createAudioSource(owner=self, clipPath=nil) -> AudioSource`

- `param owner? GameObject Defaults to attached self`
- `param clip? AudioClip`
- `return AudioSource`

```lua
local audio = createAudioSource(self, AudioClip("Audio/example.wav"))
audio:spatialBlend(1)
```

### isKeyPressed

`isKeyPressed(keyNameOrScancode) -> boolean`

- `param key string|integer SDL key name or scancode`
- `return boolean`

```lua
if isKeyPressed("Space") then debug.log("Space") end
```

### isKeyDown

`isKeyDown(keyNameOrScancode) -> boolean`

- `param key string|integer SDL key name or scancode`
- `return boolean`

```lua
if isKeyDown("Space") then debug.log("Space") end
```

### isKeyReleased

`isKeyReleased(keyNameOrScancode) -> boolean`

- `param key string|integer SDL key name or scancode`
- `return boolean`

```lua
if isKeyReleased("Space") then debug.log("Space") end
```

### isMouseButtonPressed

`isMouseButtonPressed(buttonNameOrNumber) -> boolean`

- `param button string|integer left/middle/right or SDL button number`
- `return boolean`

```lua
if isMouseButtonPressed("Left") then debug.log("Left mouse") end
```

### isMouseButtonDown

`isMouseButtonDown(buttonNameOrNumber) -> boolean`

- `param button string|integer left/middle/right or SDL button number`
- `return boolean`

```lua
if isMouseButtonDown("Left") then debug.log("Left mouse") end
```

### isMouseButtonReleased

`isMouseButtonReleased(buttonNameOrNumber) -> boolean`

- `param button string|integer left/middle/right or SDL button number`
- `return boolean`

```lua
if isMouseButtonReleased("Left") then debug.log("Left mouse") end
```

### getMousePosition

`getMousePosition() -> {x,y} window pixels`

- `return Vec2`

```lua
local value = getMousePosition()
debug.log(value.x, value.y)
```

### getMouseDelta

`getMouseDelta() -> {x,y} relative pixels this frame`

- `return Vec2`

```lua
local value = getMouseDelta()
debug.log(value.x, value.y)
```

### getMouseWheelDelta

`getMouseWheelDelta() -> {x,y} wheel units this frame`

- `return Vec2`

```lua
local value = getMouseWheelDelta()
debug.log(value.x, value.y)
```

### getInputAxis

`getInputAxis(negativeButton,positiveButton) -> -1 to 1`

- `param negativeButton string Project button name`
- `param positiveButton string Project button name`
- `return number`

```lua
local horizontal = getInputAxis("Left", "Right")
debug.log(horizontal)
```

### getInputVector

`getInputVector(leftButton,rightButton,downButton,upButton) -> normalized {x,y}`

- `param leftButton string`
- `param rightButton string`
- `param downButton string`
- `param upButton string`
- `return Vec2`

```lua
local movement = getInputVector("Left", "Right", "Down", "Up")
debug.log(movement.x, movement.y)
```

### isGamepadButtonPressed

`isGamepadButtonPressed(buttonNameOrNumber) -> boolean`

- `param button string|integer SDL button name or enum value`
- `return boolean`

```lua
if isGamepadButtonPressed("South") then debug.log("South button") end
```

### isGamepadButtonDown

`isGamepadButtonDown(buttonNameOrNumber) -> boolean`

- `param button string|integer SDL button name or enum value`
- `return boolean`

```lua
if isGamepadButtonDown("South") then debug.log("South button") end
```

### getGamepadAxis

`getGamepadAxis(axisNameOrNumber) -> -1 to 1`

- `param axis string|integer SDL axis name or enum value`
- `return number`

```lua
debug.log(getGamepadAxis("leftx"))
```

### getGamepadStick

`getGamepadStick(leftOrRight) -> {x,y}`

- `param stick? 'left'|'right'`
- `return Vec2`

```lua
local stick = getGamepadStick("Left")
debug.log(stick.x, stick.y)
```

### isButtonPressed

`isButtonPressed(projectButtonName) -> boolean`

- `param name string Defined in Project > Input Settings`
- `return boolean`

```lua
if isButtonPressed("Jump") then debug.log("Jump action") end
```

### isButtonReleased

`isButtonReleased(projectButtonName) -> boolean`

- `param name string Defined in Project > Input Settings`
- `return boolean`

```lua
if isButtonReleased("Jump") then debug.log("Jump action") end
```

### isButtonDown

`isButtonDown(projectButtonName) -> boolean`

- `param name string Defined in Project > Input Settings`
- `return boolean`

```lua
if isButtonDown("Jump") then debug.log("Jump action") end
```

### lockCursor

`lockCursor() -> boolean; relative motion, cursor hidden`

- `return boolean`

```lua
local result = lockCursor()
debug.log(result)
```

### unlockCursor

`unlockCursor() -> boolean`

- `return boolean`

```lua
local result = unlockCursor()
debug.log(result)
```

### hideCursor

`hideCursor() -> boolean`

- `return boolean`

```lua
local result = hideCursor()
debug.log(result)
```

### showCursor

`showCursor() -> boolean`

- `return boolean`

```lua
local result = showCursor()
debug.log(result)
```

### isCursorVisible

`isCursorVisible() -> boolean`

- `return boolean`

```lua
local result = isCursorVisible()
debug.log(result)
```

### isCursorLocked

`isCursorLocked() -> boolean`

- `return boolean`

```lua
local result = isCursorLocked()
debug.log(result)
```

### loadLevel

`loadLevel(name=nil) -> accepted, error; switch at next frame boundary`

- `param name? string`
- `return boolean accepted`
- `return string? error`

```lua
local accepted, reason = loadLevel("Main")
if not accepted then debug.warning(reason) end
```

### loadLevelAsync

`loadLevelAsync(name=nil) -> accepted, error; background file read`

- `param name? string`
- `return boolean accepted`
- `return string? error`

```lua
local accepted, reason = loadLevelAsync("Main")
if not accepted then debug.warning(reason) end
```

### unloadLevel

`unloadLevel(name=nil) -> accepted, error; unload current level`

- `param name? string`
- `return boolean accepted`
- `return string? error`

```lua
local accepted, reason = unloadLevel()
if not accepted then debug.warning(reason) end
```

### restartLevel

`restartLevel() -> accepted, error; reload current level from disk`

- `return boolean accepted`
- `return string? error`

```lua
local accepted, reason = restartLevel()
if not accepted then debug.warning(reason) end
```

### getCurrentLevel

`getCurrentLevel() -> string or nil`

- `return string? name`

```lua
local name = getCurrentLevel()
if name then debug.log("Current level", name) end
```

### setCurrentLevel

`setCurrentLevel(name) -> accepted, error; alias for loadLevel`

- `param name string`
- `return boolean accepted`
- `return string? error`

```lua
local accepted, reason = setCurrentLevel("Main")
if not accepted then debug.warning(reason) end
```

### nextLevel

`nextLevel() -> accepted, error; next registered level, no wrapping`

- `return boolean accepted`
- `return string? error`

```lua
local accepted, reason = nextLevel()
if not accepted then debug.warning(reason) end
```

### previousLevel

`previousLevel() -> accepted, error; previous registered level, no wrapping`

- `return boolean accepted`
- `return string? error`

```lua
local accepted, reason = previousLevel()
if not accepted then debug.warning(reason) end
```

### on

`on(eventName, callback) -> listener token; receives queued custom events`

- `param eventName string`
- `param callback function`
- `return integer token`

```lua
local token = on("scoreChanged", function(score)
    debug.log("Score", score)
end)
```

### off

`off(token) -> boolean; remove this script's listener`

- `param token integer`
- `return boolean removed`

```lua
local token = on("scoreChanged", function(score) debug.log(score) end)
local removed = off(token)
debug.log(removed)
```

### emit

`emit(eventName, ...) -> boolean; queue up to eight primitive arguments`

- `param eventName string`
- `return boolean accepted`

```lua
on("scoreChanged", function(score, name) debug.log(name, score) end)
local accepted = emit("scoreChanged", 10, "Player")
debug.log(accepted)
```

### onLevelLoaded

`onLevelLoaded(name) - destination callback after its scripts start`

- `param name string`

```lua
function onLevelLoaded(name)
    debug.log("Loaded level", name)
end
```

### onLevelFailedToLoad

`onLevelFailedToLoad(name, error) - current level stays active`

- `param name string`
- `param error string`

```lua
function onLevelFailedToLoad(name, reason)
    debug.warning("Failed level", name, reason)
end
```

### fadeSkybox

`fadeSkybox(destination, speed) or fadeSkybox(source, destination, speed) - activate destination after smooth crossfade`

- `param destination GameObject|Skybox`
- `param speed number`
- `overload fun(source:GameObject|Skybox,destination:GameObject|Skybox,speed:number)`

```lua
local skybox1 = GameObject.new("Skybox1")
local skybox2 = GameObject.new("Skybox2")
skybox1:addComponent(Skybox):setColor(0.8, 0.9, 1)
local evening = skybox2:addComponent(Skybox)
evening:setColor(0.3, 0.1, 0.05)
evening:setEnabled(false)
fadeSkybox(skybox1, skybox2, 0.5)
```

### onMouseDown

`onMouseDown() - targeted GameObject callback`

Runs on the nearest active Collider hit by a left-button press in Game. Requires an active Game camera; this is object picking, not a global input hook.

```lua
function onMouseDown()
    debug.log("onMouseDown", self:getName())
end
```

### onMouseUp

`onMouseUp() - targeted GameObject callback`

Runs on the object that received the matching Down, even if the pointer has moved off it. No callback is delivered after the object is destroyed or inactive.

```lua
function onMouseUp()
    debug.log("onMouseUp", self:getName())
end
```

### onMouseEnter

`onMouseEnter() - targeted GameObject callback`

Runs once when the hovered Collider changes to this object. Picking uses the active camera viewport; a locked cursor uses its center.

```lua
function onMouseEnter()
    debug.log("onMouseEnter", self:getName())
end
```

### onMouseExit

`onMouseExit() - targeted GameObject callback`

Runs when hover leaves this active object, including moving out of Game. Deactivated objects do not receive callbacks.

```lua
function onMouseExit()
    debug.log("onMouseExit", self:getName())
end
```

### onMouseOver

`onMouseOver() - targeted GameObject callback`

Runs every playing frame while this object is the nearest active Collider under the Game pointer. It does not require a pressed button.

```lua
function onMouseOver()
    debug.log("onMouseOver", self:getName())
end
```

### onAudioStarted

`onAudioStarted() - targeted GameObject callback`

Runs on scripts attached to the AudioSource owner after play/fadeIn starts or restarts the primary clip. One-shots do not produce this primary-clip event.

```lua
function onAudioStarted()
    debug.log("onAudioStarted", self:getName())
end
```

### onAudioFinished

`onAudioFinished() - targeted GameObject callback`

Runs on the AudioSource owner when its non-looping primary clip reaches its natural end. Explicit stop, fade-out stop, decode failure and one-shot completion do not count.

```lua
function onAudioFinished()
    debug.log("onAudioFinished", self:getName())
end
```

### onAudioPaused

`onAudioPaused() - targeted GameObject callback`

Runs on the AudioSource owner when pause changes Playing to Paused. Calling pause again while already paused does not emit another event.

```lua
function onAudioPaused()
    debug.log("onAudioPaused", self:getName())
end
```

### onAudioResumed

`onAudioResumed() - targeted GameObject callback`

Runs on the AudioSource owner when resume changes Paused to Playing. Resuming continues from the existing playback cursor.

```lua
function onAudioResumed()
    debug.log("onAudioResumed", self:getName())
end
```

### onAudioStopped

`onAudioStopped() - targeted GameObject callback`

Runs on the AudioSource owner when a playing/paused primary source is explicitly stopped, including fade-out stop or playback failure. It is distinct from natural Finished.

```lua
function onAudioStopped()
    debug.log("onAudioStopped", self:getName())
end
```

### waitForSeconds

`waitForSeconds(seconds) - suspend this callback, not the engine`

Suspends the current lifecycle/object callback for finite playing-time seconds in [0, 86400]. Zero resumes no earlier than the next update. Other callbacks continue; this does not block the engine.

- `param seconds number Finite seconds from 0 to 86400; zero resumes on next update`

```lua
function onStart()
    debug.log("Before wait")
    waitForSeconds(5)
    debug.log("Five playing seconds later")
end
```

### wait

`wait(audioSource.onAudioFinished) - resume on the next audio event`

Takes an opaque AudioEvent token, not a function name string or a call to the event. Waits for its next occurrence after subscribing. Stop/reset cancels waits; removing the AudioSource cancels its continuation.

- `param event AudioEvent`

```lua
function onStart()
    local audio = createAudioSource(self, AudioClip("Audio/example.wav"))
    audio:play()
    wait(audio.onAudioFinished)
    debug.log("Audio finished")
end
```

### spawn

`spawn(prefabPathOrGameObject, position, rotation) -> new GameObject root`

Creates a prefab asset instance or copies a GameObject subtree and returns its new root. Prefab paths are project-relative .prefab strings, not component tokens. Position is world-space; rotation accepts Euler degrees or a quaternion. Internal object references are remapped; external references are cleared. See the Prefab workflow guide.

- `param source string|GameObject Project-relative .prefab path or live object`
- `param position Vec3`
- `param rotation Vec3|Quaternion Euler degrees or quaternion`
- `return GameObject`

```lua
local copy = spawn(self, {x=0, y=2, z=0}, {x=0, y=90, z=0})
debug.log(copy:getName())
```

### destroy

`destroy(object, delay=0) / object:destroy()`

Destroys the root and its descendants now (omitted/zero delay) or after nonnegative playing seconds. Pause freezes delayed destruction. A later schedule replaces the delay; pending timers are not serialized.

- `param object GameObject`
- `param delay? number Nonnegative, defaults to zero`

```lua
local copy = spawn(self, {x=0, y=2, z=0}, {x=0, y=0, z=0})
destroy(copy, 5)
```

### onTriggerEnter

`onTriggerEnter(collider) - non-solid overlap begins`

- `param collider Collider|nil Other collider; nil if destroyed before delivery`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
local result = onTriggerEnter(camera, function onTriggerEnter(collider)
    if collider then debug.log("Entered", collider.gameObject:getName()) end
end)
```

### onTriggerStay

`onTriggerStay(collider) - non-solid overlap persists`

- `param collider Collider|nil`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
local result = onTriggerStay(camera, function onTriggerStay(collider)
    if collider then debug.log("Overlapping", collider.gameObject.layer) end
end)
```

### onTriggerExit

`onTriggerExit(collider) - overlap ends; collider may be nil if destroyed`

- `param collider Collider|nil`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
local result = onTriggerExit(camera, function onTriggerExit(collider)
    debug.log(collider and collider.gameObject:getName() or "Destroyed collider", "left")
end)
```

### onCollisionEnter

`onCollisionEnter(collider) - solid Rigidbody contact begins; collider exposes gameObject and transform`

- `param collider Collider|nil Other solid collider; exposes gameObject and transform`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
local result = onCollisionEnter(camera, function onCollisionEnter(collider)
    if collider then debug.log("Hit", collider.gameObject:getName()) end
end)
```

### onCollisionStay

`onCollisionStay(collider) - solid contact persists during a physics step`

- `param collider Collider|nil`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
local result = onCollisionStay(camera, function onCollisionStay(collider)
    if collider then debug.log("Touching", collider.gameObject:getName()) end
end)
```

### onCollisionExit

`onCollisionExit(collider) - solid contact ends; other collider may be nil if removed`

- `param collider Collider|nil`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
local result = onCollisionExit(camera, function onCollisionExit(collider)
    debug.log(collider and collider.gameObject:getName() or "Removed collider", "separated")
end)
```

### print

`print(...) - log tab-separated values`

```lua
print("Player position", self:getPosition().x)
```

## GameObject

### GameObject.getComponent

`getComponent(componentType:ComponentType):Transform|RigidBody|Collider|MeshRenderer|ObjectName|LuaScript|nil`

- `getComponent(Type) / getComponent(gameObject, Type) / object.getComponent(Type) -> live component or nil`

```lua
local object = self
object:getComponent(Transform)
```

### GameObject.addComponent

`addComponent(componentType:ComponentType):Transform|RigidBody|Collider|MeshRenderer|ObjectName|LuaScript`

- `addComponent(gameObject, componentType) -> live component`

```lua
local object = self
object:addComponent(Collider)
```

### GameObject.removeComponent

`removeComponent(componentType:ComponentType)`

- `removeComponent(gameObject, componentType)`

```lua
local object = self
object:removeComponent(Collider)
```

### GameObject.onStart

`onStart()|nil`

- `onStart() - called when Execute succeeds`

```lua
self.onStart = function()
    debug.log("Object callback: onStart")
end
```

### GameObject.onUpdate

`onUpdate(deltaTime:number)|nil`

- `onUpdate(deltaTime) - once per frame, seconds`

```lua
self.onUpdate = function(deltaTime)
    debug.log("Object callback: onUpdate")
end
```

### GameObject.onFixedUpdate

`onFixedUpdate(deltaTime:number)|nil`

- `onFixedUpdate(deltaTime) - before each 1/60 s physics step`

```lua
self.onFixedUpdate = function(deltaTime)
    debug.log("Object callback: onFixedUpdate")
end
```

### GameObject.onDestroy

`onDestroy()|nil`

- `onDestroy() - object destruction or runtime shutdown/reset`

```lua
self.onDestroy = function()
    debug.log("Object callback: onDestroy")
end
```

### GameObject.onMouseDown

`onMouseDown()|nil`

- `onMouseDown() - targeted GameObject callback`

```lua
self.onMouseDown = function()
    debug.log("Object callback: onMouseDown")
end
```

### GameObject.onMouseUp

`onMouseUp()|nil`

- `onMouseUp() - targeted GameObject callback`

```lua
self.onMouseUp = function()
    debug.log("Object callback: onMouseUp")
end
```

### GameObject.onMouseEnter

`onMouseEnter()|nil`

- `onMouseEnter() - targeted GameObject callback`

```lua
self.onMouseEnter = function()
    debug.log("Object callback: onMouseEnter")
end
```

### GameObject.onMouseExit

`onMouseExit()|nil`

- `onMouseExit() - targeted GameObject callback`

```lua
self.onMouseExit = function()
    debug.log("Object callback: onMouseExit")
end
```

### GameObject.onMouseOver

`onMouseOver()|nil`

- `onMouseOver() - targeted GameObject callback`

```lua
self.onMouseOver = function()
    debug.log("Object callback: onMouseOver")
end
```

### GameObject.onAudioStarted

`onAudioStarted()|nil`

- `onAudioStarted() - targeted GameObject callback`

```lua
self.onAudioStarted = function()
    debug.log("Object callback: onAudioStarted")
end
```

### GameObject.onAudioFinished

`onAudioFinished()|nil`

- `onAudioFinished() - targeted GameObject callback`

```lua
self.onAudioFinished = function()
    debug.log("Object callback: onAudioFinished")
end
```

### GameObject.onAudioPaused

`onAudioPaused()|nil`

- `onAudioPaused() - targeted GameObject callback`

```lua
self.onAudioPaused = function()
    debug.log("Object callback: onAudioPaused")
end
```

### GameObject.onAudioResumed

`onAudioResumed()|nil`

- `onAudioResumed() - targeted GameObject callback`

```lua
self.onAudioResumed = function()
    debug.log("Object callback: onAudioResumed")
end
```

### GameObject.onAudioStopped

`onAudioStopped()|nil`

- `onAudioStopped() - targeted GameObject callback`

```lua
self.onAudioStopped = function()
    debug.log("Object callback: onAudioStopped")
end
```

### GameObject.setActive

`setActive(self:GameObject,enabled?:boolean) Set local activity (default true); inherited by children`

Sets local activity; omitted enabled defaults to true. Inactive parents make descendants effectively inactive without changing their local flags. Inactive objects are hidden, excluded from physics, silent, and their attached script updates/events/waits pause. Another active script or Inspector can reactivate them.

- `object:setActive(enabled=true) - activity inherited by children; Camera also selects active camera`

```lua
local object = self
object:setActive(true)
```

### GameObject.isActive

`isActive(self:GameObject):boolean Effective activity including parents`

Returns effective activity, including all parents. It can be false even if this object is locally enabled. Destroyed handles raise a script error rather than returning false.

- `object:isActive() -> effective activity including parents`

```lua
local object = self
object:isActive()
```

### GameObject.setPosition

`setPosition(self:GameObject,x:number|Vec3,y?:number,z?:number)`

- `object:setPosition(x,y,z) or object:setPosition({x,y,z})`

```lua
local object = self
object:setPosition(1, 2, 3)
```

### GameObject.getPosition

`getPosition(self:GameObject):Vec3`

- `object:getPosition() -> position copy`

```lua
local object = self
object:getPosition()
```

### GameObject.setRotation

`setRotation(self:GameObject,x:number|Vec3|Quaternion,y?:number,z?:number,w?:number)`

- `object:setRotation(x,y,z) degrees or quaternion/table`

```lua
local object = self
object:setRotation(0, 45, 0)
```

### GameObject.getRotation

`getRotation(self:GameObject):Quaternion`

- `object:getRotation() -> quaternion copy`

```lua
local object = self
object:getRotation()
```

### GameObject.setScale

`setScale(self:GameObject,x:number|Vec3,y?:number,z?:number)`

- `object:setScale(x,y,z) or vector`

```lua
local object = self
object:setScale(1, 1, 1)
```

### GameObject.getScale

`getScale(self:GameObject):Vec3`

- `object:getScale() -> scale copy`

```lua
local object = self
object:getScale()
```

### GameObject.translate

`translate(self:GameObject,x:number|Vec3,y?:number,z?:number)`

- `object:translate(x,y,z) or vector`

```lua
local object = self
object:translate(0, 0, 1)
```

### GameObject.lookAt

`lookAt(self:GameObject,x:number|Vec3|GameObject,y?:number,z?:number)`

- `lookAt(camera, worldPositionOrGameObject) - one-shot aim`

```lua
local object = self
object:lookAt(0, 0, -5)
```

### GameObject.setParent

`setParent(self:GameObject,parent:GameObject|nil)`

- `setParent(child, parentOrNil) - keep world pose; reject cycles`

```lua
local object = self
object:setParent(nil)
```

### GameObject.getParent

`getParent(self:GameObject):GameObject|nil`

- `getParent(child) -> GameObject or nil`

```lua
local object = self
object:getParent()
```

### GameObject.setName

`setName(self:GameObject,name:string)`

- `object:setName(name)`

```lua
local object = self
object:setName("Player")
```

### GameObject.getName

`getName(self:GameObject):string`

- `object:getName() -> string`

```lua
local object = self
object:getName()
```

### GameObject.destroy

`destroy(self:GameObject)`

- `destroy(object, delay=0) / object:destroy()`

```lua
local object = self
object:destroy()
```

### GameObject.getForward

`getForward(self:GameObject):Vec3 World-space local -Z axis`

- `object:getForward() -> world-space normalized local -Z direction`

```lua
local object = self
object:getForward()
```

### GameObject.getBackward

`getBackward(self:GameObject):Vec3 World-space local +Z axis`

- `object:getBackward() -> world-space normalized local +Z direction`

```lua
local object = self
object:getBackward()
```

### GameObject.getRight

`getRight(self:GameObject):Vec3 World-space local +X axis`

- `object:getRight() -> world-space normalized local +X direction`

```lua
local object = self
object:getRight()
```

### GameObject.getLeft

`getLeft(self:GameObject):Vec3 World-space local -X axis`

- `object:getLeft() -> world-space normalized local -X direction`

```lua
local object = self
object:getLeft()
```

### GameObject.getUp

`getUp(self:GameObject):Vec3 World-space local +Y axis`

- `object:getUp() -> world-space normalized local +Y direction`

```lua
local object = self
object:getUp()
```

### GameObject.getDown

`getDown(self:GameObject):Vec3 World-space local -Y axis`

- `object:getDown() -> world-space normalized local -Y direction`

```lua
local object = self
object:getDown()
```

### GameObject.onTriggerEnter

`onTriggerEnter(collider:Collider|nil)|nil`

- `onTriggerEnter(collider) - non-solid overlap begins`

```lua
self.onTriggerEnter = function(collider)
    if collider then debug.log(collider.gameObject:getName()) end
end
```

### GameObject.onTriggerStay

`onTriggerStay(collider:Collider|nil)|nil`

- `onTriggerStay(collider) - non-solid overlap persists`

```lua
self.onTriggerStay = function(collider)
    if collider then debug.log(collider.gameObject:getName()) end
end
```

### GameObject.onTriggerExit

`onTriggerExit(collider:Collider|nil)|nil`

- `onTriggerExit(collider) - overlap ends; collider may be nil if destroyed`

```lua
self.onTriggerExit = function(collider)
    if collider then debug.log(collider.gameObject:getName()) end
end
```

### GameObject.onCollisionEnter

`onCollisionEnter(collider:Collider|nil)|nil`

- `onCollisionEnter(collider) - solid Rigidbody contact begins; collider exposes gameObject and transform`

```lua
self.onCollisionEnter = function(collider)
    if collider then debug.log(collider.gameObject:getName()) end
end
```

### GameObject.onCollisionStay

`onCollisionStay(collider:Collider|nil)|nil`

- `onCollisionStay(collider) - solid contact persists during a physics step`

```lua
self.onCollisionStay = function(collider)
    if collider then debug.log(collider.gameObject:getName()) end
end
```

### GameObject.onCollisionExit

`onCollisionExit(collider:Collider|nil)|nil`

- `onCollisionExit(collider) - solid contact ends; other collider may be nil if removed`

```lua
self.onCollisionExit = function(collider)
    if collider then debug.log(collider.gameObject:getName()) end
end
```

## MeshRenderer

### MeshRenderer.setMaterial

`setMaterial(self:MeshRenderer,path:string) Assign an asset, reset instance overrides`

- `mesh:setMaterial(path) - assign .mat asset, reset overrides`

```lua
local component = self:getComponent(MeshRenderer) or self:addComponent(MeshRenderer)
component:setMaterial("Materials/example.mat")
```

### MeshRenderer.setMaterialColor

`setMaterialColor(self:MeshRenderer,r:number,g:number,b:number)`

- `mesh:setMaterialColor(r,g,b) - instance color override`

```lua
local component = self:getComponent(MeshRenderer) or self:addComponent(MeshRenderer)
component:setMaterialColor(1, 0.6, 0.3)
```

### MeshRenderer.setMaterialTexture

`setMaterialTexture(self:MeshRenderer,texture:Texture|nil)`

- `mesh:setMaterialTexture(Texture) - instance albedo override; nil clears`

```lua
local component = self:getComponent(MeshRenderer) or self:addComponent(MeshRenderer)
component:setMaterialTexture("Textures/albedo.png")
```

### MeshRenderer.setMaterialNormalMap

`setMaterialNormalMap(self:MeshRenderer,texture:Texture|nil)`

- `mesh:setMaterialNormalMap(Texture) - instance normal map override; nil clears`

```lua
local component = self:getComponent(MeshRenderer) or self:addComponent(MeshRenderer)
component:setMaterialNormalMap("Textures/normal.png")
```

### MeshRenderer.setMaterialShader

`setMaterialShader(self:MeshRenderer,shader:string) Diffuse, Diffuse Bumped or Diffuse Transparent`

- `mesh:setMaterialShader('Diffuse'|'Diffuse Bumped'|'Diffuse Transparent')`

```lua
local component = self:getComponent(MeshRenderer) or self:addComponent(MeshRenderer)
component:setMaterialShader("Diffuse Bumped")
```

### MeshRenderer.setMaterialNormalStrength

`setMaterialNormalStrength(self:MeshRenderer,value:number)`

- `mesh:setMaterialNormalStrength(value) - 0..4`

```lua
local component = self:getComponent(MeshRenderer) or self:addComponent(MeshRenderer)
component:setMaterialNormalStrength(1)
```

### MeshRenderer.setMaterialTiling

`setMaterialTiling(self:MeshRenderer,x:number,y:number)`

- `mesh:setMaterialTiling(x,y) - nonzero UV repeat scale`

```lua
local component = self:getComponent(MeshRenderer) or self:addComponent(MeshRenderer)
component:setMaterialTiling(2, 2)
```

### MeshRenderer.setMaterialSpecular

`setMaterialSpecular(self:MeshRenderer,value:number)`

- `mesh:setMaterialSpecular(value) - 0..1`

```lua
local component = self:getComponent(MeshRenderer) or self:addComponent(MeshRenderer)
component:setMaterialSpecular(0.5)
```

### MeshRenderer.resetMaterialOverrides

`resetMaterialOverrides(self:MeshRenderer)`

- `mesh:resetMaterialOverrides() - restore shared asset properties`

```lua
local component = self:getComponent(MeshRenderer) or self:addComponent(MeshRenderer)
component:resetMaterialOverrides()
```

## GameObjectConstructor

### GameObjectConstructor.new

`new(name:string):GameObject`

- `GameObject.new(name) / Camera.new(name)`

```lua
local object = GameObject.new("Player")
```

### GameObjectConstructor.find

`find(name:string):GameObject|nil`

- `GameObject.find(name) -> GameObject or nil`

```lua
local object = GameObject.find("Player")
if object then debug.log(object:getName()) end
```

## CameraObject

### CameraObject.setActive

`setActive(self:CameraObject,enabled?:boolean)`

- `object:setActive(enabled=true) - activity inherited by children; Camera also selects active camera`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
camera:setActive(true)
```

### CameraObject.setFieldOfView

`setFieldOfView(self:CameraObject,degrees:number)`

- `setFieldOfView(camera, degrees)`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
camera:setFieldOfView(60)
```

### CameraObject.setProjectionMode

`setProjectionMode(self:CameraObject,mode:string)`

- `setProjectionMode(camera, Perspective | Orthographic)`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
camera:setProjectionMode("Perspective")
```

### CameraObject.setNearClipPlane

`setNearClipPlane(self:CameraObject,distance:number)`

- `setNearClipPlane(camera, distance)`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
camera:setNearClipPlane(0.1)
```

### CameraObject.setFarClipPlane

`setFarClipPlane(self:CameraObject,distance:number)`

- `setFarClipPlane(camera, distance)`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
camera:setFarClipPlane(100)
```

### CameraObject.setViewport

`setViewport(self:CameraObject,x:number,y:number,width:number,height:number)`

- `setViewport(camera, x,y,width,height) - normalized top-left viewport`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
camera:setViewport(0, 0, 1, 1)
```

### CameraObject.setTarget

`setTarget(self:CameraObject,target:GameObject|Vec3|number|nil,y?:number,z?:number)`

- `camera:setTarget(objectOrPositionOrNil)`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
camera:setTarget(nil)
```

### CameraObject.setFollowTarget

`setFollowTarget(self:CameraObject,target:GameObject|Vec3|nil,offset:Vec3|number,y?:number,z?:number)`

- `setFollowTarget(camera, pointOrObjectOrNil, worldOffset)`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
camera:setFollowTarget(self, {0, 2, 5})
```

### CameraObject.setShake

`setShake(self:CameraObject,intensity:number,duration:number)`

- `camera:setShake(intensity,seconds)`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
camera:setShake(0.2, 0.5)
```

### CameraObject.setCameraShake

`setCameraShake(self:CameraObject,intensity:number,duration:number) Alias of setShake`

- `setCameraShake(camera, intensity, durationSeconds)`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
camera:setCameraShake(0.2, 0.5)
```

### CameraObject.setCameraTarget

`setCameraTarget(self:CameraObject,target:GameObject|Vec3|nil) Alias of setTarget`

- `setCameraTarget(camera, pointOrObjectOrNil) - continuous aim`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
camera:setCameraTarget(nil)
```

### CameraObject.worldToScreen

`worldToScreen(self:CameraObject,x:Vec3|number,y?:number,z?:number):Vec3`

- `worldToScreen(camera, worldPosition) -> normalized {x,y,zDepth}`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
camera:worldToScreen({0, 0, 0})
```

### CameraObject.screenToWorld

`screenToWorld(self:CameraObject,x:Vec3|number,y?:number,z?:number):Vec3`

- `screenToWorld(camera, normalized {x,y,zDepth}) -> world position`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
camera:screenToWorld({0.5, 0.5, 0.5})
```

## CameraConstructor

### CameraConstructor.new

`new(name:string):CameraObject`

- `GameObject.new(name) / Camera.new(name)`

```lua
local camera = Camera.new("Player Camera")
```

### CameraConstructor.getActive

`getActive():CameraObject|nil`

- `Camera.getActive() -> Camera GameObject or nil`

```lua
local camera = Camera.getActive()
if camera then debug.log(camera:getName()) end
```

## AudioSource

### AudioSource.play

`play(self:AudioSource)`

- `audio:play() - restart assigned clip`

```lua
local component = self:getComponent(AudioSource) or self:addComponent(AudioSource)
component.clip = AudioClip("Audio/example.wav")
component:play()
```

### AudioSource.stop

`stop(self:AudioSource)`

- `audio:stop() - stop and rewind`

```lua
local component = self:getComponent(AudioSource) or self:addComponent(AudioSource)
component.clip = AudioClip("Audio/example.wav")
component:stop()
```

### AudioSource.pause

`pause(self:AudioSource)`

- `audio:pause()`

```lua
local component = self:getComponent(AudioSource) or self:addComponent(AudioSource)
component.clip = AudioClip("Audio/example.wav")
component:pause()
```

### AudioSource.resume

`resume(self:AudioSource)`

- `audio:resume()`

```lua
local component = self:getComponent(AudioSource) or self:addComponent(AudioSource)
component.clip = AudioClip("Audio/example.wav")
component:resume()
```

### AudioSource.setPitch

`setPitch(self:AudioSource,pitch:number):number`

- `audio:setPitch(pitch) -> clamped 0.1 to 4`

```lua
local component = self:getComponent(AudioSource) or self:addComponent(AudioSource)
component.clip = AudioClip("Audio/example.wav")
component:setPitch(1.2)
```

### AudioSource.fadeIn

`fadeIn(self:AudioSource,seconds:number)`

- `audio:fadeIn(seconds) - restart and fade from silence`

```lua
local component = self:getComponent(AudioSource) or self:addComponent(AudioSource)
component.clip = AudioClip("Audio/example.wav")
component:fadeIn(1)
```

### AudioSource.fadeOut

`fadeOut(self:AudioSource,seconds:number)`

- `audio:fadeOut(seconds) - fade then stop`

```lua
local component = self:getComponent(AudioSource) or self:addComponent(AudioSource)
component.clip = AudioClip("Audio/example.wav")
component:fadeOut(1)
```

### AudioSource.setVolume

`setVolume(self:AudioSource,volume:number):number`

- `audio:setVolume(volume) -> clamped 0 to 1`

```lua
local component = self:getComponent(AudioSource) or self:addComponent(AudioSource)
component.clip = AudioClip("Audio/example.wav")
component:setVolume(0.75)
```

### AudioSource.mute

`mute(self:AudioSource)`

- `audio:mute()`

```lua
local component = self:getComponent(AudioSource) or self:addComponent(AudioSource)
component.clip = AudioClip("Audio/example.wav")
component:mute()
```

### AudioSource.unmute

`unmute(self:AudioSource)`

- `audio:unmute()`

```lua
local component = self:getComponent(AudioSource) or self:addComponent(AudioSource)
component.clip = AudioClip("Audio/example.wav")
component:unmute()
```

### AudioSource.setLooping

`setLooping(self:AudioSource,enabled:boolean):boolean`

- `audio:setLooping(enabled) -> boolean`

```lua
local component = self:getComponent(AudioSource) or self:addComponent(AudioSource)
component.clip = AudioClip("Audio/example.wav")
component:setLooping(true)
```

### AudioSource.setPan

`setPan(self:AudioSource,pan:number):number`

- `audio:setPan(pan) -> clamped -1 left to 1 right`

```lua
local component = self:getComponent(AudioSource) or self:addComponent(AudioSource)
component.clip = AudioClip("Audio/example.wav")
component:setPan(0)
```

### AudioSource.setDistance

`setDistance(self:AudioSource,distance:number):number Maximum audible 3D range`

- `audio:setDistance(distance) -> maximum audible distance (3D)`

```lua
local component = self:getComponent(AudioSource) or self:addComponent(AudioSource)
component.clip = AudioClip("Audio/example.wav")
component:setDistance(25)
```

### AudioSource.spatialBlend

`spatialBlend(self:AudioSource,value:number):number 0 = 2D, 1 = 3D`

- `audio:spatialBlend(value) -> 0 (2D) to 1 (3D)`

```lua
local component = self:getComponent(AudioSource) or self:addComponent(AudioSource)
component.clip = AudioClip("Audio/example.wav")
component:spatialBlend(1)
```

### AudioSource.setSpatialBlend

`setSpatialBlend(self:AudioSource,value:number):number`

- `audio:setSpatialBlend(value) -> alias for spatialBlend`

```lua
local component = self:getComponent(AudioSource) or self:addComponent(AudioSource)
component.clip = AudioClip("Audio/example.wav")
component:setSpatialBlend(1)
```

### AudioSource.isPlaying

`isPlaying(self:AudioSource):boolean`

- `audio:isPlaying() -> boolean`

```lua
local component = self:getComponent(AudioSource) or self:addComponent(AudioSource)
component.clip = AudioClip("Audio/example.wav")
component:isPlaying()
```

### AudioSource.playOneShot

`playOneShot(self:AudioSource,clip:AudioClip)`

- `audio:playOneShot(AudioClip) - overlapping nonlooping clip`

```lua
local component = self:getComponent(AudioSource) or self:addComponent(AudioSource)
component.clip = AudioClip("Audio/example.wav")
component:playOneShot(AudioClip("Audio/example.wav"))
```

## AudioListener

### AudioListener.pause

`pause(self:AudioListener) Disable reception without stopping AudioSources`

- `audio:pause()`

```lua
local component = self:getComponent(AudioListener) or self:addComponent(AudioListener)
component:pause()
```

### AudioListener.start

`start(self:AudioListener) Enable reception again`

```lua
local component = self:getComponent(AudioListener) or self:addComponent(AudioListener)
component:start()
```

## LegacyDebug

### LegacyDebug.log

`log(...:any)`

- `debug.log(...) - write a normal message`

```lua
debug.log("Example message", self:getName())
```

### LegacyDebug.error

`error(...:any)`

- `debug.error(...) - write an error without throwing`

```lua
debug.error("Example message", self:getName())
```

### LegacyDebug.warning

`warning(...:any)`

- `debug.warning(...) - write a warning`

```lua
debug.warning("Example message", self:getName())
```

## DirectionalLight

### DirectionalLight.setColor

`setColor(self:DirectionalLight|PointLight|SpotLight,r:number,g:number,b:number)`

```lua
local component = self:getComponent(DirectionalLight) or self:addComponent(DirectionalLight)
component:setColor(1, 0.8, 0.6)
```

### DirectionalLight.setIntensity

`setIntensity(self:DirectionalLight|PointLight|SpotLight,value:number)`

```lua
local component = self:getComponent(DirectionalLight) or self:addComponent(DirectionalLight)
component:setIntensity(2)
```

### DirectionalLight.setRange

`setRange(self:DirectionalLight|PointLight|SpotLight,value:number)`

```lua
local component = self:getComponent(DirectionalLight) or self:addComponent(DirectionalLight)
component:setRange(20)
```

### DirectionalLight.setEnabled

`setEnabled(self:DirectionalLight|PointLight|SpotLight,enabled:boolean)`

```lua
local component = self:getComponent(DirectionalLight) or self:addComponent(DirectionalLight)
component:setEnabled(true)
```

### DirectionalLight.setShadows

`setShadows(self:DirectionalLight|PointLight|SpotLight,enabled:boolean)`

```lua
local component = self:getComponent(DirectionalLight) or self:addComponent(DirectionalLight)
component:setShadows(true)
```

### DirectionalLight.setShadowQuality

`setShadowQuality(self:DirectionalLight|PointLight|SpotLight,quality:integer)`

```lua
local component = self:getComponent(DirectionalLight) or self:addComponent(DirectionalLight)
component:setShadowQuality(2)
```

### DirectionalLight.setSpotAngles

`setSpotAngles(self:DirectionalLight|PointLight|SpotLight,inner:number,outer:number)`

```lua
local component = self:getComponent(DirectionalLight) or self:addComponent(DirectionalLight)
component:setSpotAngles(15, 35)
```

## PointLight

### PointLight.setColor

`setColor(self:DirectionalLight|PointLight|SpotLight,r:number,g:number,b:number)`

```lua
local component = self:getComponent(PointLight) or self:addComponent(PointLight)
component:setColor(1, 0.8, 0.6)
```

### PointLight.setIntensity

`setIntensity(self:DirectionalLight|PointLight|SpotLight,value:number)`

```lua
local component = self:getComponent(PointLight) or self:addComponent(PointLight)
component:setIntensity(2)
```

### PointLight.setRange

`setRange(self:DirectionalLight|PointLight|SpotLight,value:number)`

```lua
local component = self:getComponent(PointLight) or self:addComponent(PointLight)
component:setRange(20)
```

### PointLight.setEnabled

`setEnabled(self:DirectionalLight|PointLight|SpotLight,enabled:boolean)`

```lua
local component = self:getComponent(PointLight) or self:addComponent(PointLight)
component:setEnabled(true)
```

### PointLight.setShadows

`setShadows(self:DirectionalLight|PointLight|SpotLight,enabled:boolean)`

```lua
local component = self:getComponent(PointLight) or self:addComponent(PointLight)
component:setShadows(true)
```

### PointLight.setShadowQuality

`setShadowQuality(self:DirectionalLight|PointLight|SpotLight,quality:integer)`

```lua
local component = self:getComponent(PointLight) or self:addComponent(PointLight)
component:setShadowQuality(2)
```

### PointLight.setSpotAngles

`setSpotAngles(self:DirectionalLight|PointLight|SpotLight,inner:number,outer:number)`

```lua
local component = self:getComponent(PointLight) or self:addComponent(PointLight)
component:setSpotAngles(15, 35)
```

## SpotLight

### SpotLight.setColor

`setColor(self:DirectionalLight|PointLight|SpotLight,r:number,g:number,b:number)`

```lua
local component = self:getComponent(SpotLight) or self:addComponent(SpotLight)
component:setColor(1, 0.8, 0.6)
```

### SpotLight.setIntensity

`setIntensity(self:DirectionalLight|PointLight|SpotLight,value:number)`

```lua
local component = self:getComponent(SpotLight) or self:addComponent(SpotLight)
component:setIntensity(2)
```

### SpotLight.setRange

`setRange(self:DirectionalLight|PointLight|SpotLight,value:number)`

```lua
local component = self:getComponent(SpotLight) or self:addComponent(SpotLight)
component:setRange(20)
```

### SpotLight.setEnabled

`setEnabled(self:DirectionalLight|PointLight|SpotLight,enabled:boolean)`

```lua
local component = self:getComponent(SpotLight) or self:addComponent(SpotLight)
component:setEnabled(true)
```

### SpotLight.setShadows

`setShadows(self:DirectionalLight|PointLight|SpotLight,enabled:boolean)`

```lua
local component = self:getComponent(SpotLight) or self:addComponent(SpotLight)
component:setShadows(true)
```

### SpotLight.setShadowQuality

`setShadowQuality(self:DirectionalLight|PointLight|SpotLight,quality:integer)`

```lua
local component = self:getComponent(SpotLight) or self:addComponent(SpotLight)
component:setShadowQuality(2)
```

### SpotLight.setSpotAngles

`setSpotAngles(self:DirectionalLight|PointLight|SpotLight,inner:number,outer:number)`

```lua
local component = self:getComponent(SpotLight) or self:addComponent(SpotLight)
component:setSpotAngles(15, 35)
```

## Skybox

### Skybox.setType

`setType(self:Skybox,type:string|integer) Cubemap or HDR, or 0/1 (Textures is a legacy alias)`

- `sky:setType('Textures'|'HDR') - Cubemap or HDR skybox (Textures is a legacy alias)`

```lua
local component = self:getComponent(Skybox) or self:addComponent(Skybox)
component:setType("Textures")
```

### Skybox.setColor

`setColor(self:Skybox,r:number,g:number,b:number)`

```lua
local component = self:getComponent(Skybox) or self:addComponent(Skybox)
component:setColor(1, 0.8, 0.6)
```

### Skybox.setEnabled

`setEnabled(self:Skybox,enabled:boolean)`

```lua
local component = self:getComponent(Skybox) or self:addComponent(Skybox)
component:setEnabled(true)
```

### Skybox.setCubemap

`setCubemap(self:Skybox,path:string|nil) Set Cubemap mode and project-relative asset`

Selects Cubemap mode and assigns a project-relative .cubemap asset. Author its six faces in Cubemap Editor; new Skybox components no longer need six independent Inspector slots. Nil clears the cubemap.

- `sky:setCubemap(projectRelativeCubemapPath) - select a .cubemap asset`

```lua
local component = self:getComponent(Skybox) or self:addComponent(Skybox)
component:setCubemap("Sky/Evening.cubemap")
```

### Skybox.setHDR

`setHDR(self:Skybox,texture:Texture|nil)`

Selects HDR mode and assigns a project-relative Radiance panorama. Nil clears the slot. Use setCubemap for six-face skybox assets.

- `sky:setHDR(Texture) - project .hdr panorama; nil clears`

```lua
local component = self:getComponent(Skybox) or self:addComponent(Skybox)
component:setHDR("Sky/studio.hdr")
```

### Skybox.setFace

`setFace(self:Skybox,face:string,texture:Texture|nil)`

- `sky:setFace(face,Texture) - right/left/top/bottom/front/back; nil clears`

```lua
local component = self:getComponent(Skybox) or self:addComponent(Skybox)
component:setFace("front", "Sky/front.png")
```

### Skybox.fadeTo

`fadeTo(self:Skybox,destination:GameObject|Skybox,speed:number) Smooth crossfade, duration = 1/speed seconds`

- `sky:fadeTo(destination, speed) - smooth skybox transition`

```lua
local skybox1 = self:getComponent(Skybox) or self:addComponent(Skybox)
local skybox2 = GameObject.new("Skybox2"):addComponent(Skybox)
skybox2:setColor(0.3, 0.1, 0.05)
skybox2:setEnabled(false)
skybox1:fadeTo(skybox2, 0.5)
```

### Skybox.setTextures

`setTextures(self:Skybox,right:Texture,left:Texture,top:Texture,bottom:Texture,front:Texture,back:Texture)`

- `sky:setTextures(right,left,top,bottom,front,back)`

```lua
local component = self:getComponent(Skybox) or self:addComponent(Skybox)
component:setTextures("Sky/right.png", "Sky/left.png", "Sky/top.png", "Sky/bottom.png", "Sky/front.png", "Sky/back.png")
```

## CharacterController

### CharacterController.move

`move(self:CharacterController,x:number,z:number) Local horizontal direction; normalized above unit length; persists until replaced`

Sets persistent local X/Z input, clamped to unit length. Call move(0,0) to stop. Applied by fixed physics steps; do not multiply by deltaTime. Requires Transform, Collider and dynamic RigidBody (added automatically). Upright rotation is locked against physics torque. State flags update on fixed steps.

- `controller:move(x,z) - persistent local direction; forward is negative Z, no deltaTime multiplication`

```lua
local component = self:getComponent(CharacterController) or self:addComponent(CharacterController)
component:move(0, -1)
```

### CharacterController.jump

`jump(self:CharacterController) Queue one jump; applied only when grounded and canJump`

Queues one grounded jump. Requests while airborne or canJump=false are discarded; holding a button does not automatically retrigger unless your script requests another jump. isJumping means airborne and ascending, not falling.

- `controller:jump() - request a grounded jump if canJump`

```lua
local component = self:getComponent(CharacterController) or self:addComponent(CharacterController)
component:jump()
```

### CharacterController.sprint

`sprint(self:CharacterController,enabled:boolean) Set sprint intent; omitted means true`

Stores sprint intent; true when omitted. isSprinting requires canMove, canSprint and nonzero movement input.

- `controller:sprint(enabled=true) - request sprint if canSprint and moving`

```lua
local component = self:getComponent(CharacterController) or self:addComponent(CharacterController)
component:sprint(true)
```

## FPSCamera

### FPSCamera.linkToCharacter

`linkToCharacter(self:FPSCamera,controller:CharacterController|GameObject,x:number,y:number,z:number) Parent using local position offset`

Accepts a CharacterController component or its GameObject followed by x,y,z local position coordinates. The older vector-table argument also remains supported. Parents the camera and sets its parent-local position offset, default (0,0.7,0). Mouse yaw turns the character, while pitch stays local and clamps to +/-89 degrees. Existing Camera APIs remain available on the camera GameObject; FPSCamera also forwards Camera settings and methods.

- `fps:linkToCharacter(controller, x,y,z) - parent using local position offset`

```lua
local character = self:getComponent(CharacterController) or self:addComponent(CharacterController)
local camera = Camera.new("Player FPS Camera")
local fps = camera:addComponent(FPSCamera)
fps:linkToCharacter(character, 0, 0.7, 0)
```

### FPSCamera.setIsFreecam

`setIsFreecam(self:FPSCamera,enabled:boolean)`

Unparented FPS cameras with isFreecam=true use native WASD and Q/E movement while Game input is focused. False disables translation, not mouse look. Parented FPS cameras never translate freely. Mouse look requires a locked cursor; mouseLook=false leaves rotation to scripts.

- `fps:setIsFreecam(enabled) - allow unparented free movement`

```lua
local component = self:getComponent(FPSCamera) or self:addComponent(FPSCamera)
component:setIsFreecam(false)
```

### FPSCamera.getIsFreecam

`getIsFreecam(self:FPSCamera):boolean`

- `fps:getIsFreecam() -> boolean`

```lua
local component = self:getComponent(FPSCamera) or self:addComponent(FPSCamera)
component:getIsFreecam()
```

## RigidBody

### RigidBody.addForce

`addForce(self:RigidBody,x:number,y:number,z:number) World force consumed on next physics step`

Accumulates world force for the next fixed physics step. Apply from onFixedUpdate for continuous thrust; mass zero or kinematic bodies ignore force. addImpulse applies velocity change impulse/mass without deltaTime multiplication.

- `body:addForce(x,y,z) - world force for next fixed step; also accepts a vector table`

```lua
local component = self:getComponent(RigidBody) or self:addComponent(RigidBody)
component:addForce(0, 10, 0)
```

### RigidBody.addImpulse

`addImpulse(self:RigidBody,x:number,y:number,z:number) Instant world impulse at next physics step`

- `body:addImpulse(x,y,z) - world impulse for next fixed step`

```lua
local component = self:getComponent(RigidBody) or self:addComponent(RigidBody)
component:addImpulse(0, 3, 0)
```

### RigidBody.setVelocity

`setVelocity(self:RigidBody,x:number,y:number,z:number)`

- `body:setVelocity(x,y,z) - metres per second`

```lua
local component = self:getComponent(RigidBody) or self:addComponent(RigidBody)
component:setVelocity(1, 0, 0)
```

### RigidBody.getVelocity

`getVelocity(self:RigidBody):Vec3`

- `body:getVelocity() -> world velocity vector`

```lua
local component = self:getComponent(RigidBody) or self:addComponent(RigidBody)
component:getVelocity()
```

### RigidBody.addVelocity

`addVelocity(self:RigidBody,x:number,y:number,z:number)`

- `body:addVelocity(x,y,z)`

```lua
local component = self:getComponent(RigidBody) or self:addComponent(RigidBody)
component:addVelocity(0, 1, 0)
```

### RigidBody.movePosition

`movePosition(self:RigidBody,x:number,y:number,z:number) Set world position`

Changes world position (teleports); this is not a swept collision-aware character move. Use velocity or CharacterController for simulated movement.

- `body:movePosition(x,y,z) - world position, teleport rather than sweep`

```lua
local component = self:getComponent(RigidBody) or self:addComponent(RigidBody)
component:movePosition(0, 2, 0)
```

### RigidBody.moveRotation

`moveRotation(self:RigidBody,x:number,y:number,z:number) Euler degrees, or quaternion table`

- `body:moveRotation(x,y,z) - Euler degrees, or quaternion table`

```lua
local component = self:getComponent(RigidBody) or self:addComponent(RigidBody)
component:moveRotation(0, 90, 0)
```

### RigidBody.stop

`stop(self:RigidBody) Clear velocities and queued forces/impulses`

- `audio:stop() - stop and rewind`

```lua
local component = self:getComponent(RigidBody) or self:addComponent(RigidBody)
component:stop()
```

## Raycast

### Raycast.cast

`cast(self:Raycast):RaycastHit|nil Nearest matching hit`

Queries current synchronized collider poses. Unselected layers are ignored before hit selection, so rays pass through them. Returns a generation-safe collider/transform and world point, normal and distance, or nil. The owner is excluded.

- `query:cast() -> nearest hit or nil`

```lua
local component = self:getComponent(Raycast) or self:addComponent(Raycast)
component:cast()
```

### Raycast.castAll

`castAll(self:Raycast):RaycastHit[] Matching distinct objects, nearest first`

Line queries return all distinct hit objects; LineOfSight samples an adjustable fan in local XZ and returns the nearest selected blocker on each ray, deduplicated and distance sorted. Sphere queries return overlapping collider objects; point/normal are the surface intersection toward collider center, or origin/zero if inside. This is not a swept sphere cast.

- `query:castAll() -> distance-sorted distinct hit objects`

```lua
local component = self:getComponent(Raycast) or self:addComponent(Raycast)
component:castAll()
```

### Raycast.setLayers

`setLayers(self:Raycast,names:table) Replace layer mask with an array of names; empty array selects none`

- `query:setLayers({'Default','Enemies'}) - ignore other layers before finding hits`

```lua
local component = self:getComponent(Raycast) or self:addComponent(Raycast)
component:setLayers({'Default'})
```

## Component fields and type tokens

Component type names such as `Transform`, `Skybox`, `PointLight` and `AudioSource`
are opaque tokens for get/add/remove operations, not callable loaders.
`BoxCollider` aliases `Collider`. `Texture` is an export-only project asset path.
`self` is generation-safe; using destroyed objects/components raises a script error.

### Vec3 fields

| Field | Type / meaning |
| --- | --- |
| `x` | `number` |
| `y` | `number` |
| `z` | `number` |

### Quaternion fields

| Field | Type / meaning |
| --- | --- |
| `w` | `number` |

### GameObject fields

| Field | Type / meaning |
| --- | --- |
| `name` | `string` |
| `parent` | `GameObject / nil` |
| `layer` | `string Layer name, defaults to Default; assign an existing name or slot number` |

### Transform fields

| Field | Type / meaning |
| --- | --- |
| `position` | `Vec3` |
| `rotation` | `Quaternion` |
| `scale` | `Vec3` |

### RigidBody fields

| Field | Type / meaning |
| --- | --- |
| `mass` | `number` |
| `useGravity` | `boolean` |
| `linearVelocity` | `Vec3` |
| `angularVelocity` | `Vec3` |
| `velocity` | `Vec3 Alias of linearVelocity` |
| `drag` | `number Nonnegative linear damping` |
| `angularDrag` | `number Nonnegative angular damping` |
| `isKinematic` | `boolean Move under script control rather than gravity/forces` |

### Collider fields

| Field | Type / meaning |
| --- | --- |
| `shape` | `'Box' / 'Sphere' / 'Capsule' / 'Cone'` |
| `size` | `Vec3` |
| `center` | `Vec3` |
| `radius` | `number` |
| `height` | `number` |
| `friction` | `number` |
| `bounciness` | `number` |
| `isTrigger` | `boolean Non-solid overlap sensor` |
| `gameObject` | `GameObject Collider owner; inspect name, layer and components` |
| `transform` | `Transform Collider owner transform` |

### MeshRenderer fields

| Field | Type / meaning |
| --- | --- |
| `material` | `string Project-relative .mat asset path; empty uses inline Diffuse` |
| `mesh` | `'Cube' / 'Sphere' / 'Capsule' / 'Cone' / 'Plane'` |
| `color` | `Vec3` |
| `checkerboard` | `boolean` |
| `specular` | `number` |

### ObjectName fields

| Field | Type / meaning |
| --- | --- |
| `value` | `string` |

### LuaScript fields

| Field | Type / meaning |
| --- | --- |
| `path` | `string Relative asset path under Assets` |
| `enabled` | `boolean` |

### Camera fields

| Field | Type / meaning |
| --- | --- |
| `active` | `boolean` |
| `orthographic` | `boolean` |
| `fieldOfView` | `number Degrees` |
| `nearClip` | `number` |
| `farClip` | `number` |
| `orthographicSize` | `number Vertical size in world units` |

### AudioSource fields

| Field | Type / meaning |
| --- | --- |
| `clip` | `AudioClip` |
| `onAudioStarted` | `AudioEvent` |
| `onAudioFinished` | `AudioEvent` |
| `onAudioPaused` | `AudioEvent` |
| `onAudioResumed` | `AudioEvent` |
| `onAudioStopped` | `AudioEvent` |

### AudioListener fields

| Field | Type / meaning |
| --- | --- |
| `enabled` | `boolean` |

### Vec2 fields

| Field | Type / meaning |
| --- | --- |
| `x` | `number` |
| `y` | `number` |

### DirectionalLight fields

| Field | Type / meaning |
| --- | --- |
| `color` | `Vec3` |
| `intensity` | `number` |
| `enabled` | `boolean` |
| `shadows` | `boolean` |
| `range` | `number` |
| `innerAngle` | `number` |
| `outerAngle` | `number` |
| `shadowDistance` | `number` |
| `shadowBias` | `number` |
| `shadowQuality` | `integer 0=Off, 1=Low (256), 2=Medium (512), 3=High (1024)` |

### PointLight fields

| Field | Type / meaning |
| --- | --- |
| `color` | `Vec3` |
| `intensity` | `number` |
| `enabled` | `boolean` |
| `shadows` | `boolean` |
| `range` | `number` |
| `innerAngle` | `number` |
| `outerAngle` | `number` |
| `shadowDistance` | `number` |
| `shadowBias` | `number` |
| `shadowQuality` | `integer 0=Off, 1=Low (256), 2=Medium (512), 3=High (1024)` |

### SpotLight fields

| Field | Type / meaning |
| --- | --- |
| `color` | `Vec3` |
| `intensity` | `number` |
| `enabled` | `boolean` |
| `shadows` | `boolean` |
| `range` | `number` |
| `innerAngle` | `number` |
| `outerAngle` | `number` |
| `shadowDistance` | `number` |
| `shadowBias` | `number` |
| `shadowQuality` | `integer 0=Off, 1=Low (256), 2=Medium (512), 3=High (1024)` |

### Skybox fields

| Field | Type / meaning |
| --- | --- |
| `type` | `integer 0 cubemap asset (legacy inline faces supported), 1 HDR panorama` |
| `enabled` | `boolean` |
| `color` | `Vector3 Color tint` |
| `right` | `Texture +X` |
| `left` | `Texture -X` |
| `top` | `Texture +Y` |
| `bottom` | `Texture -Y` |
| `front` | `Texture -Z` |
| `back` | `Texture +Z` |
| `cubemap` | `string Project-relative .cubemap asset` |
| `hdr` | `Texture Radiance .hdr equirectangular panorama` |

### CharacterController fields

| Field | Type / meaning |
| --- | --- |
| `speed` | `number Metres per second; default 4.5` |
| `sprintMultiplier` | `number At least 1; default 1.7` |
| `jumpSpeed` | `number Upward launch velocity; default 6` |
| `groundProbe` | `number Distance below capsule feet; default 0.12` |
| `canMove` | `boolean Writable movement gate` |
| `canJump` | `boolean Writable jump gate` |
| `canSprint` | `boolean Writable sprint gate` |
| `isGrounded` | `boolean Read-only, updated at fixed physics steps` |
| `isJumping` | `boolean Read-only, airborne and ascending` |
| `isMoving` | `boolean Read-only, permitted nonzero movement command` |
| `isSprinting` | `boolean Read-only, moving with permitted sprint command` |

### FPSCamera fields

| Field | Type / meaning |
| --- | --- |
| `fieldOfView` | `number Inherited Camera field` |
| `nearClip` | `number Inherited Camera field` |
| `farClip` | `number Inherited Camera field` |
| `orthographic` | `boolean Inherited Camera field` |
| `active` | `boolean Inherited Camera field` |
| `isFreecam` | `boolean Writable; only unparented cameras can translate freely` |
| `mouseLook` | `boolean Writable; native mouse look while cursor is locked` |
| `speed` | `number Freecam metres per second` |
| `sensitivity` | `number Degrees per mouse pixel` |

### ColorPalette fields

| Field | Type / meaning |
| --- | --- |
| `red` | `Vec3` |
| `green` | `Vec3` |
| `blue` | `Vec3` |
| `white` | `Vec3` |
| `black` | `Vec3` |
| `yellow` | `Vec3` |
| `cyan` | `Vec3` |
| `magenta` | `Vec3` |
| `gray` | `Vec3` |
| `orange` | `Vec3` |
| `purple` | `Vec3` |
| `pink` | `Vec3` |

### RaycastHit fields

| Field | Type / meaning |
| --- | --- |
| `collider` | `Collider` |
| `transform` | `Transform` |
| `gameObject` | `GameObject` |
| `point` | `Vec3 World hit point; sphere query uses center-directed surface point` |
| `normal` | `Vec3 World surface normal; zero when sphere origin is inside a collider` |
| `distance` | `number World distance from query origin` |

### Raycast fields

| Field | Type / meaning |
| --- | --- |
| `type` | `integer Raycast.Line, Raycast.LineOfSight or Raycast.Sphere` |
| `distance` | `number Positive ray range` |
| `radius` | `number Positive sphere overlap radius` |
| `angle` | `number Fan aperture in degrees, 0 to 360` |
| `samples` | `integer Fan rays, 2 to 181` |
| `offset` | `Vec3 Local origin offset` |
| `includeTriggers` | `boolean Whether sensors can be hit` |

### RaycastType fields

| Field | Type / meaning |
| --- | --- |
| `Line` | `integer` |
| `LineOfSight` | `integer` |
| `Sphere` | `integer` |

## Skybox1 to Skybox2 transition

Use `fadeSkybox(skybox1, skybox2, speed)` for explicit source/destination,
`fadeSkybox(skybox2, speed)` for the active source, or `skybox1Component:fadeTo(skybox2, speed)`.
A speed of 0.5 takes two seconds with smoothstep easing. The source stays enabled during
the blend. At completion Skybox1 is disabled and Skybox2 is enabled: their names,
component identities, textures and colors are not overwritten. Prepare valid textures
first; failed destination loading cancels the transition and keeps the source.

```lua
function onStart()
    local skybox1 = GameObject.find("Skybox1")
    local skybox2 = GameObject.find("Skybox2")
    if skybox1 and skybox2 then fadeSkybox(skybox1, skybox2, 0.5) end
end
```
