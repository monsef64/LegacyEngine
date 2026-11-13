---@meta
-- Editor-only LuaLS/EmmyLua definitions. Do not execute this file.
---@class Vec3
---@field x number
---@field y number
---@field z number
---@class Quaternion: Vec3
---@field w number
---@class ComponentToken
---@alias ComponentType ComponentToken|'Transform'|'RigidBody'|'Collider'|'BoxCollider'|'MeshRenderer'|'ObjectName'|'LuaScript'|'Camera'|'AudioSource'|'AudioListener'
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
---@field setMaterialShader fun(self:MeshRenderer,shader:string) Diffuse, Bumped Diffuse or Diffuse Transparent
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
---@field type integer 0 six face textures, 1 HDR panorama
---@field enabled boolean
---@field color Vector3 Color tint
---@field right Texture +X
---@field left Texture -X
---@field top Texture +Y
---@field bottom Texture -Y
---@field front Texture -Z
---@field back Texture +Z
---@field hdr Texture Radiance .hdr equirectangular panorama
---@field setType fun(self:Skybox,type:string|integer) Textures or HDR, or 0/1
---@field setColor fun(self:Skybox,r:number,g:number,b:number)
---@field setEnabled fun(self:Skybox,enabled:boolean)
---@field setHDR fun(self:Skybox,texture:Texture|nil)
---@field setFace fun(self:Skybox,face:string,texture:Texture|nil)
---@field setTextures fun(self:Skybox,right:Texture,left:Texture,top:Texture,bottom:Texture,front:Texture,back:Texture)
