---@meta
-- Editor-only LuaLS/EmmyLua definitions. Do not execute this file.
---@class Vec3
---@field x number
---@field y number
---@field z number
---@class Quaternion: Vec3
---@field w number
---@class ComponentToken
---@alias ComponentType ComponentToken|'Transform'|'RigidBody'|'Collider'|'BoxCollider'|'MeshRenderer'|'ObjectName'|'LuaScript'|'Camera'
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
---@class MeshRenderer
---@field mesh 'Cube'|'Sphere'|'Capsule'|'Cone'
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
