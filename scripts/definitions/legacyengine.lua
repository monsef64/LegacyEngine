---@meta
-- Editor-only LuaLS/EmmyLua definitions. Do not execute this file.
---@class Vec3
---@field x number
---@field y number
---@field z number
---@class Quaternion: Vec3
---@field w number
---@class ComponentToken
---@alias ComponentType ComponentToken|'Transform'|'RigidBody'|'Collider'|'BoxCollider'|'MeshRenderer'|'ObjectName'|'LuaScript'|'Camera'|'AudioSource'|'AudioListener'|'CharacterController'|'FPSCamera'|'Animation'|'Animator'|'ParticleSystem'|'Laser'|'SpotLight'|'LightShafts'|'CameraEffects'|'Fog'|'CloudSystem'|'River'|'Vehicle'|'Water'|'BoneAttachment'|'TPSCamera'|'NavMeshAgent'|'Decal'|'Recoil'
---Live named script proxy. Public globals accept scalar values/functions; see scripting guide.
---@class NamedScriptComponent
---@class GameObject
---@field getScript fun(name:string):NamedScriptComponent|nil
---@field name string
---@field parent GameObject|nil
---@field getComponent fun(componentType:ComponentType|string):NamedScriptComponent|Transform|RigidBody|Collider|MeshRenderer|ObjectName|LuaScript|Animation|Animator|ParticleSystem|Laser|LightShafts|CameraEffects|Fog|CloudSystem|River|Vehicle|Water|BoneAttachment|TPSCamera|NavMeshAgent|Decal|nil
---@field getComponentInParent fun(componentType:ComponentType):Transform|RigidBody|Collider|MeshRenderer|ObjectName|LuaScript|Animation|Animator|ParticleSystem|Laser|LightShafts|CameraEffects|Fog|CloudSystem|River|Vehicle|Water|BoneAttachment|TPSCamera|NavMeshAgent|Decal|nil
---@field getComponentInChildren fun(componentType:ComponentType):Transform|RigidBody|Collider|MeshRenderer|ObjectName|LuaScript|Animation|Animator|ParticleSystem|Laser|LightShafts|CameraEffects|Fog|CloudSystem|River|Vehicle|Water|BoneAttachment|TPSCamera|NavMeshAgent|Decal|nil
---@field findComponentInParent fun(componentType:ComponentType):Transform|RigidBody|Collider|MeshRenderer|ObjectName|LuaScript|Animation|Animator|ParticleSystem|Laser|LightShafts|CameraEffects|Fog|CloudSystem|River|Vehicle|Water|BoneAttachment|TPSCamera|NavMeshAgent|Decal|nil
---@field findComponentInChildren fun(componentType:ComponentType):Transform|RigidBody|Collider|MeshRenderer|ObjectName|LuaScript|Animation|Animator|ParticleSystem|Laser|LightShafts|CameraEffects|Fog|CloudSystem|River|Vehicle|Water|BoneAttachment|TPSCamera|NavMeshAgent|Decal|nil
---@field addComponent fun(componentType:ComponentType):Transform|RigidBody|Collider|MeshRenderer|ObjectName|LuaScript|Animation|Animator|ParticleSystem|Laser|LightShafts|CameraEffects|Fog|CloudSystem|River|Vehicle|Water|BoneAttachment|TPSCamera|NavMeshAgent|Decal
---@field removeComponent fun(componentType:ComponentType)
---@field onAnimationFinish fun()|nil
---@field onAnimationStart fun()|nil
---@field onAnimationStopped fun()|nil
---@field onAnimationPause fun()|nil
---@field onAnimationResume fun()|nil
---@field onAnimationRestarted fun()|nil
---@field onGUI fun()|nil Draw GUI once per rendered Game frame; no wait/task
---@field onStart fun()|nil
---@field onUpdate fun(deltaTime:number)|nil
---@field onFixedUpdate fun(deltaTime:number)|nil
---@field onDestroy fun()|nil
---@field onMouseDown fun()|nil
---@field onMouseUp fun()|nil
---@field onMouseEnter fun()|nil
---@field onMouseExit fun()|nil
---@field onMouseOver fun()|nil
---@field onAudioStarted fun()|nil
---@field onAudioFinished fun()|nil
---@field onAudioPaused fun()|nil
---@field onAudioResumed fun()|nil
---@field onAudioStopped fun()|nil
---@class Transform
---@field position Vec3
---@field rotation Quaternion
---@field scale Vec3
---@class RigidBody
---@field mass number
---@field useGravity boolean
---@field linearVelocity Vec3
---@field angularVelocity Vec3
---@class Collider
---@field shape 'Box'|'Sphere'|'Capsule'|'Cone'|'Mesh'
---@field model string Static triangle-mesh .mesh source; empty follows the rendered model
---@field meshDetail integer Mesh detail 2..15 (default 8); 15 preserves source triangles
---@field size Vec3
---@field center Vec3
---@field radius number
---@field height number
---@field friction number
---@field bounciness number
-- Texture exports are project-relative PNG/JPEG/BMP/TGA/HDR asset paths, not loaders.
---@alias Texture string
---@class MeshRenderer
---@field castShadow boolean Cast realtime/baked shadows; default true. Rebake static lighting after changes.
---@field material string Project-relative .mat asset path; empty uses inline Diffuse
---@field setMaterial fun(self:MeshRenderer,path:string) Assign an asset, reset instance overrides
---@field setMaterialColor fun(self:MeshRenderer,r:number,g:number,b:number)
---@field setMaterialTexture fun(self:MeshRenderer,texture:Texture|nil)
---@field setMaterialNormalMap fun(self:MeshRenderer,texture:Texture|nil)
---@field setMaterialShader fun(self:MeshRenderer,shader:string) Diffuse, Diffuse Bumped, Diffuse Transparent, Diffuse Reflection or Stochastic Texture Sampling
---@field setMaterialNormalStrength fun(self:MeshRenderer,value:number)
---@field setMaterialTiling fun(self:MeshRenderer,x:number,y:number)
---@field setMaterialAlpha fun(self:MeshRenderer,value:number) 0..1 opacity for Diffuse Transparent
---@field setMaterialSpecular fun(self:MeshRenderer,value:number)
---@field resetMaterialOverrides fun(self:MeshRenderer)
---@field mesh 'Cube'|'Sphere'|'Capsule'|'Cone'|'Plane'
---@field color Vec3
---@field checkerboard boolean
---@field specular number
---@class ObjectName
---@field value string

function onStart() end
---Draw GUI only in this callback; called each rendered Game frame during Play.
function onGUI() end
---@param deltaTime number Seconds since the previous frame (capped at 0.25).
function onUpdate(deltaTime) end
---@param deltaTime number Fixed physics interval, 1/60 seconds.
function onFixedUpdate(deltaTime) end
function onDestroy() end
---@param name string
---@return GameObject
function createGameObject(name) end
---@param gameObject GameObject
function destroyGameObject(gameObject) end
---@param gameObject GameObject
---@param componentType ComponentType
---@overload fun(gameObject:GameObject,componentType:'Transform'):Transform|nil
---@overload fun(gameObject:GameObject,componentType:'RigidBody'):RigidBody|nil
---@overload fun(gameObject:GameObject,componentType:'Collider'|'BoxCollider'):Collider|nil
---@overload fun(gameObject:GameObject,componentType:'MeshRenderer'):MeshRenderer|nil
---@overload fun(gameObject:GameObject,componentType:'ObjectName'):ObjectName|nil
---@return Transform|RigidBody|Collider|MeshRenderer|ObjectName|LuaScript|Animation|Animator|ParticleSystem|Laser|LightShafts|CameraEffects|Fog|CloudSystem|River|Vehicle|Water|BoneAttachment|TPSCamera|NavMeshAgent|Decal|nil
---@overload fun(componentType:ComponentType):any
function getComponent(gameObject,componentType) end
---@param gameObject GameObject
---@param componentType ComponentType
---@overload fun(gameObject:GameObject,componentType:'Transform'):Transform
---@overload fun(gameObject:GameObject,componentType:'RigidBody'):RigidBody
---@overload fun(gameObject:GameObject,componentType:'Collider'|'BoxCollider'):Collider
---@overload fun(gameObject:GameObject,componentType:'MeshRenderer'):MeshRenderer
---@overload fun(gameObject:GameObject,componentType:'ObjectName'):ObjectName
---@return Transform|RigidBody|Collider|MeshRenderer|ObjectName|LuaScript
function addComponent(gameObject,componentType) end
---@param gameObject GameObject
---@param componentType ComponentType
function removeComponent(gameObject,componentType) end
---@param name string
---@return GameObject|nil
function findGameObjectByName(name) end
---@param componentType ComponentType
---@return GameObject[]
function findGameObjectsByComponent(componentType) end

---@param child GameObject
---@param parent GameObject|nil
function setParent(child,parent) end
---@param child GameObject
---@return GameObject|nil
function getParent(child) end

-- Runtime-provided opaque component type tokens (strings remain supported).
---@type ComponentToken
Transform = nil
---@type ComponentToken
RigidBody = nil
---@type ComponentToken
Collider = nil
---@type ComponentToken
BoxCollider = nil
---@type ComponentToken
MeshRenderer = nil
---@type ComponentToken
ObjectName = nil

---@class LuaScript
---@field path string Relative asset path under Assets
---@field enabled boolean
---@type ComponentToken
LuaScript = nil
---@type GameObject
self = nil -- Provided in each attached Lua Script runtime.

---@class Camera
---@field active boolean
---@field orthographic boolean
---@field fieldOfView number Degrees
---@field nearClip number
---@field farClip number
---@field orthographicSize number Vertical size in world units
---@type ComponentToken
Camera = nil
---@param name string
---@return GameObject
function createCamera(name) end
---@param camera GameObject|nil
function setActiveCamera(camera) end
---@return GameObject|nil
function getActiveCamera() end
---@param camera GameObject
---@param position Vec3
function setCameraPosition(camera,position) end
---@param camera GameObject
---@param rotation Vec3|Quaternion Euler degrees or quaternion
function setCameraRotation(camera,rotation) end
---@param camera GameObject
---@param target Vec3|GameObject
function lookAt(camera,target) end
---@param camera GameObject
---@param degrees number
function setFieldOfView(camera,degrees) end
---@param camera GameObject
---@param mode 'Perspective'|'Orthographic'|'perspective'|'orthographic'
function setProjectionMode(camera,mode) end
---@param camera GameObject
---@param distance number
function setNearClipPlane(camera,distance) end
---@param camera GameObject
---@param distance number
function setFarClipPlane(camera,distance) end
---@param camera GameObject
---@param x number Normalized top-left origin
---@param y number
---@param width number
---@param height number
function setViewport(camera,x,y,width,height) end
---@param camera GameObject
---@param target Vec3|GameObject|nil
function setCameraTarget(camera,target) end
---@param camera GameObject
---@param target Vec3|GameObject|nil
---@param offset Vec3|nil World-space offset
function setFollowTarget(camera,target,offset) end
---@param camera GameObject
---@param intensity number World units
---@param duration number Seconds
function setCameraShake(camera,intensity,duration) end
---@param camera GameObject
---@param worldPosition Vec3
---@return Vec3 Normalized top-left screen x/y, depth 0 near to 1 far
function worldToScreen(camera,worldPosition) end
---@param camera GameObject
---@param screenPosition Vec3 Normalized x/y and depth
---@return Vec3
function screenToWorld(camera,screenPosition) end

-- Object-oriented constructors and methods. Existing global APIs remain valid.
---@class GameObject
---@field setActive fun(self:GameObject,enabled?:boolean) Set local activity (default true); inherited by children
---@field isActive fun(self:GameObject):boolean Effective activity including parents
---@field setPosition fun(self:GameObject,x:number|Vec3,y?:number,z?:number)
---@field getPosition fun(self:GameObject):Vec3
---@field setRotation fun(self:GameObject,x:number|Vec3|Quaternion,y?:number,z?:number,w?:number)
---@field getRotation fun(self:GameObject):Quaternion
---@field setScale fun(self:GameObject,x:number|Vec3,y?:number,z?:number)
---@field getScale fun(self:GameObject):Vec3
---@field translate fun(self:GameObject,x:number|Vec3,y?:number,z?:number)
---@field lookAt fun(self:GameObject,x:number|Vec3|GameObject,y?:number,z?:number)
---@field setParent fun(self:GameObject,parent:GameObject|nil)
---@field getParent fun(self:GameObject):GameObject|nil
---@field setName fun(self:GameObject,name:string)
---@field getName fun(self:GameObject):string
---@field destroy fun(self:GameObject)
---@class GameObjectConstructor
---@field new fun(name:string):GameObject
---@field find fun(name:string):GameObject|nil
---@type GameObjectConstructor
GameObject = nil
---@class CameraObject:GameObject
---@field setActive fun(self:CameraObject,enabled?:boolean)
---@field setFieldOfView fun(self:CameraObject,degrees:number)
---@field setProjectionMode fun(self:CameraObject,mode:string)
---@field setNearClipPlane fun(self:CameraObject,distance:number)
---@field setFarClipPlane fun(self:CameraObject,distance:number)
---@field setViewport fun(self:CameraObject,x:number,y:number,width:number,height:number)
---@field setTarget fun(self:CameraObject,target:GameObject|Vec3|number|nil,y?:number,z?:number)
---@field setFollowTarget fun(self:CameraObject,target:GameObject|Vec3|nil,offset:Vec3|number,y?:number,z?:number)
---@field setShake fun(self:CameraObject,intensity:number,duration:number)
---@field setCameraShake fun(self:CameraObject,intensity:number,duration:number) Alias of setShake
---@field setCameraTarget fun(self:CameraObject,target:GameObject|Vec3|nil) Alias of setTarget
---@field worldToScreen fun(self:CameraObject,x:Vec3|number,y?:number,z?:number):Vec3
---@field screenToWorld fun(self:CameraObject,x:Vec3|number,y?:number,z?:number):Vec3
---@class CameraConstructor:ComponentToken
---@field new fun(name:string):CameraObject
---@field getActive fun():CameraObject|nil
---@type CameraConstructor
Camera = nil

---@alias AudioClip string Project-relative WAV or MP3 path
---@class AudioSource
---@field clip AudioClip
---@field play fun(self:AudioSource)
---@field stop fun(self:AudioSource)
---@field pause fun(self:AudioSource)
---@field resume fun(self:AudioSource)
---@field setPitch fun(self:AudioSource,pitch:number):number
---@field fadeIn fun(self:AudioSource,seconds:number)
---@field fadeOut fun(self:AudioSource,seconds:number)
---@field setVolume fun(self:AudioSource,volume:number):number
---@field mute fun(self:AudioSource)
---@field unmute fun(self:AudioSource)
---@field setLooping fun(self:AudioSource,enabled:boolean):boolean
---@field setPan fun(self:AudioSource,pan:number):number
---@field setDistance fun(self:AudioSource,distance:number):number Maximum audible 3D range
---@field spatialBlend fun(self:AudioSource,value:number):number 0 = 2D, 1 = 3D
---@field setSpatialBlend fun(self:AudioSource,value:number):number
---@field isPlaying fun(self:AudioSource):boolean
---@field playOneShot fun(self:AudioSource,clip:AudioClip)
AudioSource = nil
---@param path string Project-relative WAV or MP3 asset
---@return AudioClip
function AudioClip(path) end
---@param owner? GameObject Defaults to attached self
---@param clip? AudioClip
---@return AudioSource
function createAudioSource(owner,clip) end

---@class AudioListener
---@field enabled boolean
---@field pause fun(self:AudioListener) Disable reception without stopping AudioSources
---@field start fun(self:AudioListener) Enable reception again
AudioListener = nil

-- Input snapshots are shared across attached scripts for the current render frame.
---@class Vec2
---@field x number
---@field y number
---@param key string|integer SDL key name or scancode
---@return boolean
function isKeyPressed(key) end
---@param key string|integer SDL key name or scancode
---@return boolean
function isKeyDown(key) end
---@param key string|integer SDL key name or scancode
---@return boolean
function isKeyReleased(key) end
---@param button string|integer left/middle/right or SDL button number
---@return boolean
function isMouseButtonPressed(button) end
---@param button string|integer left/middle/right or SDL button number
---@return boolean
function isMouseButtonDown(button) end
---@param button string|integer left/middle/right or SDL button number
---@return boolean
function isMouseButtonReleased(button) end
---@return Vec2
function getMousePosition() end
---@return Vec2
function getMouseDelta() end
---@return Vec2
function getMouseWheelDelta() end
---@param negativeButton string Project button name
---@param positiveButton string Project button name
---@return number
function getInputAxis(negativeButton,positiveButton) end
---@param leftButton string
---@param rightButton string
---@param downButton string
---@param upButton string
---@return Vec2
function getInputVector(leftButton,rightButton,downButton,upButton) end
---@param button string|integer SDL button name or enum value
---@return boolean
function isGamepadButtonPressed(button) end
---@param button string|integer SDL button name or enum value
---@return boolean
function isGamepadButtonDown(button) end
---@param axis string|integer SDL axis name or enum value
---@return number
function getGamepadAxis(axis) end
---@param stick? 'left'|'right'
---@return Vec2
function getGamepadStick(stick) end
---@param name string Defined in Project > Input Settings
---@return boolean
function isButtonPressed(name) end
---@param name string Defined in Project > Input Settings
---@return boolean
function isButtonReleased(name) end
---@param name string Defined in Project > Input Settings
---@return boolean
function isButtonDown(name) end

-- Restricted logging facade; not Lua's standard debug library.
---@class LegacyDebug
---@field log fun(...:any)
---@field error fun(...:any)
---@field warning fun(...:any)
---@type LegacyDebug
debug = nil

-- Dynamic lights: emission follows the owning Transform local -Z axis.
---@class DirectionalLight
---@field color Vec3
---@field intensity number
---@field enabled boolean
---@field shadows boolean
---@field range number
---@field innerAngle number
---@field outerAngle number
---@field shadowDistance number
---@field shadowBias number
---@field shadowQuality integer 0=Off, 1=Low (256), 2=Medium (512), 3=High (1024)
---@field setColor fun(self:DirectionalLight|PointLight|SpotLight,r:number,g:number,b:number)
---@field setIntensity fun(self:DirectionalLight|PointLight|SpotLight,value:number)
---@field setRange fun(self:DirectionalLight|PointLight|SpotLight,value:number)
---@field setEnabled fun(self:DirectionalLight|PointLight|SpotLight,enabled:boolean)
---@field setShadows fun(self:DirectionalLight|PointLight|SpotLight,enabled:boolean)
---@field setShadowQuality fun(self:DirectionalLight|PointLight|SpotLight,quality:integer)
---@field setSpotAngles fun(self:DirectionalLight|PointLight|SpotLight,inner:number,outer:number)
DirectionalLight = nil

---@class PointLight
---@field color Vec3
---@field intensity number
---@field enabled boolean
---@field shadows boolean
---@field range number
---@field innerAngle number
---@field outerAngle number
---@field shadowDistance number
---@field shadowBias number
---@field shadowQuality integer 0=Off, 1=Low (256), 2=Medium (512), 3=High (1024)
---@field setColor fun(self:DirectionalLight|PointLight|SpotLight,r:number,g:number,b:number)
---@field setIntensity fun(self:DirectionalLight|PointLight|SpotLight,value:number)
---@field setRange fun(self:DirectionalLight|PointLight|SpotLight,value:number)
---@field setEnabled fun(self:DirectionalLight|PointLight|SpotLight,enabled:boolean)
---@field setShadows fun(self:DirectionalLight|PointLight|SpotLight,enabled:boolean)
---@field setShadowQuality fun(self:DirectionalLight|PointLight|SpotLight,quality:integer)
---@field setSpotAngles fun(self:DirectionalLight|PointLight|SpotLight,inner:number,outer:number)
PointLight = nil

---@class SpotLight
---@field cookie string Project-relative PNG/JPG/JPEG/TGA/BMP; empty disables projection
---@field color Vec3
---@field intensity number
---@field enabled boolean
---@field shadows boolean
---@field range number
---@field innerAngle number
---@field outerAngle number
---@field shadowDistance number
---@field shadowBias number
---@field shadowQuality integer 0=Off, 1=Low (256), 2=Medium (512), 3=High (1024)
---@field setColor fun(self:DirectionalLight|PointLight|SpotLight,r:number,g:number,b:number)
---@field setIntensity fun(self:DirectionalLight|PointLight|SpotLight,value:number)
---@field setRange fun(self:DirectionalLight|PointLight|SpotLight,value:number)
---@field setEnabled fun(self:DirectionalLight|PointLight|SpotLight,enabled:boolean)
---@field setShadows fun(self:DirectionalLight|PointLight|SpotLight,enabled:boolean)
---@field setShadowQuality fun(self:DirectionalLight|PointLight|SpotLight,quality:integer)
---@field setSpotAngles fun(self:DirectionalLight|PointLight|SpotLight,inner:number,outer:number)
SpotLight = nil

-- Mouse commands return true on success; queries report native window/cursor state.
-- Locking uses relative motion and hides the cursor until unlocked.
---@return boolean
function lockCursor() end
---@return boolean
function unlockCursor() end
---@return boolean
function hideCursor() end
---@return boolean
function showCursor() end
---@return boolean
function isCursorVisible() end
---@return boolean
function isCursorLocked() end

---@class Skybox
---@field type integer 0 cubemap asset (legacy inline faces supported), 1 HDR panorama
---@field enabled boolean
---@field color Vector3 Color tint
---@field right Texture +X
---@field left Texture -X
---@field top Texture +Y
---@field bottom Texture -Y
---@field front Texture -Z
---@field back Texture +Z
---@field cubemap string Project-relative .cubemap asset
---@field hdr Texture Radiance .hdr equirectangular panorama
---@field setType fun(self:Skybox,type:string|integer) Cubemap or HDR, or 0/1 (Textures is a legacy alias)
---@field setColor fun(self:Skybox,r:number,g:number,b:number)
---@field setEnabled fun(self:Skybox,enabled:boolean)
---@field setCubemap fun(self:Skybox,path:string|nil) Set Cubemap mode and project-relative asset
---@field setHDR fun(self:Skybox,texture:Texture|nil)
---@field setFace fun(self:Skybox,face:string,texture:Texture|nil)
---@field fadeTo fun(self:Skybox,destination:GameObject|Skybox,speed:number) Smooth crossfade, duration = 1/speed seconds
---@field setTextures fun(self:Skybox,right:Texture,left:Texture,top:Texture,bottom:Texture,front:Texture,back:Texture)

---Request a deferred level switch. Returns acceptance, not completion.
---@param name? string
---@return boolean accepted
---@return string? error
function loadLevel(name) end
---Read the level in a worker; validate/apply at the next safe frame boundary.
---@param name? string
---@return boolean accepted
---@return string? error
function loadLevelAsync(name) end
---@param name? string
---@return boolean accepted
---@return string? error
function unloadLevel(name) end
---@return boolean accepted
---@return string? error
function restartLevel() end
---@return string? name
function getCurrentLevel() end
---@param name string
---@return boolean accepted
---@return string? error
function setCurrentLevel(name) end
---@return boolean accepted
---@return string? error
function nextLevel() end
---@return boolean accepted
---@return string? error
function previousLevel() end

---Subscribe to a custom queued event; listeners are removed when the runtime stops.
---@param eventName string
---@param callback function
---@return integer token
function on(eventName,callback) end
---@param token integer
---@return boolean removed
function off(token) end
---Queue an event for delivery between script updates. Payloads: nil, boolean, number, string.
---@param eventName string
---@vararg any
---@return boolean accepted
function emit(eventName,...) end
---@param name string
function onLevelLoaded(name) end
---@param name string
---@param error string
function onLevelFailedToLoad(name,error) end
---Smoothly blend from the active skybox. Speed must be positive; 0.5 takes 2 seconds.
---@param destination GameObject|Skybox
---@param speed number
---@overload fun(source:GameObject|Skybox,destination:GameObject|Skybox,speed:number)
function fadeSkybox(destination,speed) end

-- Targeted callbacks: attached scripts receive events for their owning GameObject.
function onMouseDown() end
function onMouseUp() end
function onMouseEnter() end
function onMouseExit() end
function onMouseOver() end
function onAudioStarted() end
function onAudioFinished() end
function onAudioPaused() end
function onAudioResumed() end
function onAudioStopped() end

---@class AudioEvent Opaque token identifying an AudioSource event, not a callable function
---@class AudioSource
---@field onAudioStarted AudioEvent
---@field onAudioFinished AudioEvent
---@field onAudioPaused AudioEvent
---@field onAudioResumed AudioEvent
---@field onAudioStopped AudioEvent

---Start an independent cooperative block now; waits suspend only that block.
---Call from a lifecycle/object callback or another task, not top-level/onDestroy/custom event listeners.
---Returns no handle/results. Pause/inactivity freeze waits; Stop/owner destruction cancels them.
---@param block fun()
function task(block) end

---Suspend this lifecycle callback/task without blocking the engine. Not supported at top-level/onDestroy/custom event listeners.
---@param seconds number Finite seconds from 0 to 86400; zero resumes on next update
function waitForSeconds(seconds) end
---Wait for playing-time seconds or the next audio/animation event; past events do not count.
---@param secondsOrEvent number|AudioEvent|ComponentEvent Finite 0..86400 seconds or a live audio/animation event token
function wait(secondsOrEvent) end

---Instantiate a prefab asset or copy a GameObject subtree. Returns the new root.
---@param source string|GameObject Project-relative .prefab path or live object
---@param position Vec3
---@param rotation Vec3|Quaternion Euler degrees or quaternion
---@return GameObject
function spawn(source,position,rotation) end
---Destroy a subtree now or after a delay in playing seconds.
---@param object GameObject
---@param delay? number Nonnegative, defaults to zero
function destroy(object,delay) end

---@class GameObject
---@field getForward fun(self:GameObject):Vec3 World-space local -Z axis
---@field getBackward fun(self:GameObject):Vec3 World-space local +Z axis
---@field getRight fun(self:GameObject):Vec3 World-space local +X axis
---@field getLeft fun(self:GameObject):Vec3 World-space local -X axis
---@field getUp fun(self:GameObject):Vec3 World-space local +Y axis
---@field getDown fun(self:GameObject):Vec3 World-space local -Y axis
---@class CharacterController
---@field speed number Metres per second; default 4.5
---@field sprintMultiplier number At least 1; default 1.7
---@field jumpSpeed number Upward launch velocity; default 6
---@field groundProbe number Distance below capsule feet; default 0.12
---@field canMove boolean Writable movement gate
---@field canJump boolean Writable jump gate
---@field canSprint boolean Writable sprint gate
---@field isGrounded boolean Read-only, updated at fixed physics steps
---@field isJumping boolean Read-only, airborne and ascending
---@field isMoving boolean Read-only, permitted nonzero movement command
---@field isSprinting boolean Read-only, moving with permitted sprint command
---@field move fun(self:CharacterController,x:number,z:number) Local horizontal direction; normalized above unit length; persists until replaced
---@field jump fun(self:CharacterController) Queue one jump; applied only when grounded and canJump
---@field sprint fun(self:CharacterController,enabled:boolean) Set sprint intent; omitted means true
---@type ComponentToken
CharacterController = nil
---@class FPSCamera: CameraObject
---@field fieldOfView number Inherited Camera field
---@field nearClip number Inherited Camera field
---@field farClip number Inherited Camera field
---@field orthographic boolean Inherited Camera field
---@field active boolean Inherited Camera field
---@field isFreecam boolean Writable; only unparented cameras can translate freely
---@field mouseLook boolean Writable; native mouse look while cursor is locked
---@field speed number Freecam metres per second
---@field sensitivity number Degrees per mouse pixel
---@field linkToCharacter fun(self:FPSCamera,controller:CharacterController|GameObject,x:number,y:number,z:number) Parent using local position offset
---@field setIsFreecam fun(self:FPSCamera,enabled:boolean)
---@field getIsFreecam fun(self:FPSCamera):boolean
---@type ComponentToken
FPSCamera = nil

---@class ColorPalette
---@field red Vec3
---@field green Vec3
---@field blue Vec3
---@field white Vec3
---@field black Vec3
---@field yellow Vec3
---@field cyan Vec3
---@field magenta Vec3
---@field gray Vec3
---@field orange Vec3
---@field purple Vec3
---@field pink Vec3
---@type ColorPalette
color = nil
---@class GameObject
---@field layer string Layer name, defaults to Default; assign an existing name or slot number
---@field onTriggerEnter fun(collider:Collider|nil)|nil
---@field onTriggerStay fun(collider:Collider|nil)|nil
---@field onTriggerExit fun(collider:Collider|nil)|nil
---@class Collider
---@field isTrigger boolean Non-solid overlap sensor
---@field gameObject GameObject Collider owner; inspect name, layer and components
---@field transform Transform Collider owner transform
---@class RigidBody
---@field velocity Vec3 Alias of linearVelocity
---@field drag number Nonnegative linear damping
---@field angularDrag number Nonnegative angular damping
---@field isKinematic boolean Move under script control rather than gravity/forces
---@field addForce fun(self:RigidBody,x:number,y:number,z:number) World force consumed on next physics step
---@field addImpulse fun(self:RigidBody,x:number,y:number,z:number) Instant world impulse at next physics step
---@field setVelocity fun(self:RigidBody,x:number,y:number,z:number)
---@field getVelocity fun(self:RigidBody):Vec3
---@field addVelocity fun(self:RigidBody,x:number,y:number,z:number)
---@field movePosition fun(self:RigidBody,x:number,y:number,z:number) Set world position
---@field moveRotation fun(self:RigidBody,x:number,y:number,z:number) Euler degrees, or quaternion table
---@field stop fun(self:RigidBody) Clear velocities and queued forces/impulses
---@class RaycastHit
---@field collider Collider
---@field transform Transform
---@field gameObject GameObject
---@field point Vec3 World hit point; sphere query uses center-directed surface point
---@field normal Vec3 World surface normal; zero when sphere origin is inside a collider
---@field distance number World distance from query origin
---@class Raycast
---@field type integer Raycast.Line, Raycast.LineOfSight or Raycast.Sphere
---@field distance number Positive ray range
---@field radius number Positive sphere overlap radius
---@field angle number Fan aperture in degrees, 0 to 360
---@field samples integer Fan rays, 2 to 181
---@field offset Vec3 Local origin offset
---@field includeTriggers boolean Whether sensors can be hit
---@field cast fun(self:Raycast):RaycastHit|nil Nearest matching hit
---@field castAll fun(self:Raycast):RaycastHit[] Matching distinct objects, nearest first
---@field setLayers fun(self:Raycast,names:table) Replace layer mask with an array of names; empty array selects none
---@class RaycastType
---@field Line integer
---@field LineOfSight integer
---@field Sphere integer
---@type RaycastType
Raycast = nil
---@param collider Collider|nil Other collider; nil if destroyed before delivery
function onTriggerEnter(collider) end
---@param collider Collider|nil
function onTriggerStay(collider) end
---@param collider Collider|nil
function onTriggerExit(collider) end

---@class GameObject
---@field onCollisionEnter fun(collider:Collider|nil)|nil
---@field onCollisionStay fun(collider:Collider|nil)|nil
---@field onCollisionExit fun(collider:Collider|nil)|nil
---@param collider Collider|nil Other solid collider; exposes gameObject and transform
function onCollisionEnter(collider) end
---@param collider Collider|nil
function onCollisionStay(collider) end
---@param collider Collider|nil
function onCollisionExit(collider) end

---@class MeshRenderer
---@field model string Static .mesh asset path; setting mesh to a primitive clears this

---Include self, then search nearest parents. Returns the component, or nil.
---@param gameObject GameObject
---@param componentType ComponentType
---@return any|nil
---@overload fun(componentType:ComponentType):any|nil
function getComponentInParent(gameObject,componentType) end
---Include self, then breadth-first descendants. Returns the component, or nil.
---@param gameObject GameObject
---@param componentType ComponentType
---@return any|nil
---@overload fun(componentType:ComponentType):any|nil
function getComponentInChildren(gameObject,componentType) end
---Alias of getComponentInParent; also accepts implicit attached self.
---@param gameObject GameObject
---@param componentType ComponentType
---@return any|nil
---@overload fun(componentType:ComponentType):any|nil
function findComponentInParent(gameObject,componentType) end
---Alias of getComponentInChildren; also accepts implicit attached self.
---@param gameObject GameObject
---@param componentType ComponentType
---@return any|nil
---@overload fun(componentType:ComponentType):any|nil
function findComponentInChildren(gameObject,componentType) end

---@class Animation
---@field onFinished ComponentEvent Natural nonloop completion, not Stop.
---@field onRestarted ComponentEvent Explicit restart or loop wrap.
---@field onStopped ComponentEvent Stop or Reset while active.
---@field onStarted ComponentEvent Playback starts after the clip is ready.
---@field onPaused ComponentEvent Playback paused.
---@field onResumed ComponentEvent Playback resumed.
---@field gameObject GameObject
---@field clip string
---@field loop boolean
---@field playOnStart boolean
---@field isPlaying boolean Read-only
---@field paused boolean Read-only
---@field time number Read-only seconds
---@field error string Read-only loading error
---@field play fun(self:Animation)
---@field playClip fun(self:Animation,clip:string) Play an AnimationClip path value; loads asynchronously if needed.
---@field stop fun(self:Animation) Stops and rewinds to frame zero
---@field pause fun(self:Animation)
---@field resume fun(self:Animation)
---@field crossFade fun(self:Animation,clip:string,seconds?:number) Timed transition; default 0.25 seconds
---@field reset fun(self:Animation) Stops and restores the pre-play values
---@type ComponentToken
Animation = {}


---@class ParticleSystem
---@field Point integer Shape constant
---@field Box integer Shape constant
---@field Sphere integer Shape constant
---@field Cone integer Shape constant
---@field Alpha integer Blend constant
---@field Additive integer Blend constant
---@field Fire integer Sprite constant
---@field Smoke integer Sprite constant
---@field Sparkles integer Sprite constant
---@field play fun(self:ParticleSystem)
---@field pause fun(self:ParticleSystem)
---@field resume fun(self:ParticleSystem)
---@field stop fun(self:ParticleSystem,clearParticles?:boolean)
---@field restart fun(self:ParticleSystem)
---@field clear fun(self:ParticleSystem)
---@field emit fun(self:ParticleSystem,count:integer)
---@field setPreset fun(self:ParticleSystem,name:string) Fire, Smoke, Sparkles, Blood or Explosion
---@field isPlaying boolean Read-only
---@field paused boolean Read-only
---@field particleCount integer Read-only
---@field time number Read-only emitter cycle time
---@field loop boolean
---@field playOnAwake boolean
---@field localSpace boolean
---@field collision boolean Optional swept-point collider physics
---@field restitution number Bounce 0..1
---@field collisionFriction number Tangential velocity loss 0..1
---@field maxParticles integer
---@field burstCount integer
---@field shape integer
---@field blend integer
---@field sprite integer
---@field seed integer
---@field useSpriteSheet boolean Opt-in atlas animation; false by default
---@field sheetColumns integer
---@field sheetRows integer
---@field color table Assign a complete x/y/z vector table; returned tables are copies
---@field endColor table Assign a complete x/y/z vector table; returned tables are copies
---@field direction table Assign a complete x/y/z vector table; returned tables are copies
---@field velocity table Assign a complete x/y/z vector table; returned tables are copies
---@field gravity table Assign a complete x/y/z vector table; returned tables are copies
---@field boxSize table Assign a complete x/y/z vector table; returned tables are copies
---@field duration number
---@field startDelay number
---@field rate number
---@field lifetimeMin number
---@field lifetimeMax number
---@field speedMin number
---@field speedMax number
---@field sizeMin number
---@field sizeMax number
---@field endSize number
---@field alpha number
---@field endAlpha number
---@field radius number
---@field coneAngle number
---@field drag number
---@field rotationMin number
---@field rotationMax number
---@field angularVelocity number
---@field frameRate number
---@field texture string Empty selects bundled sprite; otherwise project-relative image
ParticleSystem = {}

---@class Laser
---@field enabled boolean
---@field color Vec3 Assign a complete RGB vector table (copy on read)
---@field intensity number 0..100
---@field range number 0.01..10000 metres
---@field width number Core diameter 0.001..10 metres
---@field glowWidth number Outer diameter; at least width, at most 100 metres
---@field impactSize number Impact disc diameter 0.001..100 metres
---@field hit boolean Read-only; latest simulation query hit a collider
---@field endPosition Vec3 Read-only; latest beam endpoint in world space
---@field hitNormal Vec3 Read-only; valid only when hit is true
---@type ComponentToken
Laser = {}

---@class LightShafts
---@field enabled boolean
---@field direction Vec3 Local emission direction; assign a complete nonzero vector
---@field height number 0.01..1000 metres
---@field width number Start width, 0.001..100 metres
---@field endWidth number End width, 0.001..500 metres
---@field color Vec3 RGB color; assign a complete vector
---@field intensity number 0..100
---@field density number 0..1
---@field falloff number Profile edge exponent, 0.1..8
---@field streaks number Profile streak frequency, 0..64
---@field fade number Profile start fade, 0.01..1
---@field nearFade number Camera near fade distance, 0.01..100 metres
---@field useBakedTexture boolean Use the cached profile if available, else procedural fallback
---@field texture string Project-relative profile image
---@type ComponentToken
LightShafts = {}

---@class CameraEffects
---@field enabled boolean
---@field bloom boolean
---@field colorGrading boolean
---@field vignette boolean
---@field depthOfField boolean
---@field motionBlur boolean Camera-only reprojection, no object velocities
---@field filmGrain boolean
---@field chromaticAberration boolean
---@field antiAliasing boolean FXAA
---@field ambientOcclusion boolean Six-tap SSAO
---@field nightVision boolean Green or infrared-style luminance visualization
---@field nausea boolean Animated screen distortion
---@field nightVisionMode number 0 = green, 1 = infrared-style (not physical heat)
---@field nightVisionIntensity number 0..1
---@field nauseaIntensity number 0..1
---@field nauseaSpeed number 0..5
---@field bloomIntensity number 0..4
---@field bloomThreshold number 0..8
---@field exposure number -5..5 stops
---@field contrast number 0..2
---@field saturation number 0..2
---@field temperature number -1..1
---@field vignetteIntensity number 0..1
---@field focusDistance number 0.01..10000 metres
---@field focusRange number 0.01..10000 metres
---@field dofStrength number 0..1
---@field motionStrength number 0..1
---@field grainIntensity number 0..0.2
---@field aberrationStrength number 0..0.02
---@field aoIntensity number 0..2
---@field aoRadius number 0.01..5 metres
CameraEffects = nil

---@class Fog
---@field enabled boolean Global distance fog; first enabled active Fog in entity order wins
---@field color table RGB 0..1; assign a complete x/y/z or three-element table (read returns a copy)
---@field density number 0..10 inverse metres; zero is clear
---@field start number Distance before fog begins, 0 or greater and less than end
---@field end number Distance cap, greater than start and at most 1000000 metres; use fog["end"] in Lua
Fog = nil

---@class ColorFade
---@field start fun(self:ColorFade,seconds?:number) Fade to fully visible color, default one second; zero is immediate
---@field stop fun(self:ColorFade,seconds?:number) Fade back to transparent, default one second; zero is immediate

---Create a fade controller for the current active camera; does not automatically start.
---Controllers share that camera's fade state; start/stop reverse from the current opacity.
---@param rgb Vec3 RGB 0..1, e.g. color.black
---@return ColorFade
function fadeToColor(rgb) end

---@class CloudSystem
---@field enabled boolean
---@field volumetric boolean False = 2D procedural layer; true = bounded volumetric box
---@field color table RGB 0..1; assign a complete x/y/z or three-element table, read returns a copy
---@field size table XYZ extent 0.1..10000 metres, multiplied by Transform scale; read returns a copy
---@field coverage number 0..1
---@field density number 0..10
---@field scale number Noise frequency 0.001..10
---@field speed number Wind speed -100..100
---@field opacity number 0..1
---@field steps integer Volumetric raymarch budget 4..24, default 12
CloudSystem = nil

---@class River
---@field enabled boolean
---@field color table RGB 0..1; assign a complete vector, read returns a copy
---@field width number 0.05..1000 local units
---@field opacity number 0..1
---@field speed number Flow speed -100..100
---@field waveScale number 0.01..100
---@field waveStrength number 0..1 Shading only; does not displace geometry
---@field foam number 0..1 Procedural edge foam
---@field subdivisions integer Samples per curve span 2..32
---@field points table List of 2..64 complete local vector tables; getter returns copies, assign the whole list to change
River = nil

---@class Vehicle
---@field enabled boolean
---@field throttle number -1..1, forward/reverse, runtime command
---@field steering number -1..1, positive right, runtime command
---@field brake number 0..1 service brake
---@field handbrake boolean Rear brake/sliding
---@field speed number Read-only signed forward m/s
---@field groundedWheels integer Read-only 0..4
---@field isGrounded boolean Read-only
---@field wheelCompression number[] Read-only copy, FL/FR/RL/RR, 0..1
---@field wheelBase number 0.2..20 local metres
---@field trackWidth number 0.2..10 local metres
---@field mountHeight number -10..10 local metres
---@field wheelRadius number 0.05..3 local metres
---@field suspensionLength number 0.05..3 local metres
---@field springStrength number 100..500000 N/m per wheel
---@field centerOfMassX number Local chassis COM offset X in metres, -10..10, default 0
---@field centerOfMassY number Local chassis COM offset Y in metres, -10..10, default 0; negative lowers COM
---@field centerOfMassZ number Local chassis COM offset Z in metres, -10..10, default 0
---@field suspensionDamping number 0..50000 per wheel
---@field engineForce number 0..200000 N total
---@field brakeForce number 0..300000 N total
---@field tireGrip number 0.05..5
---@field maxSteerAngle number 0..70 degrees
---@field maxSpeed number 1..150 m/s engine cutoff
Vehicle = nil

---@class Water: River
---@field mesh string Optional .mesh asset; empty uses this MeshRenderer or default plane
Water = nil

---@class BoneAttachment
---@field enabled boolean
---@field source GameObject|nil Animated model owner
---@field node integer Skeleton node index (-1 unbound)
---@field offset Vec3 Bone-local position offset (assign complete vector)
---@field rotation Vec3 Euler-degree offset (assign complete vector)
---@field error string Read-only runtime diagnostic
BoneAttachment = nil

---@class River
---@field shaderStyle integer 0 Classic, 1 Clear, 2 Ocean, 3 Lake, 4 Rapids, 5 Tropical, 6 Marsh, 7 Industrial canal
---@field waveHeight number Geometric displacement 0..3 metres
---@field waveLength number 0.1..100 metres
---@field waveSpeed number 0..10 multiplier


---@class TPSCamera
---@field enabled boolean
---@field target GameObject|nil Target root; nil follows the Transform parent
---@field offset Vec3 World-space look-at offset; assign a complete vector
---@field mouseLook boolean Right mouse or locked cursor orbits during Play
---@field lockMouse boolean Default false; request cursor lock in focused Play; false releases TPS-owned lock
---@field collision boolean Sphere-sweep obstruction avoidance (requires colliders)
---@field distance number Desired distance, within minDistance..maxDistance
---@field minDistance number At least 0.05 metres
---@field maxDistance number At most 1000 metres
---@field yaw number Orbit yaw in degrees
---@field pitch number Orbit pitch in degrees, within minPitch..maxPitch
---@field minPitch number At least -89 degrees
---@field maxPitch number At most 89 degrees
---@field sensitivity number 0..10 degrees per mouse pixel
---@field zoomSpeed number 0..20 metres per wheel unit
---@field smoothing number 0..100 response, zero snaps
---@field shoulder number -10..10 metres, local screen-right
---@field collisionRadius number 0.02..5 metres; near-plane coverage can enlarge this
---@field resolvedDistance number Read-only obstruction-adjusted distance
TPSCamera = nil

---@class Skybox
---@field affectEnvironment boolean Optional sky-colored ambient light; default false
---@field environmentIntensity number 0..5, default 1

---@class Vehicle

---@class NavMeshAgent
---@field enabled boolean
---@field speed number Movement speed, 0..100 metres/second
---@field heightOffset number World-space pivot height above the navmesh, -1000..1000; default 0
---@field stop fun(self:NavMeshAgent) Pause movement and clear velocity, keeping the route
---@field start fun(self:NavMeshAgent) Resume the existing route without discarding it
---@field setSpeed fun(self:NavMeshAgent, speed:number) Set movement speed, 0..100
---@field setHeightOffset fun(self:NavMeshAgent, height:number) Set pivot height above the surface, -1000..1000
---@field stoppingDistance number 0.01..10 metres
---@field stopped boolean Pause movement without clearing the route
---@field destination Vec3 Assign a complete world-space destination, or call setDestination
---@field velocity Vec3 Read-only actual last-frame velocity
---@field hasPath boolean Read-only; true while following a complete reachable path
---@field pathPending boolean Read-only; destination request awaits the next Play update
---@field distance number Read-only straight-line 3D distance from the agent position to its destination, in world units; zero if no destination or Transform.
---@field remainingDistance number Read-only remaining path distance
---@field status string Read-only navigation status
---@field setDestination fun(self:NavMeshAgent, x:number|Vec3, y?:number, z?:number) Queue a path request; resumes movement
---@field resetPath fun(self:NavMeshAgent) Clear destination request, route and velocity
NavMeshAgent = nil

---Construct a vector accepted by position/rotation/destination setters.
---@param x? number
---@param y? number
---@param z? number
---@return Vec3
function Vector3(x,y,z) end

---@class NavMeshAgent
---@field updateRotation boolean Automatically face the navigation path; default true
---@field angularSpeed number Degrees per second, 0..3600

---@class NavMeshObstacle
---@field enabled boolean
---@field size Vec3 Local box dimensions, positive; assign a whole vector
---@field center Vec3 Local box center; assign a whole vector
NavMeshObstacle = nil

---@class GUI
---@field WindowFlags GUIWindowFlags
---@field TableFlags GUITableFlags
---@field Col GUIColors
---@field StyleVar GUIStyleVars
---@field image fun(path:string,width?:number,height?:number):boolean Full-resolution project image; returns loaded.
---@field imageButton fun(id:string,path:string,width?:number,height?:number):boolean Returns clicked.
---@field beginTable fun(id:string,columns:integer,flags?:integer,height?:number):boolean 1..64 columns; only end when true. ScrollY needs height.
---@field endTable fun() Table scope operation; set up columns before rows.
---@field tableNextRow fun() Table scope operation; set up columns before rows.
---@field tableHeadersRow fun() Table scope operation; set up columns before rows.
---@field tableNextColumn fun():boolean Advance to next column.
---@field tableSetColumnIndex fun(index:integer):boolean Zero-based table column index.
---@field tableSetupColumn fun(label:string,width?:number) Fixed width when positive, otherwise stretch.
---@field tableGetColumnIndex fun():integer Current zero-based index or column count.
---@field tableGetColumnCount fun():integer Current zero-based index or column count.
---@field beginMenu fun(label:string,enabled?:boolean):boolean Only end when true.
---@field endMenu fun() End successful beginMenu.
---@field beginMenuBar fun():boolean Requires GUI.WindowFlags.MenuBar on beginWindow. Only end when true.
---@field endMenuBar fun() End successful beginMenuBar.
---@field menuItem fun(label:string,selected?:boolean,enabled?:boolean):boolean Returns clicked.
---@field openPopup fun(id:string) Open a named popup.
---@field beginPopup fun(id:string):boolean Only endPopup when true.
---@field beginPopupModal fun(id:string):boolean Only endPopup when true.
---@field endPopup fun() Popup scope operation.
---@field closeCurrentPopup fun() Popup scope operation.
---@field beginCombo fun(label:string,preview:string):boolean Only endCombo when true.
---@field endCombo fun() End successful beginCombo.
---@field combo fun(label:string,index:integer,items:table):integer,boolean 1-based selected index and changed; 1..256 items.
---@field radioButton fun(label:string,selected:boolean):boolean Returns clicked.
---@field pushStyleColor fun(name:string|integer,r:number,g:number,b:number,a?:number) Use GUI.Col constants; pair with popStyleColor.
---@field popStyleColor fun(count?:integer) Strict reverse-order scoped pop; default 1.
---@field popStyleVar fun(count?:integer) Strict reverse-order scoped pop; default 1.
---@field pushStyleVar fun(name:string|integer,x:number,y?:number) Use GUI.StyleVar constants. Values 0..1000.
---@field pushFontSize fun(size:number) 8..96 pixels; pair with popFont.
---@field popFont fun() End font size scope.
---@field beginDragDropSource fun():boolean Only end when true. Script payloads isolated from editor payloads.
---@field beginDragDropTarget fun():boolean Only end when true. Script payloads isolated from editor payloads.
---@field endDragDropSource fun() End successful drag/drop scope.
---@field endDragDropTarget fun() End successful drag/drop scope.
---@field setDragDropPayload fun(kind:string,value:string) Source only. Type max 28 bytes; value max 4096.
---@field acceptDragDropPayload fun(kind:string):string? Target only. Returns delivered matching script payload or nil.
---@field getMousePos fun():number,number Returns Game-surface mouse x,y.
---@field isMouseDown fun(button:integer):boolean Mouse button 0..4.
---@field isMouseClicked fun(button:integer):boolean Mouse button 0..4.
---@field isMouseReleased fun(button:integer):boolean Mouse button 0..4.
---@field isKeyDown fun(key:string):boolean Named key: A..Z, 0..9, Space, Enter, Escape, Tab, Backspace, Delete, arrows.
---@field isKeyReleased fun(key:string):boolean Named key: A..Z, 0..9, Space, Enter, Escape, Tab, Backspace, Delete, arrows.
---@field isKeyPressed fun(key:string,repeatKey?:boolean):boolean Named key, optional repeat.
---@field drawLine fun(x:number,y:number,x2:number,y2:number,r:number,g:number,b:number,a?:number,width?:number) Screen-pixel coordinates; colors 0..1.
---@field drawRect fun(x:number,y:number,width:number,height:number,r:number,g:number,b:number,a?:number,rounding?:number,thickness?:number) Screen-pixel coordinates; colors 0..1.
---@field drawRectFilled fun(x:number,y:number,width:number,height:number,r:number,g:number,b:number,a?:number,rounding?:number,thickness?:number) Screen-pixel coordinates; colors 0..1.
---@field drawCircle fun(x:number,y:number,radius:number,r:number,g:number,b:number,a?:number,thickness?:number) Screen-pixel coordinates; radius 0..100000.
---@field drawCircleFilled fun(x:number,y:number,radius:number,r:number,g:number,b:number,a?:number,thickness?:number) Screen-pixel coordinates; radius 0..100000.
---@field drawText fun(x:number,y:number,value:string,r:number,g:number,b:number,a?:number) Screen-pixel coordinates.
---@field drawTriangleFilled fun(x:number,y:number,x2:number,y2:number,x3:number,y3:number,r:number,g:number,b:number,a?:number) Screen-pixel coordinates.
---@field beginWindow fun(title:string,flags?:integer):boolean Always call endWindow even when false.
---@field endWindow fun() End the current window.
---@field text fun(value:string) Plain text, never a printf format.
---@field textWrapped fun(value:string) Wrapped plain text.
---@field button fun(label:string):boolean True on activation.
---@field smallButton fun(label:string):boolean Compact button.
---@field checkbox fun(label:string,value:boolean):boolean,boolean Returns new value, changed.
---@field sliderFloat fun(label:string,value:number,min:number,max:number):number,boolean Returns value, changed.
---@field sliderInt fun(label:string,value:integer,min:integer,max:integer):integer,boolean Returns value, changed.
---@field dragFloat fun(label:string,value:number,speed?:number):number,boolean Returns value, changed; speed defaults 0.1.
---@field inputFloat fun(label:string,value:number):number,boolean Returns value, changed.
---@field inputInt fun(label:string,value:integer):integer,boolean Returns value, changed.
---@field inputText fun(label:string,value:string):string,boolean Returns text, changed; max 4095 bytes.
---@field colorEdit3 fun(label:string,r:number,g:number,b:number):number,number,number,boolean Returns r,g,b,changed.
---@field colorEdit4 fun(label:string,r:number,g:number,b:number,a:number):number,number,number,number,boolean Returns r,g,b,a,changed.
---@field separator fun() Horizontal separator.
---@field sameLine fun(offset?:number,spacing?:number) Continue on the same line.
---@field spacing fun() Add vertical space.
---@field newLine fun() Start a line.
---@field progressBar fun(fraction:number) Clamps fraction to 0..1.
---@field selectable fun(label:string,selected?:boolean):boolean True when clicked.
---@field collapsingHeader fun(label:string):boolean True while expanded; no matching end.
---@field beginChild fun(id:string,width?:number,height?:number):boolean Always pair with endChild.
---@field endChild fun() End child region.
---@field beginGroup fun() Begin a layout group.
---@field endGroup fun() End a layout group.
---@field beginDisabled fun(disabled?:boolean) Default true; pair with endDisabled.
---@field endDisabled fun() End disabled scope.
---@field pushID fun(id:string) Scope repeated widget labels; pair with popID.
---@field popID fun() Pop a widget ID.
---@field setNextWindowPos fun(x:number,y:number) Game-surface pixels; call before beginWindow.
---@field setNextWindowSize fun(width:number,height:number) Game-surface pixels; call before beginWindow.
---@field setCursorPos fun(x:number,y:number) Window-local content position.
---@field getWindowSize fun():number,number Returns width,height.
---@field getContentRegionAvail fun():number,number Returns available width,height.
---@field setNextItemWidth fun(width:number) Width of next input.
---@field isItemHovered fun():boolean Whether last widget is hovered.
---@field isItemActive fun():boolean Whether last widget is active.
---@field setTooltip fun(value:string) Plain-text tooltip.
---@field bulletText fun(value:string) Bulleted plain text.
---@field getDisplaySize fun():number,number Returns Game-surface pixel width,height.
GUI = {}

---@return integer quality 0 full, 1 half, 2 quarter, 3 eighth
function getTextureQuality() end
---@param quality integer Play-only texture quality, 0..3
function setTextureQuality(quality) end

---@class GUIWindowFlags
---@field NoTitleBar integer
---@field NoResize integer
---@field NoMove integer
---@field NoScrollbar integer
---@field NoCollapse integer
---@field AlwaysAutoResize integer
---@field NoBackground integer
---@field MenuBar integer
---@field NoInputs integer
---@field NoNav integer
---@field NoDecoration integer
---@field NoFocusOnAppearing integer
---@field NoBringToFrontOnFocus integer

---@class GUITableFlags
---@field Borders integer
---@field RowBg integer
---@field Resizable integer
---@field Reorderable integer
---@field Hideable integer
---@field SizingStretchProp integer
---@field ScrollY integer

---@class GUIStyleVars
---@field Alpha integer
---@field DisabledAlpha integer
---@field WindowPadding integer
---@field WindowRounding integer
---@field WindowBorderSize integer
---@field WindowMinSize integer
---@field WindowTitleAlign integer
---@field ChildRounding integer
---@field ChildBorderSize integer
---@field PopupRounding integer
---@field PopupBorderSize integer
---@field FramePadding integer
---@field FrameRounding integer
---@field FrameBorderSize integer
---@field ItemSpacing integer
---@field ItemInnerSpacing integer
---@field IndentSpacing integer
---@field CellPadding integer
---@field ScrollbarSize integer
---@field ScrollbarRounding integer
---@field GrabMinSize integer
---@field GrabRounding integer
---@field TabRounding integer
---@field ButtonTextAlign integer
---@field SelectableTextAlign integer

---@class GUIColors
---@field Text integer
---@field TextDisabled integer
---@field WindowBg integer
---@field ChildBg integer
---@field PopupBg integer
---@field Border integer
---@field BorderShadow integer
---@field FrameBg integer
---@field FrameBgHovered integer
---@field FrameBgActive integer
---@field TitleBg integer
---@field TitleBgActive integer
---@field TitleBgCollapsed integer
---@field MenuBarBg integer
---@field ScrollbarBg integer
---@field ScrollbarGrab integer
---@field ScrollbarGrabHovered integer
---@field ScrollbarGrabActive integer
---@field CheckMark integer
---@field SliderGrab integer
---@field SliderGrabActive integer
---@field Button integer
---@field ButtonHovered integer
---@field ButtonActive integer
---@field Header integer
---@field HeaderHovered integer
---@field HeaderActive integer
---@field Separator integer
---@field SeparatorHovered integer
---@field SeparatorActive integer
---@field ResizeGrip integer
---@field ResizeGripHovered integer
---@field ResizeGripActive integer
---@field InputTextCursor integer
---@field TabHovered integer
---@field Tab integer
---@field TabSelected integer
---@field TabSelectedOverline integer
---@field TabDimmed integer
---@field TabDimmedSelected integer
---@field TabDimmedSelectedOverline integer
---@field DockingPreview integer
---@field DockingEmptyBg integer
---@field PlotLines integer
---@field PlotLinesHovered integer
---@field PlotHistogram integer
---@field PlotHistogramHovered integer
---@field TableHeaderBg integer
---@field TableBorderStrong integer
---@field TableBorderLight integer
---@field TableRowBg integer
---@field TableRowBgAlt integer
---@field TextLink integer
---@field TextSelectedBg integer
---@field TreeLines integer
---@field DragDropTarget integer
---@field DragDropTargetBg integer
---@field UnsavedMarker integer
---@field NavCursor integer
---@field NavWindowingHighlight integer
---@field NavWindowingDimBg integer
---@field ModalWindowDimBg integer
---@field COUNT integer

---Box-projected texture on opaque geometry. Local XY is the image, Z is projection depth.
---@class Decal
---@field enabled boolean
---@field texture string
---@field color Vec3 Tint, read as a copy; assign the whole vector to update.
---@field width number
---@field height number
---@field depth number
---@field opacity number
---@field sortingOrder integer
---@type ComponentToken
Decal = {}


---@class ScreenResolution
---@field width integer
---@field height integer
---@field refreshRate number
---@class ScreenAPI
---@field brightness number Rendered-game brightness, 0..1 (not monitor brightness).
---@field currentResolution ScreenResolution Read-only display mode.
---@field fullScreen boolean Separate game/player windows only.
---@field width integer Read-only game render width in pixels.
---@field height integer Read-only game render height in pixels.
---@field getResolutions fun():ScreenResolution[]
---@field getResolution fun(width:integer,height:integer):boolean Checks availability.
---@field setResolution fun(width:integer,height:integer):boolean|nil,error?:string Separate game/player windows only.
---@field takeScreenshot fun(path:string):boolean|nil,error?:string Queue a PNG under userDataPath.
---@type ScreenAPI
Screen = {}
---@class GameAPI
---@field companyName string Read-only.
---@field getDataPath string Read-only mounted game asset directory (a property).
---@field isEditor boolean Read-only.
---@field isFocused boolean Read-only native-window focus.
---@field userDataPath string Read-only per-game persistent directory.
---@field engineVersion string Read-only.
---@field blockGameKilling boolean Holds normal graceful quit until cleared or the ten-second timeout.
---@field targetFrameRate integer -1 uncapped, or 1..1000; VSync may cap further.
---@field openURL fun(url:string):boolean|nil,error?:string HTTP/HTTPS only; success means handed to OS.
---@field quit fun() Deferred graceful quit; stops Play in editor.
---@field saveString fun(key:string,value:string):boolean|nil,error?:string
---@field loadString fun(key:string,default?:string):string|nil,error?:string
---@field saveInt fun(key:string,value:integer):boolean|nil,error?:string
---@field loadInt fun(key:string,default?:integer):integer|nil,error?:string
---@field saveFloat fun(key:string,value:number):boolean|nil,error?:string
---@field loadFloat fun(key:string,default?:number):number|nil,error?:string
---@field saveBool fun(key:string,value:boolean):boolean|nil,error?:string
---@field loadBool fun(key:string,default?:boolean):boolean|nil,error?:string
---@field saveTable fun(key:string,value:table):boolean|nil,error?:string
---@field loadTable fun(key:string,default?:table):table|nil,error?:string
---@field saveVector3 fun(key:string,value:Vec3):boolean|nil,error?:string
---@field loadVector3 fun(key:string,default?:Vec3):Vec3|nil,error?:string
---@type GameAPI
Game = {}
---@class GameFile Opaque file handle; use File functions, not io.
---@class FileAPI
---@field open fun(path:string,mode:string):GameFile|nil,error?:string r/rb/w/wb/a/ab; sandboxed paths.
---@field read fun(file:GameFile,format?:string|integer):string|nil,error?:string Default *l; also *a or byte count.
---@field write fun(file:GameFile,data:string):boolean|nil,error?:string
---@field close fun(file:GameFile)
---@field flush fun(file:GameFile):boolean
---@field seek fun(file:GameFile,whence?:string,offset?:integer):integer|nil,error?:string set/cur/end.
---@field lines fun(file:GameFile|string):fun():string|nil Path form auto-closes at EOF.
---@field rename fun(oldPath:string,newPath:string):boolean|nil,error?:string Never overwrites.
---@field remove fun(path:string):boolean|nil,error?:string Files only.
---@type FileAPI
File = {}
-- Built-in events may be global functions or registered using on(name, callback):
-- OnResolutionChanged(resolution), OnFocusLost(), OnFocus(), OnUrlOpened(url), OnGamePreQuit().


---@param path string
---@return string clip Checked .leanim asset path value.
function AnimationClip(path) end
---@class RandomAPI
---@field randomFloat fun(min:number,max:number):number Finite bounds, min <= max; uniform floating sample.
---@field randomInt fun(min:integer,max:integer):integer Inclusive uniform bounds.
---@field randomBool fun():boolean Equally likely true/false.
---@field randomString fun(length:integer):string 0..4096 ASCII alphanumeric characters; not cryptographic.
---@type RandomAPI
Random = {}
-- The same Random functions are also available globally.
function randomFloat(min,max) end
function randomInt(min,max) end
function randomBool() end
function randomString(length) end

---@class ComponentEvent
---Opaque next-occurrence event for wait(); read it from a live component.
---@class Recoil
---@field enabled boolean Render-only local camera recoil, independent of FPS/TPS Transform.
---@field playOnStart boolean Start burst, or sequence if keys exist.
---@field sequenceLoop boolean Repeat the scheduled sequence.
---@field burstCount integer Zero repeats until stopped or duration expires.
---@field seed integer Nonnegative pseudorandom seed.
---@field isFiring boolean Read-only burst/sequence activity, not recovery state.
---@field trigger fun(self:Recoil,strength?:number) One impulse; multiplier 0..10, default 1.
---@field start fun(self:Recoil) Start/restart timed burst after delay.
---@field stop fun(self:Recoil) Stop future impulses; let the spring recover.
---@field reset fun(self:Recoil) Stop and zero offsets immediately.
---@field playSequence fun(self:Recoil) Start the authored sequence after delay.
---@field setSequence fun(self:Recoil,keys:RecoilKey[]) Replace sorted keys; stops firing.
---@field pitch number Degrees, -45..45. Positive lifts the view.
---@field yaw number Degrees, -45..45.
---@field roll number Degrees, -45..45.
---@field back number Metres, -1..1; positive local Z kicks backward.
---@field up number Metres, -1..1.
---@field side number Metres, -1..1.
---@field rotationRandom number Independent random angle amplitude, 0..45 degrees.
---@field positionRandom number Fraction of absolute back displacement, 0..2.
---@field frequency number Angular spring frequency, 0.1..40 Hz.
---@field damping number Angular damping ratio, 0.1..3; one is critical damping.
---@field positionFrequency number Position spring frequency, 0.1..40 Hz.
---@field positionDamping number Position damping ratio, 0.1..3.
---@field maxAngle number Combined angular displacement cap, 0..89 degrees.
---@field maxDistance number Combined positional displacement cap, 0..2 metres.
---@field interval number Timed shot spacing / sequence loop gap, 0.01..60 seconds.
---@field duration number Burst duration after delay, 0..3600; zero unlimited.
---@field delay number Start delay, 0..3600 seconds.
---@field strength number Global impulse multiplier, 0..10.
---@field recovery number Spring frequency multiplier, 0.1..4.
---@type ComponentToken
Recoil = {}
---@class RecoilKey
---@field time number Seconds from sequence start, 0..3600; sorted ascending.
---@field strength number Optional impulse multiplier, 0..10; default 1.

---@class Animator
---@field controller string Project-relative .controller file
---@field enabled boolean Pause/resume the state machine and its backend
---@field playOnStart boolean Enter the controller entry state automatically; default true
---@field currentState string Read-only current state name
---@field stateTime number Read-only elapsed playback seconds in the current state
---@field isPlaying boolean Read-only; state machine is running, enabled and unpaused (it may hold a finished nonlooping state)
---@field paused boolean Read-only paused flag
---@field error string Read-only controller/playback diagnostic
---@field setBool fun(self:Animator, name:string, value:boolean) Set a declared Bool parameter
---@field setInt fun(self:Animator, name:string, value:integer) Set a declared Int parameter
---@field setFloat fun(self:Animator, name:string, value:number) Set a declared Float parameter
---@field getBool fun(self:Animator, name:string):boolean Read a declared Bool parameter
---@field getInt fun(self:Animator, name:string):integer Read a declared Int parameter
---@field getFloat fun(self:Animator, name:string):number Read a declared Float parameter
---@field play fun(self:Animator, state:string) Enter/restart a state with no fade
---@field crossFade fun(self:Animator, state:string, seconds?:number) Enter/restart a state with a fade; default 0.2 seconds
---@field stop fun(self:Animator) Stop state evaluation and rewind backend to its first pose
---@field pause fun(self:Animator) Pause state evaluation and playback
---@field resume fun(self:Animator) Resume a paused state machine
---@field hasState fun(self:Animator, state:string):boolean Test a controller state name or ID
Animator = nil
