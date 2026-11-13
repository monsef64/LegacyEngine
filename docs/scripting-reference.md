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
[level storage](projects.md#levels), and [skybox transitions](skybox.md#smooth-scripted-crossfades).

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
- `debug.error()` logs without throwing. Memory/instruction limits still apply.

The sandbox also exposes Lua 5.4 base, table, string, math and utf8 computation libraries.
It does not expose filesystem/process/module loading, coroutines, pcall/xpcall or the
standard introspection debug library. Those standard computation-library APIs are not
engine functions; this reference covers the engine-owned API.

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

`destroyGameObject(gameObject)`

- `param gameObject GameObject`

```lua
local object = createGameObject("Temporary")
destroyGameObject(object)
```

### getComponent

`getComponent(gameObject, Type) / object.getComponent(Type) -> live component or nil`

- `param gameObject GameObject`
- `param componentType ComponentType`
- `overload fun(gameObject:GameObject,componentType:'Transform'):Transform|nil`
- `overload fun(gameObject:GameObject,componentType:'RigidBody'):RigidBody|nil`
- `overload fun(gameObject:GameObject,componentType:'Collider'|'BoxCollider'):Collider|nil`
- `overload fun(gameObject:GameObject,componentType:'MeshRenderer'):MeshRenderer|nil`
- `overload fun(gameObject:GameObject,componentType:'ObjectName'):ObjectName|nil`
- `return Transform|RigidBody|Collider|MeshRenderer|ObjectName|LuaScript|nil`

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

### print

`print(...) - log tab-separated values`

```lua
print("Player position", self:getPosition().x)
```

## GameObject

### GameObject.getComponent

`getComponent(componentType:ComponentType):Transform|RigidBody|Collider|MeshRenderer|ObjectName|LuaScript|nil`

- `getComponent(gameObject, Type) / object.getComponent(Type) -> live component or nil`

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

- `object:destroy()`

```lua
local object = self
object:destroy()
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

- `camera:setActive(enabled=true)`

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

`setType(self:Skybox,type:string|integer) Textures or HDR, or 0/1`

- `sky:setType('Textures'|'HDR') - six-face or HDR skybox`

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

### Skybox.setHDR

`setHDR(self:Skybox,texture:Texture|nil)`

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
| `type` | `integer 0 six face textures, 1 HDR panorama` |
| `enabled` | `boolean` |
| `color` | `Vector3 Color tint` |
| `right` | `Texture +X` |
| `left` | `Texture -X` |
| `top` | `Texture +Y` |
| `bottom` | `Texture -Y` |
| `front` | `Texture -Z` |
| `back` | `Texture +Z` |
| `hdr` | `Texture Radiance .hdr equirectangular panorama` |

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
