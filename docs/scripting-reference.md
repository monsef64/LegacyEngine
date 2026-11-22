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

## Named script components

Use `object:getScript("Health")` or `object:getComponent("Health")` to resolve an enabled
attachment by its unique case-insensitive filename stem. Proxies expose global scalar
fields and synchronous functions, not Lua locals or tables. See
[named script components](scripting.md#named-script-components) for examples, bridge limits
and animation function events.

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

### onGUI

`onGUI() - Called once per rendered Game frame during unpaused Play. GUI calls are valid only here; no wait/task.`

```lua
function onGUI()
    GUI.beginWindow("HUD")
    GUI.text("Hello")
    GUI.endWindow()
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
- `return Transform|RigidBody|Collider|MeshRenderer|ObjectName|LuaScript|Animation|ParticleSystem|Laser|LightShafts|CameraEffects|Fog|CloudSystem|River|Vehicle|Water|BoneAttachment|TPSCamera|NavMeshAgent|UI|Decal|nil`
- `overload fun(componentType:ComponentType):any`

```lua
local transform = getComponent(self, Transform)
if transform then debug.log(transform.position.x) end
```

### addComponent

`addComponent(gameObject, componentType) / component:addComponent(Type) -> live component on the same owner`

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

`lockCursor() -> boolean; viewport-confined motion, cursor hidden`

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

### task

`task(function() ... end) - run an independent cooperative block; waits pause only that task; call inside lifecycle/object callbacks or tasks`

- `param block fun()`

```lua
function onStart()
    task(function()
        wait(1)
        debug.log("Independent task completed")
    end)
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

`wait(seconds) / wait(audioSource.onAudioFinished) - suspend this callback/task until time or the next audio event`

Takes an opaque AudioEvent token, not a function name string or a call to the event. Waits for its next occurrence after subscribing. Stop/reset cancels waits; removing the AudioSource cancels its continuation.

- `param secondsOrEvent number|AudioEvent Finite 0..86400 seconds or a live audio event token`

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

### getComponentInParent

`getComponentInParent(Type) / getComponentInParent(object, Type) / object:getComponentInParent(Type) -> live component or nil; self first, then nearest parent`

- `param gameObject GameObject`
- `param componentType ComponentType`
- `return any|nil`
- `overload fun(componentType:ComponentType):any|nil`

```lua
local body = self:getComponentInParent(RigidBody)
if body then debug.log(body.mass) end
```

### getComponentInChildren

`getComponentInChildren(Type) / getComponentInChildren(object, Type) / object:getComponentInChildren(Type) -> live component or nil; self first, then breadth-first descendants`

- `param gameObject GameObject`
- `param componentType ComponentType`
- `return any|nil`
- `overload fun(componentType:ComponentType):any|nil`

```lua
local camera = self:getComponentInChildren(Camera)
if camera then debug.log(camera.gameObject:getName()) end
```

### findComponentInParent

`findComponentInParent(Type) / findComponentInParent(object, Type) / object:findComponentInParent(Type) -> alias of getComponentInParent`

- `param gameObject GameObject`
- `param componentType ComponentType`
- `return any|nil`
- `overload fun(componentType:ComponentType):any|nil`

```lua
local body = findComponentInParent(self, RigidBody)
if body then debug.log(body.mass) end
```

### findComponentInChildren

`findComponentInChildren(Type) / findComponentInChildren(object, Type) / object:findComponentInChildren(Type) -> alias of getComponentInChildren`

- `param gameObject GameObject`
- `param componentType ComponentType`
- `return any|nil`
- `overload fun(componentType:ComponentType):any|nil`

```lua
local camera = findComponentInChildren(Camera)
if camera then debug.log(camera.gameObject:getName()) end
```

### fadeToColor

`fadeToColor(rgb) -> ColorFade for the current active camera; RGB 0..1, e.g. color.black. Call start(seconds=1) to fade to opaque, stop(seconds=1) to fade to transparent; 0..3600 playing seconds, 0 immediate. Controllers share camera state.`

- `param rgb Vec3 RGB 0..1, e.g. color.black`
- `return ColorFade`

```lua
local fade = fadeToColor(color.black)
fade:start(1)
-- Later, reverse toward transparent:
fade:stop(0.5)
```

### Vector3

`Vector3(x,y,z) - Construct a vector table accepted by position, rotation, destination and obstacle vector setters.`

- `param x? number`
- `param y? number`
- `param z? number`
- `return Vec3`

```lua
local camera = Camera.new("Reference Camera")
camera:setPosition(0, 2, 6)
local result = Vector3(camera, local destination = Vector3(2, 0, 4)
self:setPosition(destination))
```

### getTextureQuality

`getTextureQuality():integer - Effective texture quality: 0 full, 1 half, 2 quarter, 3 eighth.`

- `return integer quality 0 full, 1 half, 2 quarter, 3 eighth`

```lua
debug.log(tostring(getTextureQuality()))
```

### setTextureQuality

`setTextureQuality(quality:integer) - Play-only override 0..3; Stop restores project default.`

- `param quality integer Play-only texture quality, 0..3`

```lua
function onStart()
    setTextureQuality(1)
end
```

### print

`print(...) - log tab-separated values`

```lua
print("Player position", self:getPosition().x)
```

## GameObject

### GameObject.getScript

`getScript(name:string):NamedScriptComponent|nil`

```lua
local health = self:getScript("Health")
if health then debug.log(health.health) end
```

### GameObject.getComponent

`getComponent(componentType:ComponentType|string):NamedScriptComponent|Transform|RigidBody|Collider|MeshRenderer|ObjectName|LuaScript|Animation|ParticleSystem|Laser|LightShafts|CameraEffects|Fog|CloudSystem|River|Vehicle|Water|BoneAttachment|TPSCamera|NavMeshAgent|UI|Decal|nil`

- `getComponent(Type) / getComponent(gameObject, Type) / object.getComponent(Type) -> live component or nil`

```lua
local object = self
object:getComponent(Transform)
```

### GameObject.getComponentInParent

`getComponentInParent(componentType:ComponentType):Transform|RigidBody|Collider|MeshRenderer|ObjectName|LuaScript|Animation|ParticleSystem|Laser|LightShafts|CameraEffects|Fog|CloudSystem|River|Vehicle|Water|BoneAttachment|TPSCamera|NavMeshAgent|UI|Decal|nil`

- `getComponentInParent(Type) / getComponentInParent(object, Type) / object:getComponentInParent(Type) -> live component or nil; self first, then nearest parent`

```lua
local object = self
object:getComponentInParent(RigidBody)
```

### GameObject.getComponentInChildren

`getComponentInChildren(componentType:ComponentType):Transform|RigidBody|Collider|MeshRenderer|ObjectName|LuaScript|Animation|ParticleSystem|Laser|LightShafts|CameraEffects|Fog|CloudSystem|River|Vehicle|Water|BoneAttachment|TPSCamera|NavMeshAgent|UI|Decal|nil`

- `getComponentInChildren(Type) / getComponentInChildren(object, Type) / object:getComponentInChildren(Type) -> live component or nil; self first, then breadth-first descendants`

```lua
local object = self
object:getComponentInChildren(Camera)
```

### GameObject.findComponentInParent

`findComponentInParent(componentType:ComponentType):Transform|RigidBody|Collider|MeshRenderer|ObjectName|LuaScript|Animation|ParticleSystem|Laser|LightShafts|CameraEffects|Fog|CloudSystem|River|Vehicle|Water|BoneAttachment|TPSCamera|NavMeshAgent|UI|Decal|nil`

- `findComponentInParent(Type) / findComponentInParent(object, Type) / object:findComponentInParent(Type) -> alias of getComponentInParent`

```lua
local object = self
object:findComponentInParent(RigidBody)
```

### GameObject.findComponentInChildren

`findComponentInChildren(componentType:ComponentType):Transform|RigidBody|Collider|MeshRenderer|ObjectName|LuaScript|Animation|ParticleSystem|Laser|LightShafts|CameraEffects|Fog|CloudSystem|River|Vehicle|Water|BoneAttachment|TPSCamera|NavMeshAgent|UI|Decal|nil`

- `findComponentInChildren(Type) / findComponentInChildren(object, Type) / object:findComponentInChildren(Type) -> alias of getComponentInChildren`

```lua
local object = self
object:findComponentInChildren(Camera)
```

### GameObject.addComponent

`addComponent(componentType:ComponentType):Transform|RigidBody|Collider|MeshRenderer|ObjectName|LuaScript|Animation|ParticleSystem|Laser|LightShafts|CameraEffects|Fog|CloudSystem|River|Vehicle|Water|BoneAttachment|TPSCamera|NavMeshAgent|UI|Decal`

- `addComponent(gameObject, componentType) / component:addComponent(Type) -> live component on the same owner`

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

### GameObject.onAnimationFinish

`onAnimationFinish()|nil`

- `onAnimationFinish() - targeted animation lifecycle callback`

```lua
self.onAnimationFinish = function()
    debug.log("Object callback: onAnimationFinish")
end
```

### GameObject.onAnimationStart

`onAnimationStart()|nil`

- `onAnimationStart() - targeted animation lifecycle callback`

```lua
self.onAnimationStart = function()
    debug.log("Object callback: onAnimationStart")
end
```

### GameObject.onAnimationStopped

`onAnimationStopped()|nil`

- `onAnimationStopped() - targeted animation lifecycle callback`

```lua
self.onAnimationStopped = function()
    debug.log("Object callback: onAnimationStopped")
end
```

### GameObject.onAnimationPause

`onAnimationPause()|nil`

- `onAnimationPause() - targeted animation lifecycle callback`

```lua
self.onAnimationPause = function()
    debug.log("Object callback: onAnimationPause")
end
```

### GameObject.onAnimationResume

`onAnimationResume()|nil`

- `onAnimationResume() - targeted animation lifecycle callback`

```lua
self.onAnimationResume = function()
    debug.log("Object callback: onAnimationResume")
end
```

### GameObject.onGUI

`onGUI()|nil Draw GUI once per rendered Game frame; no wait/task`

- `onGUI() - Called once per rendered Game frame during unpaused Play. GUI calls are valid only here; no wait/task.`

```lua
self.onGUI = function()
    debug.log("Object callback: onGUI")
end
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

`setMaterialShader(self:MeshRenderer,shader:string) Diffuse, Diffuse Bumped, Diffuse Transparent or Diffuse Reflection`

- `mesh:setMaterialShader('Diffuse'|'Diffuse Bumped'|'Diffuse Transparent'|'Diffuse Reflection')`

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

### MeshRenderer.setMaterialAlpha

`setMaterialAlpha(self:MeshRenderer,value:number) 0..1 opacity for Diffuse Transparent`

- `mesh:setMaterialAlpha(value) - 0..1 opacity multiplier for Diffuse Transparent; defaults to 1.`

```lua
local component = self:getComponent(MeshRenderer) or self:addComponent(MeshRenderer)
component:setMaterialAlpha(0.5)
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

## Animation

### Animation.play

`play(self:Animation)`

- `audio:play() - restart assigned clip`

```lua
local component = self:getComponent(Animation) or self:addComponent(Animation)
component:play()
```

### Animation.stop

`stop(self:Animation) Stops and rewinds to frame zero`

- `audio:stop() - stop and rewind`

```lua
local component = self:getComponent(Animation) or self:addComponent(Animation)
component:stop()
```

### Animation.pause

`pause(self:Animation)`

- `audio:pause()`

```lua
local component = self:getComponent(Animation) or self:addComponent(Animation)
component:pause()
```

### Animation.resume

`resume(self:Animation)`

- `audio:resume()`

```lua
local component = self:getComponent(Animation) or self:addComponent(Animation)
component:resume()
```

### Animation.crossFade

`crossFade(self:Animation,clip:string,seconds?:number) Timed transition; default 0.25 seconds`

```lua
local animation = self:getComponent(Animation)
animation:crossFade("Character_data/Animation_1.leanim", 0.3)
```

### Animation.reset

`reset(self:Animation) Stops and restores the pre-play values`

```lua
local component = self:getComponent(Animation) or self:addComponent(Animation)
component:reset()
```

## ParticleSystem

### ParticleSystem.play

`play(self:ParticleSystem)`

- `audio:play() - restart assigned clip`

```lua
local component = self:getComponent(ParticleSystem) or self:addComponent(ParticleSystem)
component:play()
```

### ParticleSystem.pause

`pause(self:ParticleSystem)`

- `audio:pause()`

```lua
local component = self:getComponent(ParticleSystem) or self:addComponent(ParticleSystem)
component:pause()
```

### ParticleSystem.resume

`resume(self:ParticleSystem)`

- `audio:resume()`

```lua
local component = self:getComponent(ParticleSystem) or self:addComponent(ParticleSystem)
component:resume()
```

### ParticleSystem.stop

`stop(self:ParticleSystem,clearParticles?:boolean)`

- `audio:stop() - stop and rewind`

```lua
local component = self:getComponent(ParticleSystem) or self:addComponent(ParticleSystem)
component:stop()
```

### ParticleSystem.restart

`restart(self:ParticleSystem)`

```lua
local component = self:getComponent(ParticleSystem) or self:addComponent(ParticleSystem)
component:restart()
```

### ParticleSystem.clear

`clear(self:ParticleSystem)`

```lua
local component = self:getComponent(ParticleSystem) or self:addComponent(ParticleSystem)
component:clear()
```

### ParticleSystem.emit

`emit(self:ParticleSystem,count:integer)`

- `emit(eventName, ...) -> boolean; queue up to eight primitive arguments`

```lua
local component = self:getComponent(ParticleSystem) or self:addComponent(ParticleSystem)
component:emit(10)
```

### ParticleSystem.setPreset

`setPreset(self:ParticleSystem,name:string) Fire, Smoke, Sparkles, Blood or Explosion`

```lua
local component = self:getComponent(ParticleSystem) or self:addComponent(ParticleSystem)
component:setPreset("Fire")
```

## ColorFade

### ColorFade.start

`start(self:ColorFade,seconds?:number) Fade to fully visible color, default one second; zero is immediate`

```lua
local fade = fadeToColor(color.black)
fade:start(1)
```

### ColorFade.stop

`stop(self:ColorFade,seconds?:number) Fade back to transparent, default one second; zero is immediate`

- `audio:stop() - stop and rewind`

```lua
local fade = fadeToColor(color.black)
fade:stop(1)
```

## NavMeshAgent

### NavMeshAgent.setDestination

`setDestination(self:NavMeshAgent, x:number|Vec3, y?:number, z?:number) Queue a path request; resumes movement`

```lua
local component = self:getComponent(NavMeshAgent) or self:addComponent(NavMeshAgent)
component:setDestination(Vector3(8, 0, 4))
```

### NavMeshAgent.resetPath

`resetPath(self:NavMeshAgent) Clear destination request, route and velocity`

```lua
local component = self:getComponent(NavMeshAgent) or self:addComponent(NavMeshAgent)
component:resetPath()
```

## UI

### UI.isLoaded

`isLoaded(self:UI):boolean Whether this component's document is loaded in Play.`

```lua
local gui = self:getComponent(UI)
if gui and gui:isLoaded() then debug.log("UI ready") end
```

### UI.setText

`setText(self:UI,id:string,plainText:string):boolean Set escaped plain text in this UI document.`

```lua
local gui = self:getComponent(UI)
if gui then gui:setText("health", "100 / 100") end
```

### UI.onClick

`onClick(self:UI,id:string,callback?:fun()):boolean|integer Register a click callback (returns off(token) handle); omit callback to consume a queued click as a boolean. Register callbacks once in onStart.`

```lua
function onStart()
    local gui = self:getComponent(UI)
    if gui then gui:onClick("collect", function() gui:setText("status", "Collected!") end) end
end
```

## GUI

### GUI.image

`image(path:string,width?:number,height?:number):boolean Full-resolution project image; returns loaded.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.image("icon.png", 64, 64)
        GUI.endWindow()
end
```

### GUI.imageButton

`imageButton(id:string,path:string,width?:number,height?:number):boolean Returns clicked.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.imageButton("icon", "icon.png", 64, 64)
        GUI.endWindow()
end
```

### GUI.beginTable

`beginTable(id:string,columns:integer,flags?:integer,height?:number):boolean 1..64 columns; only end when true. ScrollY needs height.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.beginTable("Stats", 2, GUI.TableFlags.Borders) then
        GUI.tableSetupColumn("Name")
        GUI.tableSetupColumn("Value")
        GUI.tableHeadersRow()
        GUI.tableNextRow()
        GUI.tableSetColumnIndex(0)
        GUI.text("Health")
        GUI.tableNextColumn()
        GUI.text("100")
        GUI.endTable()
        end
        GUI.endWindow()
end
```

### GUI.endTable

`endTable() Table scope operation; set up columns before rows.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.beginTable("Stats", 2, GUI.TableFlags.Borders) then
        GUI.tableSetupColumn("Name")
        GUI.tableSetupColumn("Value")
        GUI.tableHeadersRow()
        GUI.tableNextRow()
        GUI.tableSetColumnIndex(0)
        GUI.text("Health")
        GUI.tableNextColumn()
        GUI.text("100")
        GUI.endTable()
        end
        GUI.endWindow()
end
```

### GUI.tableNextRow

`tableNextRow() Table scope operation; set up columns before rows.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.beginTable("Stats", 2, GUI.TableFlags.Borders) then
        GUI.tableSetupColumn("Name")
        GUI.tableSetupColumn("Value")
        GUI.tableHeadersRow()
        GUI.tableNextRow()
        GUI.tableSetColumnIndex(0)
        GUI.text("Health")
        GUI.tableNextColumn()
        GUI.text("100")
        GUI.endTable()
        end
        GUI.endWindow()
end
```

### GUI.tableHeadersRow

`tableHeadersRow() Table scope operation; set up columns before rows.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.beginTable("Stats", 2, GUI.TableFlags.Borders) then
        GUI.tableSetupColumn("Name")
        GUI.tableSetupColumn("Value")
        GUI.tableHeadersRow()
        GUI.tableNextRow()
        GUI.tableSetColumnIndex(0)
        GUI.text("Health")
        GUI.tableNextColumn()
        GUI.text("100")
        GUI.endTable()
        end
        GUI.endWindow()
end
```

### GUI.tableNextColumn

`tableNextColumn():boolean Advance to next column.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.beginTable("Stats", 2, GUI.TableFlags.Borders) then
        GUI.tableSetupColumn("Name")
        GUI.tableSetupColumn("Value")
        GUI.tableHeadersRow()
        GUI.tableNextRow()
        GUI.tableSetColumnIndex(0)
        GUI.text("Health")
        GUI.tableNextColumn()
        GUI.text("100")
        GUI.endTable()
        end
        GUI.endWindow()
end
```

### GUI.tableSetColumnIndex

`tableSetColumnIndex(index:integer):boolean Zero-based table column index.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.beginTable("Stats", 2, GUI.TableFlags.Borders) then
        GUI.tableSetupColumn("Name")
        GUI.tableSetupColumn("Value")
        GUI.tableHeadersRow()
        GUI.tableNextRow()
        GUI.tableSetColumnIndex(0)
        GUI.text("Health")
        GUI.tableNextColumn()
        GUI.text("100")
        GUI.endTable()
        end
        GUI.endWindow()
end
```

### GUI.tableSetupColumn

`tableSetupColumn(label:string,width?:number) Fixed width when positive, otherwise stretch.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.beginTable("Stats", 2, GUI.TableFlags.Borders) then
        GUI.tableSetupColumn("Name")
        GUI.tableSetupColumn("Value")
        GUI.tableHeadersRow()
        GUI.tableNextRow()
        GUI.tableSetColumnIndex(0)
        GUI.text("Health")
        GUI.tableNextColumn()
        GUI.text("100")
        GUI.endTable()
        end
        GUI.endWindow()
end
```

### GUI.tableGetColumnIndex

`tableGetColumnIndex():integer Current zero-based index or column count.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.beginTable("Stats", 2, GUI.TableFlags.Borders) then
        GUI.tableSetupColumn("Name")
        GUI.tableSetupColumn("Value")
        GUI.tableHeadersRow()
        GUI.tableNextRow()
        GUI.tableSetColumnIndex(0)
        GUI.text("Health")
        GUI.tableNextColumn()
        GUI.text("100")
        GUI.endTable()
        end
        GUI.endWindow()
end
```

### GUI.tableGetColumnCount

`tableGetColumnCount():integer Current zero-based index or column count.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.beginTable("Stats", 2, GUI.TableFlags.Borders) then
        GUI.tableSetupColumn("Name")
        GUI.tableSetupColumn("Value")
        GUI.tableHeadersRow()
        GUI.tableNextRow()
        GUI.tableSetColumnIndex(0)
        GUI.text("Health")
        GUI.tableNextColumn()
        GUI.text("100")
        GUI.endTable()
        end
        GUI.endWindow()
end
```

### GUI.beginMenu

`beginMenu(label:string,enabled?:boolean):boolean Only end when true.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.beginMenu("Actions") then
        if GUI.menuItem("Save") then debug.log("Save") end
        GUI.endMenu()
        end
        GUI.endWindow()
end
```

### GUI.endMenu

`endMenu() End successful beginMenu.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.beginMenu("Actions") then
        if GUI.menuItem("Save") then debug.log("Save") end
        GUI.endMenu()
        end
        GUI.endWindow()
end
```

### GUI.beginMenuBar

`beginMenuBar():boolean Requires GUI.WindowFlags.MenuBar on beginWindow. Only end when true.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.beginMenuBar() then
        if GUI.beginMenu("Actions") then
        if GUI.menuItem("Save") then debug.log("Save") end
        GUI.endMenu()
        end
        GUI.endMenuBar()
        end
        GUI.endWindow()
end
```

### GUI.endMenuBar

`endMenuBar() End successful beginMenuBar.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.beginMenuBar() then GUI.endMenuBar() end
        GUI.endWindow()
end
```

### GUI.menuItem

`menuItem(label:string,selected?:boolean,enabled?:boolean):boolean Returns clicked.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.beginMenu("Actions") then
        if GUI.menuItem("Save") then debug.log("Save") end
        GUI.endMenu()
        end
        GUI.endWindow()
end
```

### GUI.openPopup

`openPopup(id:string) Open a named popup.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.button("Open") then GUI.openPopup("Info") end
        if GUI.beginPopup("Info") then
        GUI.text("Information")
        if GUI.button("Close") then GUI.closeCurrentPopup() end
        GUI.endPopup()
        end
        GUI.endWindow()
end
```

### GUI.beginPopup

`beginPopup(id:string):boolean Only endPopup when true.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.button("Open") then GUI.openPopup("Info") end
        if GUI.beginPopup("Info") then
        GUI.text("Information")
        if GUI.button("Close") then GUI.closeCurrentPopup() end
        GUI.endPopup()
        end
        GUI.endWindow()
end
```

### GUI.beginPopupModal

`beginPopupModal(id:string):boolean Only endPopup when true.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.button("Open") then GUI.openPopup("Info") end
        if GUI.beginPopup("Info") then
        GUI.text("Information")
        if GUI.button("Close") then GUI.closeCurrentPopup() end
        GUI.endPopup()
        end
        GUI.endWindow()
end
```

### GUI.endPopup

`endPopup() Popup scope operation.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.button("Open") then GUI.openPopup("Info") end
        if GUI.beginPopup("Info") then
        GUI.text("Information")
        if GUI.button("Close") then GUI.closeCurrentPopup() end
        GUI.endPopup()
        end
        GUI.endWindow()
end
```

### GUI.closeCurrentPopup

`closeCurrentPopup() Popup scope operation.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.button("Open") then GUI.openPopup("Info") end
        if GUI.beginPopup("Info") then
        GUI.text("Information")
        if GUI.button("Close") then GUI.closeCurrentPopup() end
        GUI.endPopup()
        end
        GUI.endWindow()
end
```

### GUI.beginCombo

`beginCombo(label:string,preview:string):boolean Only endCombo when true.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.beginCombo("Choice", "First") then
        GUI.selectable("First")
        GUI.endCombo()
        end
        GUI.endWindow()
end
```

### GUI.endCombo

`endCombo() End successful beginCombo.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.beginCombo("Choice", "First") then
        GUI.selectable("First")
        GUI.endCombo()
        end
        GUI.endWindow()
end
```

### GUI.combo

`combo(label:string,index:integer,items:table):integer,boolean 1-based selected index and changed; 1..256 items.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        local index, changed = GUI.combo("Choice", 1, {"First", "Second"})
        GUI.endWindow()
end
```

### GUI.radioButton

`radioButton(label:string,selected:boolean):boolean Returns clicked.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.radioButton("First", true) then debug.log("First") end
        GUI.endWindow()
end
```

### GUI.pushStyleColor

`pushStyleColor(name:string|integer,r:number,g:number,b:number,a?:number) Use GUI.Col constants; pair with popStyleColor.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.pushStyleColor(GUI.Col.Text, 1, 0.8, 0.2, 1)
        GUI.text("Gold")
        GUI.popStyleColor()
        GUI.endWindow()
end
```

### GUI.popStyleColor

`popStyleColor(count?:integer) Strict reverse-order scoped pop; default 1.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.pushStyleVar(GUI.StyleVar.Alpha, 0.8)
        GUI.text("Styled")
        GUI.popStyleVar()
        GUI.endWindow()
end
```

### GUI.popStyleVar

`popStyleVar(count?:integer) Strict reverse-order scoped pop; default 1.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.pushStyleVar(GUI.StyleVar.Alpha, 0.8)
        GUI.text("Styled")
        GUI.popStyleVar()
        GUI.endWindow()
end
```

### GUI.pushStyleVar

`pushStyleVar(name:string|integer,x:number,y?:number) Use GUI.StyleVar constants. Values 0..1000.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.pushStyleVar(GUI.StyleVar.WindowPadding, 12, 12)
        GUI.text("Padded")
        GUI.popStyleVar()
        GUI.endWindow()
end
```

### GUI.pushFontSize

`pushFontSize(size:number) 8..96 pixels; pair with popFont.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.pushFontSize(24)
        GUI.text("Large")
        GUI.popFont()
        GUI.endWindow()
end
```

### GUI.popFont

`popFont() End font size scope.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.pushFontSize(24)
        GUI.text("Large")
        GUI.popFont()
        GUI.endWindow()
end
```

### GUI.beginDragDropSource

`beginDragDropSource():boolean Only end when true. Script payloads isolated from editor payloads.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.button("Drag")
        if GUI.beginDragDropSource() then
        GUI.setDragDropPayload("item", "sword")
        GUI.text("Sword")
        GUI.endDragDropSource()
        end
        GUI.button("Drop")
        if GUI.beginDragDropTarget() then
        local value = GUI.acceptDragDropPayload("item")
        if value then debug.log(value) end
        GUI.endDragDropTarget()
        end
        GUI.endWindow()
end
```

### GUI.beginDragDropTarget

`beginDragDropTarget():boolean Only end when true. Script payloads isolated from editor payloads.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.button("Drag")
        if GUI.beginDragDropSource() then
        GUI.setDragDropPayload("item", "sword")
        GUI.text("Sword")
        GUI.endDragDropSource()
        end
        GUI.button("Drop")
        if GUI.beginDragDropTarget() then
        local value = GUI.acceptDragDropPayload("item")
        if value then debug.log(value) end
        GUI.endDragDropTarget()
        end
        GUI.endWindow()
end
```

### GUI.endDragDropSource

`endDragDropSource() End successful drag/drop scope.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.button("Drag")
        if GUI.beginDragDropSource() then
        GUI.setDragDropPayload("item", "sword")
        GUI.text("Sword")
        GUI.endDragDropSource()
        end
        GUI.button("Drop")
        if GUI.beginDragDropTarget() then
        local value = GUI.acceptDragDropPayload("item")
        if value then debug.log(value) end
        GUI.endDragDropTarget()
        end
        GUI.endWindow()
end
```

### GUI.endDragDropTarget

`endDragDropTarget() End successful drag/drop scope.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.button("Drag")
        if GUI.beginDragDropSource() then
        GUI.setDragDropPayload("item", "sword")
        GUI.text("Sword")
        GUI.endDragDropSource()
        end
        GUI.button("Drop")
        if GUI.beginDragDropTarget() then
        local value = GUI.acceptDragDropPayload("item")
        if value then debug.log(value) end
        GUI.endDragDropTarget()
        end
        GUI.endWindow()
end
```

### GUI.setDragDropPayload

`setDragDropPayload(kind:string,value:string) Source only. Type max 28 bytes; value max 4096.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.button("Drag")
        if GUI.beginDragDropSource() then
        GUI.setDragDropPayload("item", "sword")
        GUI.text("Sword")
        GUI.endDragDropSource()
        end
        GUI.button("Drop")
        if GUI.beginDragDropTarget() then
        local value = GUI.acceptDragDropPayload("item")
        if value then debug.log(value) end
        GUI.endDragDropTarget()
        end
        GUI.endWindow()
end
```

### GUI.acceptDragDropPayload

`acceptDragDropPayload(kind:string):string? Target only. Returns delivered matching script payload or nil.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.button("Drag")
        if GUI.beginDragDropSource() then
        GUI.setDragDropPayload("item", "sword")
        GUI.text("Sword")
        GUI.endDragDropSource()
        end
        GUI.button("Drop")
        if GUI.beginDragDropTarget() then
        local value = GUI.acceptDragDropPayload("item")
        if value then debug.log(value) end
        GUI.endDragDropTarget()
        end
        GUI.endWindow()
end
```

### GUI.getMousePos

`getMousePos():number,number Returns Game-surface mouse x,y.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        local x, y = GUI.getMousePos()
        GUI.text(tostring(x))
        GUI.endWindow()
end
```

### GUI.isMouseDown

`isMouseDown(button:integer):boolean Mouse button 0..4.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.isMouseClicked(0) then debug.log("Click") end
        GUI.endWindow()
end
```

### GUI.isMouseClicked

`isMouseClicked(button:integer):boolean Mouse button 0..4.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.isMouseClicked(0) then debug.log("Click") end
        GUI.endWindow()
end
```

### GUI.isMouseReleased

`isMouseReleased(button:integer):boolean Mouse button 0..4.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.isMouseClicked(0) then debug.log("Click") end
        GUI.endWindow()
end
```

### GUI.isKeyDown

`isKeyDown(key:string):boolean Named key: A..Z, 0..9, Space, Enter, Escape, Tab, Backspace, Delete, arrows.`

- `isKeyDown(keyNameOrScancode) -> boolean`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.isKeyDown("Space") then GUI.text("Space") end
        GUI.endWindow()
end
```

### GUI.isKeyReleased

`isKeyReleased(key:string):boolean Named key: A..Z, 0..9, Space, Enter, Escape, Tab, Backspace, Delete, arrows.`

- `isKeyReleased(keyNameOrScancode) -> boolean`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.isKeyDown("Space") then GUI.text("Space") end
        GUI.endWindow()
end
```

### GUI.isKeyPressed

`isKeyPressed(key:string,repeatKey?:boolean):boolean Named key, optional repeat.`

- `isKeyPressed(keyNameOrScancode) -> boolean`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.isKeyPressed("Space") then debug.log("Space") end
        GUI.endWindow()
end
```

### GUI.drawLine

`drawLine(x:number,y:number,x2:number,y2:number,r:number,g:number,b:number,a?:number,width?:number) Screen-pixel coordinates; colors 0..1.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.drawLine(20, 20, 100, 100, 1, 1, 1, 1, 2)
        GUI.endWindow()
end
```

### GUI.drawRect

`drawRect(x:number,y:number,width:number,height:number,r:number,g:number,b:number,a?:number,rounding?:number,thickness?:number) Screen-pixel coordinates; colors 0..1.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.drawRect(20, 20, 100, 60, 1, 1, 1, 1)
        GUI.endWindow()
end
```

### GUI.drawRectFilled

`drawRectFilled(x:number,y:number,width:number,height:number,r:number,g:number,b:number,a?:number,rounding?:number,thickness?:number) Screen-pixel coordinates; colors 0..1.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.drawRect(20, 20, 100, 60, 1, 1, 1, 1)
        GUI.endWindow()
end
```

### GUI.drawCircle

`drawCircle(x:number,y:number,radius:number,r:number,g:number,b:number,a?:number,thickness?:number) Screen-pixel coordinates; radius 0..100000.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.drawCircle(80, 80, 30, 1, 0, 0, 1)
        GUI.endWindow()
end
```

### GUI.drawCircleFilled

`drawCircleFilled(x:number,y:number,radius:number,r:number,g:number,b:number,a?:number,thickness?:number) Screen-pixel coordinates; radius 0..100000.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.drawCircle(80, 80, 30, 1, 0, 0, 1)
        GUI.endWindow()
end
```

### GUI.drawText

`drawText(x:number,y:number,value:string,r:number,g:number,b:number,a?:number) Screen-pixel coordinates.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.drawText(20, 20, "Hello", 1, 1, 1, 1)
        GUI.endWindow()
end
```

### GUI.drawTriangleFilled

`drawTriangleFilled(x:number,y:number,x2:number,y2:number,x3:number,y3:number,r:number,g:number,b:number,a?:number) Screen-pixel coordinates.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.drawTriangleFilled(20, 20, 60, 80, 100, 20, 1, 0, 0, 1)
        GUI.endWindow()
end
```

### GUI.beginWindow

`beginWindow(title:string,flags?:integer):boolean Always call endWindow even when false.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.text("Window contents")
        GUI.endWindow()
end
```

### GUI.endWindow

`endWindow() End the current window.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.text("Window contents")
        GUI.endWindow()
end
```

### GUI.text

`text(value:string) Plain text, never a printf format.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.text("Hello, player!")
        GUI.endWindow()
end
```

### GUI.textWrapped

`textWrapped(value:string) Wrapped plain text.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.textWrapped("A longer message for the player.")
        GUI.endWindow()
end
```

### GUI.button

`button(label:string):boolean True on activation.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.button("Collect") then debug.log("Collected") end
        GUI.endWindow()
end
```

### GUI.smallButton

`smallButton(label:string):boolean Compact button.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.smallButton("Reset") then debug.log("Reset") end
        GUI.endWindow()
end
```

### GUI.checkbox

`checkbox(label:string,value:boolean):boolean,boolean Returns new value, changed.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        enabled, changed = GUI.checkbox("Enabled", enabled)
        GUI.endWindow()
end
```

### GUI.sliderFloat

`sliderFloat(label:string,value:number,min:number,max:number):number,boolean Returns value, changed.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        volume, changed = GUI.sliderFloat("Volume", volume, 0, 1)
        GUI.endWindow()
end
```

### GUI.sliderInt

`sliderInt(label:string,value:integer,min:integer,max:integer):integer,boolean Returns value, changed.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        count, changed = GUI.sliderInt("Count", count, 0, 10)
        GUI.endWindow()
end
```

### GUI.dragFloat

`dragFloat(label:string,value:number,speed?:number):number,boolean Returns value, changed; speed defaults 0.1.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        volume, changed = GUI.dragFloat("Value", volume, 0.1)
        GUI.endWindow()
end
```

### GUI.inputFloat

`inputFloat(label:string,value:number):number,boolean Returns value, changed.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        volume, changed = GUI.inputFloat("Value", volume)
        GUI.endWindow()
end
```

### GUI.inputInt

`inputInt(label:string,value:integer):integer,boolean Returns value, changed.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        count, changed = GUI.inputInt("Count", count)
        GUI.endWindow()
end
```

### GUI.inputText

`inputText(label:string,value:string):string,boolean Returns text, changed; max 4095 bytes.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        name, changed = GUI.inputText("Name", name)
        GUI.endWindow()
end
```

### GUI.colorEdit3

`colorEdit3(label:string,r:number,g:number,b:number):number,number,number,boolean Returns r,g,b,changed.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        r, g, b, changed = GUI.colorEdit3("Color", r, g, b)
        GUI.endWindow()
end
```

### GUI.colorEdit4

`colorEdit4(label:string,r:number,g:number,b:number,a:number):number,number,number,number,boolean Returns r,g,b,a,changed.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        r, g, b, a, changed = GUI.colorEdit4("Color", r, g, b, a)
        GUI.endWindow()
end
```

### GUI.separator

`separator() Horizontal separator.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.separator()
        GUI.endWindow()
end
```

### GUI.sameLine

`sameLine(offset?:number,spacing?:number) Continue on the same line.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.text("Label"); GUI.sameLine(); GUI.button("Action")
        GUI.endWindow()
end
```

### GUI.spacing

`spacing() Add vertical space.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.spacing()
        GUI.endWindow()
end
```

### GUI.newLine

`newLine() Start a line.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.newLine()
        GUI.endWindow()
end
```

### GUI.progressBar

`progressBar(fraction:number) Clamps fraction to 0..1.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.progressBar(0.75)
        GUI.endWindow()
end
```

### GUI.selectable

`selectable(label:string,selected?:boolean):boolean True when clicked.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.selectable("Item", false) then debug.log("Selected") end
        GUI.endWindow()
end
```

### GUI.collapsingHeader

`collapsingHeader(label:string):boolean True while expanded; no matching end.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        if GUI.collapsingHeader("Details") then GUI.text("More information") end
        GUI.endWindow()
end
```

### GUI.beginChild

`beginChild(id:string,width?:number,height?:number):boolean Always pair with endChild.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.beginChild("Content", 200, 100)
        GUI.text("Child contents")
        GUI.endChild()
        GUI.endWindow()
end
```

### GUI.endChild

`endChild() End child region.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.beginChild("Content", 200, 100)
        GUI.text("Child contents")
        GUI.endChild()
        GUI.endWindow()
end
```

### GUI.beginGroup

`beginGroup() Begin a layout group.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.beginGroup()
        GUI.text("Group")
        GUI.endGroup()
        GUI.endWindow()
end
```

### GUI.endGroup

`endGroup() End a layout group.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.beginGroup()
        GUI.text("Group")
        GUI.endGroup()
        GUI.endWindow()
end
```

### GUI.beginDisabled

`beginDisabled(disabled?:boolean) Default true; pair with endDisabled.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.beginDisabled(true)
        GUI.button("Unavailable")
        GUI.endDisabled()
        GUI.endWindow()
end
```

### GUI.endDisabled

`endDisabled() End disabled scope.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.beginDisabled(true)
        GUI.button("Unavailable")
        GUI.endDisabled()
        GUI.endWindow()
end
```

### GUI.pushID

`pushID(id:string) Scope repeated widget labels; pair with popID.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.pushID("item-1")
        GUI.button("Use")
        GUI.popID()
        GUI.endWindow()
end
```

### GUI.popID

`popID() Pop a widget ID.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.pushID("item-1")
        GUI.button("Use")
        GUI.popID()
        GUI.endWindow()
end
```

### GUI.setNextWindowPos

`setNextWindowPos(x:number,y:number) Game-surface pixels; call before beginWindow.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.setNextWindowPos(20, 20)
        GUI.beginWindow("HUD")
        GUI.text("Positioned window")
        GUI.endWindow()
end
```

### GUI.setNextWindowSize

`setNextWindowSize(width:number,height:number) Game-surface pixels; call before beginWindow.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.setNextWindowSize(300, 200)
        GUI.beginWindow("HUD")
        GUI.text("Sized window")
        GUI.endWindow()
end
```

### GUI.setCursorPos

`setCursorPos(x:number,y:number) Window-local content position.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.setCursorPos(20, 60)
        GUI.text("Positioned text")
        GUI.endWindow()
end
```

### GUI.getWindowSize

`getWindowSize():number,number Returns width,height.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        local w, h = GUI.getWindowSize()
        GUI.text(tostring(w) .. " x " .. tostring(h))
        GUI.endWindow()
end
```

### GUI.getContentRegionAvail

`getContentRegionAvail():number,number Returns available width,height.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        local w, h = GUI.getContentRegionAvail()
        GUI.text(tostring(w) .. " available")
        GUI.endWindow()
end
```

### GUI.setNextItemWidth

`setNextItemWidth(width:number) Width of next input.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.setNextItemWidth(180)
        name = GUI.inputText("Name", name)
        GUI.endWindow()
end
```

### GUI.isItemHovered

`isItemHovered():boolean Whether last widget is hovered.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.button("Help")
        if GUI.isItemHovered() then GUI.setTooltip("A helpful hint") end
        GUI.endWindow()
end
```

### GUI.isItemActive

`isItemActive():boolean Whether last widget is active.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.button("Hold")
        if GUI.isItemActive() then debug.log("Active") end
        GUI.endWindow()
end
```

### GUI.setTooltip

`setTooltip(value:string) Plain-text tooltip.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.button("Help")
        if GUI.isItemHovered() then GUI.setTooltip("A helpful hint") end
        GUI.endWindow()
end
```

### GUI.bulletText

`bulletText(value:string) Bulleted plain text.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        GUI.bulletText("Objective: find supplies")
        GUI.endWindow()
end
```

### GUI.getDisplaySize

`getDisplaySize():number,number Returns Game-surface pixel width,height.`

```lua
local enabled, changed = true, false
local volume, count, name = 0.5, 1, "Player"
local r, g, b, a = 1, 1, 1, 1
function onGUI()
        GUI.beginWindow("HUD")
        local w, h = GUI.getDisplaySize()
        GUI.text(tostring(w) .. " x " .. tostring(h))
        GUI.endWindow()
end
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
| `shape` | `'Box' / 'Sphere' / 'Capsule' / 'Cone' / 'Mesh'` |
| `model` | `string Static triangle-mesh .mesh source; empty follows the rendered model` |
| `meshDetail` | `integer Mesh detail 2..15 (default 8); 15 preserves source triangles` |
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
| `model` | `string Static .mesh asset path; setting mesh to a primitive clears this` |

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
| `cookie` | `string Project-relative PNG/JPG/JPEG/TGA/BMP; empty disables projection` |
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
| `hdrCompression` | `boolean Packed RGB9_E5 GPU HDR storage, default false; source asset unchanged` |
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
| `affectEnvironment` | `boolean Optional sky-colored ambient light; default false` |
| `environmentIntensity` | `number 0..5, default 1` |

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

### Animation fields

| Field | Type / meaning |
| --- | --- |
| `gameObject` | `GameObject` |
| `clip` | `string` |
| `loop` | `boolean` |
| `playOnStart` | `boolean` |
| `isPlaying` | `boolean Read-only` |
| `paused` | `boolean Read-only` |
| `time` | `number Read-only seconds` |
| `error` | `string Read-only loading error` |

### ParticleSystem fields

| Field | Type / meaning |
| --- | --- |
| `Point` | `integer Shape constant` |
| `Box` | `integer Shape constant` |
| `Sphere` | `integer Shape constant` |
| `Cone` | `integer Shape constant` |
| `Alpha` | `integer Blend constant` |
| `Additive` | `integer Blend constant` |
| `Fire` | `integer Sprite constant` |
| `Smoke` | `integer Sprite constant` |
| `Sparkles` | `integer Sprite constant` |
| `isPlaying` | `boolean Read-only` |
| `paused` | `boolean Read-only` |
| `particleCount` | `integer Read-only` |
| `time` | `number Read-only emitter cycle time` |
| `loop` | `boolean` |
| `playOnAwake` | `boolean` |
| `localSpace` | `boolean` |
| `collision` | `boolean Optional swept-point collider physics` |
| `restitution` | `number Bounce 0..1` |
| `collisionFriction` | `number Tangential velocity loss 0..1` |
| `maxParticles` | `integer` |
| `burstCount` | `integer` |
| `shape` | `integer` |
| `blend` | `integer` |
| `sprite` | `integer` |
| `seed` | `integer` |
| `useSpriteSheet` | `boolean Opt-in atlas animation; false by default` |
| `sheetColumns` | `integer` |
| `sheetRows` | `integer` |
| `color` | `table Assign a complete x/y/z vector table; returned tables are copies` |
| `endColor` | `table Assign a complete x/y/z vector table; returned tables are copies` |
| `direction` | `table Assign a complete x/y/z vector table; returned tables are copies` |
| `velocity` | `table Assign a complete x/y/z vector table; returned tables are copies` |
| `gravity` | `table Assign a complete x/y/z vector table; returned tables are copies` |
| `boxSize` | `table Assign a complete x/y/z vector table; returned tables are copies` |
| `duration` | `number` |
| `startDelay` | `number` |
| `rate` | `number` |
| `lifetimeMin` | `number` |
| `lifetimeMax` | `number` |
| `speedMin` | `number` |
| `speedMax` | `number` |
| `sizeMin` | `number` |
| `sizeMax` | `number` |
| `endSize` | `number` |
| `alpha` | `number` |
| `endAlpha` | `number` |
| `radius` | `number` |
| `coneAngle` | `number` |
| `drag` | `number` |
| `rotationMin` | `number` |
| `rotationMax` | `number` |
| `angularVelocity` | `number` |
| `frameRate` | `number` |
| `texture` | `string Empty selects bundled sprite; otherwise project-relative image` |

### Laser fields

| Field | Type / meaning |
| --- | --- |
| `enabled` | `boolean` |
| `color` | `Vec3 Assign a complete RGB vector table (copy on read)` |
| `intensity` | `number 0..100` |
| `range` | `number 0.01..10000 metres` |
| `width` | `number Core diameter 0.001..10 metres` |
| `glowWidth` | `number Outer diameter; at least width, at most 100 metres` |
| `impactSize` | `number Impact disc diameter 0.001..100 metres` |
| `hit` | `boolean Read-only; latest simulation query hit a collider` |
| `endPosition` | `Vec3 Read-only; latest beam endpoint in world space` |
| `hitNormal` | `Vec3 Read-only; valid only when hit is true` |

### LightShafts fields

| Field | Type / meaning |
| --- | --- |
| `enabled` | `boolean` |
| `direction` | `Vec3 Local emission direction; assign a complete nonzero vector` |
| `height` | `number 0.01..1000 metres` |
| `width` | `number Start width, 0.001..100 metres` |
| `endWidth` | `number End width, 0.001..500 metres` |
| `color` | `Vec3 RGB color; assign a complete vector` |
| `intensity` | `number 0..100` |
| `density` | `number 0..1` |
| `falloff` | `number Profile edge exponent, 0.1..8` |
| `streaks` | `number Profile streak frequency, 0..64` |
| `fade` | `number Profile start fade, 0.01..1` |
| `nearFade` | `number Camera near fade distance, 0.01..100 metres` |
| `useBakedTexture` | `boolean Use the cached profile if available, else procedural fallback` |
| `texture` | `string Project-relative profile image` |

### CameraEffects fields

| Field | Type / meaning |
| --- | --- |
| `enabled` | `boolean` |
| `bloom` | `boolean` |
| `colorGrading` | `boolean` |
| `vignette` | `boolean` |
| `depthOfField` | `boolean` |
| `motionBlur` | `boolean Camera-only reprojection, no object velocities` |
| `filmGrain` | `boolean` |
| `chromaticAberration` | `boolean` |
| `antiAliasing` | `boolean FXAA` |
| `ambientOcclusion` | `boolean Six-tap SSAO` |
| `nightVision` | `boolean Green or infrared-style luminance visualization` |
| `nausea` | `boolean Animated screen distortion` |
| `nightVisionMode` | `number 0 = green, 1 = infrared-style (not physical heat)` |
| `nightVisionIntensity` | `number 0..1` |
| `nauseaIntensity` | `number 0..1` |
| `nauseaSpeed` | `number 0..5` |
| `bloomIntensity` | `number 0..4` |
| `bloomThreshold` | `number 0..8` |
| `exposure` | `number -5..5 stops` |
| `contrast` | `number 0..2` |
| `saturation` | `number 0..2` |
| `temperature` | `number -1..1` |
| `vignetteIntensity` | `number 0..1` |
| `focusDistance` | `number 0.01..10000 metres` |
| `focusRange` | `number 0.01..10000 metres` |
| `dofStrength` | `number 0..1` |
| `motionStrength` | `number 0..1` |
| `grainIntensity` | `number 0..0.2` |
| `aberrationStrength` | `number 0..0.02` |
| `aoIntensity` | `number 0..2` |
| `aoRadius` | `number 0.01..5 metres` |

### Fog fields

| Field | Type / meaning |
| --- | --- |
| `enabled` | `boolean Global distance fog; first enabled active Fog in entity order wins` |
| `color` | `table RGB 0..1; assign a complete x/y/z or three-element table (read returns a copy)` |
| `density` | `number 0..10 inverse metres; zero is clear` |
| `start` | `number Distance before fog begins, 0 or greater and less than end` |
| `end` | `number Distance cap, greater than start and at most 1000000 metres; use fog["end"] in Lua` |

### CloudSystem fields

| Field | Type / meaning |
| --- | --- |
| `enabled` | `boolean` |
| `volumetric` | `boolean False = 2D procedural layer; true = bounded volumetric box` |
| `color` | `table RGB 0..1; assign a complete x/y/z or three-element table, read returns a copy` |
| `size` | `table XYZ extent 0.1..10000 metres, multiplied by Transform scale; read returns a copy` |
| `coverage` | `number 0..1` |
| `density` | `number 0..10` |
| `scale` | `number Noise frequency 0.001..10` |
| `speed` | `number Wind speed -100..100` |
| `opacity` | `number 0..1` |
| `steps` | `integer Volumetric raymarch budget 4..24, default 12` |

### River fields

| Field | Type / meaning |
| --- | --- |
| `enabled` | `boolean` |
| `color` | `table RGB 0..1; assign a complete vector, read returns a copy` |
| `width` | `number 0.05..1000 local units` |
| `opacity` | `number 0..1` |
| `speed` | `number Flow speed -100..100` |
| `waveScale` | `number 0.01..100` |
| `waveStrength` | `number 0..1 Shading only; does not displace geometry` |
| `foam` | `number 0..1 Procedural edge foam` |
| `subdivisions` | `integer Samples per curve span 2..32` |
| `points` | `table List of 2..64 complete local vector tables; getter returns copies, assign the whole list to change` |
| `shaderStyle` | `integer 0 Classic, 1 Clear, 2 Ocean, 3 Lake, 4 Rapids, 5 Tropical, 6 Marsh, 7 Industrial canal` |
| `waveHeight` | `number Geometric displacement 0..3 metres` |
| `waveLength` | `number 0.1..100 metres` |
| `waveSpeed` | `number 0..10 multiplier` |

### Vehicle fields

| Field | Type / meaning |
| --- | --- |
| `enabled` | `boolean` |
| `throttle` | `number -1..1, forward/reverse, runtime command` |
| `steering` | `number -1..1, positive right, runtime command` |
| `brake` | `number 0..1 service brake` |
| `handbrake` | `boolean Rear brake/sliding` |
| `speed` | `number Read-only signed forward m/s` |
| `groundedWheels` | `integer Read-only 0..4` |
| `isGrounded` | `boolean Read-only` |
| `wheelCompression` | `number[] Read-only copy, FL/FR/RL/RR, 0..1` |
| `wheelBase` | `number 0.2..20 local metres` |
| `trackWidth` | `number 0.2..10 local metres` |
| `mountHeight` | `number -10..10 local metres` |
| `wheelRadius` | `number 0.05..3 local metres` |
| `suspensionLength` | `number 0.05..3 local metres` |
| `springStrength` | `number 100..500000 N/m per wheel` |
| `centerOfMassX` | `number Local chassis COM offset X in metres, -10..10, default 0` |
| `centerOfMassY` | `number Local chassis COM offset Y in metres, -10..10, default 0; negative lowers COM` |
| `centerOfMassZ` | `number Local chassis COM offset Z in metres, -10..10, default 0` |
| `suspensionDamping` | `number 0..50000 per wheel` |
| `engineForce` | `number 0..200000 N total` |
| `brakeForce` | `number 0..300000 N total` |
| `tireGrip` | `number 0.05..5` |
| `maxSteerAngle` | `number 0..70 degrees` |
| `maxSpeed` | `number 1..150 m/s engine cutoff` |

### Water fields

| Field | Type / meaning |
| --- | --- |
| `mesh` | `string Optional .mesh asset; empty uses this MeshRenderer or default plane` |

### BoneAttachment fields

| Field | Type / meaning |
| --- | --- |
| `enabled` | `boolean` |
| `source` | `GameObject / nil Animated model owner` |
| `node` | `integer Skeleton node index (-1 unbound)` |
| `offset` | `Vec3 Bone-local position offset (assign complete vector)` |
| `rotation` | `Vec3 Euler-degree offset (assign complete vector)` |
| `error` | `string Read-only runtime diagnostic` |

### TPSCamera fields

| Field | Type / meaning |
| --- | --- |
| `enabled` | `boolean` |
| `target` | `GameObject / nil Target root; nil follows the Transform parent` |
| `offset` | `Vec3 World-space look-at offset; assign a complete vector` |
| `mouseLook` | `boolean Right mouse or locked cursor orbits during Play` |
| `lockMouse` | `boolean Default false; request cursor lock in focused Play; false releases TPS-owned lock` |
| `collision` | `boolean Sphere-sweep obstruction avoidance (requires colliders)` |
| `distance` | `number Desired distance, within minDistance..maxDistance` |
| `minDistance` | `number At least 0.05 metres` |
| `maxDistance` | `number At most 1000 metres` |
| `yaw` | `number Orbit yaw in degrees` |
| `pitch` | `number Orbit pitch in degrees, within minPitch..maxPitch` |
| `minPitch` | `number At least -89 degrees` |
| `maxPitch` | `number At most 89 degrees` |
| `sensitivity` | `number 0..10 degrees per mouse pixel` |
| `zoomSpeed` | `number 0..20 metres per wheel unit` |
| `smoothing` | `number 0..100 response, zero snaps` |
| `shoulder` | `number -10..10 metres, local screen-right` |
| `collisionRadius` | `number 0.02..5 metres; near-plane coverage can enlarge this` |
| `resolvedDistance` | `number Read-only obstruction-adjusted distance` |

### NavMeshAgent fields

| Field | Type / meaning |
| --- | --- |
| `enabled` | `boolean` |
| `speed` | `number Movement speed, 0..100 metres/second` |
| `stoppingDistance` | `number 0.01..10 metres` |
| `stopped` | `boolean Pause movement without clearing the route` |
| `destination` | `Vec3 Assign a complete world-space destination, or call setDestination` |
| `velocity` | `Vec3 Read-only actual last-frame velocity` |
| `hasPath` | `boolean Read-only; true while following a complete reachable path` |
| `pathPending` | `boolean Read-only; destination request awaits the next Play update` |
| `remainingDistance` | `number Read-only remaining path distance` |
| `status` | `string Read-only navigation status` |
| `updateRotation` | `boolean Automatically face the navigation path; default true` |
| `angularSpeed` | `number Degrees per second, 0..3600` |

### NavMeshObstacle fields

| Field | Type / meaning |
| --- | --- |
| `enabled` | `boolean` |
| `size` | `Vec3 Local box dimensions, positive; assign a whole vector` |
| `center` | `Vec3 Local box center; assign a whole vector` |

### UI fields

| Field | Type / meaning |
| --- | --- |
| `enabled` | `boolean Show this UI when its owner is active; defaults to true.` |
| `rml` | `string Project-relative .rml asset; both rml and rcss must be assigned.` |
| `rcss` | `string Project-relative .rcss asset; applied to this document.` |

### GUI fields

| Field | Type / meaning |
| --- | --- |
| `WindowFlags` | `GUIWindowFlags` |
| `TableFlags` | `GUITableFlags` |
| `Col` | `GUIColors` |
| `StyleVar` | `GUIStyleVars` |

### GUIWindowFlags fields

| Field | Type / meaning |
| --- | --- |
| `NoTitleBar` | `integer` |
| `NoResize` | `integer` |
| `NoMove` | `integer` |
| `NoScrollbar` | `integer` |
| `NoCollapse` | `integer` |
| `AlwaysAutoResize` | `integer` |
| `NoBackground` | `integer` |
| `MenuBar` | `integer` |
| `NoInputs` | `integer` |
| `NoNav` | `integer` |
| `NoDecoration` | `integer` |
| `NoFocusOnAppearing` | `integer` |
| `NoBringToFrontOnFocus` | `integer` |

### GUITableFlags fields

| Field | Type / meaning |
| --- | --- |
| `Borders` | `integer` |
| `RowBg` | `integer` |
| `Resizable` | `integer` |
| `Reorderable` | `integer` |
| `Hideable` | `integer` |
| `SizingStretchProp` | `integer` |
| `ScrollY` | `integer` |

### GUIStyleVars fields

| Field | Type / meaning |
| --- | --- |
| `Alpha` | `integer` |
| `DisabledAlpha` | `integer` |
| `WindowPadding` | `integer` |
| `WindowRounding` | `integer` |
| `WindowBorderSize` | `integer` |
| `WindowMinSize` | `integer` |
| `WindowTitleAlign` | `integer` |
| `ChildRounding` | `integer` |
| `ChildBorderSize` | `integer` |
| `PopupRounding` | `integer` |
| `PopupBorderSize` | `integer` |
| `FramePadding` | `integer` |
| `FrameRounding` | `integer` |
| `FrameBorderSize` | `integer` |
| `ItemSpacing` | `integer` |
| `ItemInnerSpacing` | `integer` |
| `IndentSpacing` | `integer` |
| `CellPadding` | `integer` |
| `ScrollbarSize` | `integer` |
| `ScrollbarRounding` | `integer` |
| `GrabMinSize` | `integer` |
| `GrabRounding` | `integer` |
| `TabRounding` | `integer` |
| `ButtonTextAlign` | `integer` |
| `SelectableTextAlign` | `integer` |

### GUIColors fields

| Field | Type / meaning |
| --- | --- |
| `Text` | `integer` |
| `TextDisabled` | `integer` |
| `WindowBg` | `integer` |
| `ChildBg` | `integer` |
| `PopupBg` | `integer` |
| `Border` | `integer` |
| `BorderShadow` | `integer` |
| `FrameBg` | `integer` |
| `FrameBgHovered` | `integer` |
| `FrameBgActive` | `integer` |
| `TitleBg` | `integer` |
| `TitleBgActive` | `integer` |
| `TitleBgCollapsed` | `integer` |
| `MenuBarBg` | `integer` |
| `ScrollbarBg` | `integer` |
| `ScrollbarGrab` | `integer` |
| `ScrollbarGrabHovered` | `integer` |
| `ScrollbarGrabActive` | `integer` |
| `CheckMark` | `integer` |
| `SliderGrab` | `integer` |
| `SliderGrabActive` | `integer` |
| `Button` | `integer` |
| `ButtonHovered` | `integer` |
| `ButtonActive` | `integer` |
| `Header` | `integer` |
| `HeaderHovered` | `integer` |
| `HeaderActive` | `integer` |
| `Separator` | `integer` |
| `SeparatorHovered` | `integer` |
| `SeparatorActive` | `integer` |
| `ResizeGrip` | `integer` |
| `ResizeGripHovered` | `integer` |
| `ResizeGripActive` | `integer` |
| `InputTextCursor` | `integer` |
| `TabHovered` | `integer` |
| `Tab` | `integer` |
| `TabSelected` | `integer` |
| `TabSelectedOverline` | `integer` |
| `TabDimmed` | `integer` |
| `TabDimmedSelected` | `integer` |
| `TabDimmedSelectedOverline` | `integer` |
| `DockingPreview` | `integer` |
| `DockingEmptyBg` | `integer` |
| `PlotLines` | `integer` |
| `PlotLinesHovered` | `integer` |
| `PlotHistogram` | `integer` |
| `PlotHistogramHovered` | `integer` |
| `TableHeaderBg` | `integer` |
| `TableBorderStrong` | `integer` |
| `TableBorderLight` | `integer` |
| `TableRowBg` | `integer` |
| `TableRowBgAlt` | `integer` |
| `TextLink` | `integer` |
| `TextSelectedBg` | `integer` |
| `TreeLines` | `integer` |
| `DragDropTarget` | `integer` |
| `DragDropTargetBg` | `integer` |
| `UnsavedMarker` | `integer` |
| `NavCursor` | `integer` |
| `NavWindowingHighlight` | `integer` |
| `NavWindowingDimBg` | `integer` |
| `ModalWindowDimBg` | `integer` |
| `COUNT` | `integer` |

### Decal fields

| Field | Type / meaning |
| --- | --- |
| `enabled` | `boolean` |
| `texture` | `string` |
| `color` | `Vec3 Tint, read as a copy; assign the whole vector to update.` |
| `width` | `number` |
| `height` | `number` |
| `depth` | `number` |
| `opacity` | `number` |
| `sortingOrder` | `integer` |

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

## Vehicle component

See [Vehicle physics and scripting](vehicles.md) for setup, tuning ranges and a
complete driving example. Use `self:getComponent(Vehicle)` or
`self:addComponent(Vehicle)`. Both the type token and string `"Vehicle"` work.

Writable commands: `throttle` (-1..1), `steering` (-1..1, positive right), `brake`
(0..1), `handbrake` (boolean). Read-only state: `speed` (signed m/s),
`groundedWheels`, `isGrounded`, `wheelCompression` (copied FL/FR/RL/RR array).
All vehicle configuration is writable through the same property names shown in
the Inspector; runtime commands/state are not serialized.


## Water and BoneAttachment components

[Water meshes and bone attachments](water-and-attachments.md) documents both new
component APIs. Water shares River fields and adds `mesh`; both support
`shaderStyle` (0..7), `waveHeight`, `waveLength` and `waveSpeed`.
BoneAttachment exposes `enabled`, `source` (GameObject), `node` (index), `offset`
and `rotation` (complete vector assignments), plus read-only `error`. Bone pose
evaluation drives its owner's Transform; a dynamic RigidBody is not supported.


## TPS Camera and skybox ambient lighting

The [TPS/environment guide](third-person-and-environment.md) documents `TPSCamera`
settings, target/parent following, orbit input and collision limits. TPSCamera is
available through component lookup/add/remove/find/export APIs; `target` is a
GameObject reference, `offset` is assigned as a complete vector and
`resolvedDistance` is read-only. Camera projection/activation remains on Camera.
`Skybox.affectEnvironment` enables sampled sky-colored ambient light and
`Skybox.environmentIntensity` sets 0..5 intensity. Vehicle suspension uses
independent spring/damper contacts. Anti-roll strength has been removed;
`centerOfMassX/Y/Z` remain available without locking chassis pitch/roll.


See [In-game UI](game-ui.md) for RML/RCSS setup and the `UI` component example.
