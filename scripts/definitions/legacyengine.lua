---@meta
-- Editor-only LuaLS/EmmyLua definitions. Do not execute this file.
---@class Vec3
---@field x number
---@field y number
---@field z number
---@class Quaternion: Vec3
---@field w number
---@class ComponentToken
---@alias ComponentType ComponentToken|'Transform'|'RigidBody'|'Collider'|'BoxCollider'|'MeshRenderer'|'ObjectName'|'LuaScript'|'Camera'|'AudioSource'|'AudioListener'|'CharacterController'|'FPSCamera'
---@class GameObject
---@field name string
---@field parent GameObject|nil
---@field getComponent fun(componentType:ComponentType):Transform|RigidBody|Collider|MeshRenderer|ObjectName|LuaScript|nil
---@field addComponent fun(componentType:ComponentType):Transform|RigidBody|Collider|MeshRenderer|ObjectName|LuaScript
---@field removeComponent fun(componentType:ComponentType)
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
---@field shape 'Box'|'Sphere'|'Capsule'|'Cone'
---@field size Vec3
---@field center Vec3
---@field radius number
---@field height number
---@field friction number
---@field bounciness number
-- Texture exports are project-relative PNG/JPEG/BMP/TGA/HDR asset paths, not loaders.
---@alias Texture string
---@class MeshRenderer
---@field material string Project-relative .mat asset path; empty uses inline Diffuse
---@field setMaterial fun(self:MeshRenderer,path:string) Assign an asset, reset instance overrides
---@field setMaterialColor fun(self:MeshRenderer,r:number,g:number,b:number)
---@field setMaterialTexture fun(self:MeshRenderer,texture:Texture|nil)
---@field setMaterialNormalMap fun(self:MeshRenderer,texture:Texture|nil)
---@field setMaterialShader fun(self:MeshRenderer,shader:string) Diffuse, Diffuse Bumped or Diffuse Transparent
---@field setMaterialNormalStrength fun(self:MeshRenderer,value:number)
---@field setMaterialTiling fun(self:MeshRenderer,x:number,y:number)
---@field setMaterialSpecular fun(self:MeshRenderer,value:number)
---@field resetMaterialOverrides fun(self:MeshRenderer)
---@field mesh 'Cube'|'Sphere'|'Capsule'|'Cone'|'Plane'
---@field color Vec3
---@field checkerboard boolean
---@field specular number
---@class ObjectName
---@field value string

function onStart() end
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
---@return Transform|RigidBody|Collider|MeshRenderer|ObjectName|LuaScript|nil
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

---Suspend this lifecycle callback without blocking the engine. Not supported at top-level/onDestroy/custom event listeners.
---@param seconds number Finite seconds from 0 to 86400; zero resumes on next update
function waitForSeconds(seconds) end
---Resume after the next occurrence of an AudioSource event; past events do not count.
---@param event AudioEvent
function wait(event) end

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
