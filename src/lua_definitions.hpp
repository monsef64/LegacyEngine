#pragma once
#include <string>
#include <string_view>

struct LuaDefinition { const char* name; const char* signature; };
inline constexpr LuaDefinition luaDefinitions[]{
    {"createAudioSource","createAudioSource(owner=self, clipPath=nil) -> AudioSource"},
    {"AudioClip","AudioClip(projectRelativeWavPath) -> AudioClip path"},
    {"AudioSource","AudioSource - audio component type token"},
    {"play","audio:play() - restart assigned clip"},
    {"stop","audio:stop() - stop and rewind"},
    {"pause","audio:pause()"},
    {"resume","audio:resume()"},
    {"setPitch","audio:setPitch(pitch) -> clamped 0.1 to 4"},
    {"fadeIn","audio:fadeIn(seconds) - restart and fade from silence"},
    {"fadeOut","audio:fadeOut(seconds) - fade then stop"},
    {"setVolume","audio:setVolume(volume) -> clamped 0 to 1"},
    {"mute","audio:mute()"},
    {"unmute","audio:unmute()"},
    {"setLooping","audio:setLooping(enabled) -> boolean"},
    {"setPan","audio:setPan(pan) -> clamped -1 left to 1 right"},
    {"setDistance","audio:setDistance(distance) -> attenuation distance"},
    {"isPlaying","audio:isPlaying() -> boolean"},
    {"playOneShot","audio:playOneShot(AudioClip) - overlapping nonlooping clip"},
    {"new","GameObject.new(name) / Camera.new(name)"},
    {"find","GameObject.find(name) -> GameObject or nil"},
    {"getActive","Camera.getActive() -> Camera GameObject or nil"},
    {"setPosition","object:setPosition(x,y,z) or object:setPosition({x,y,z})"},
    {"getPosition","object:getPosition() -> position copy"},
    {"setRotation","object:setRotation(x,y,z) degrees or quaternion/table"},
    {"getRotation","object:getRotation() -> quaternion copy"},
    {"setScale","object:setScale(x,y,z) or vector"},
    {"getScale","object:getScale() -> scale copy"},
    {"translate","object:translate(x,y,z) or vector"},
    {"setName","object:setName(name)"},
    {"getName","object:getName() -> string"},
    {"destroy","object:destroy()"},
    {"setActive","camera:setActive(enabled=true)"},
    {"setTarget","camera:setTarget(objectOrPositionOrNil)"},
    {"setShake","camera:setShake(intensity,seconds)"},
    {"createCamera","createCamera(name) -> Camera GameObject"},
    {"setActiveCamera","setActiveCamera(cameraOrNil)"},
    {"getActiveCamera","getActiveCamera() -> Camera GameObject or nil"},
    {"setCameraPosition","setCameraPosition(camera, {x,y,z})"},
    {"setCameraRotation","setCameraRotation(camera, Euler degrees {x,y,z} or quaternion {x,y,z,w})"},
    {"lookAt","lookAt(camera, worldPositionOrGameObject) - one-shot aim"},
    {"setFieldOfView","setFieldOfView(camera, degrees)"},
    {"setProjectionMode","setProjectionMode(camera, Perspective | Orthographic)"},
    {"setNearClipPlane","setNearClipPlane(camera, distance)"},
    {"setFarClipPlane","setFarClipPlane(camera, distance)"},
    {"setViewport","setViewport(camera, x,y,width,height) - normalized top-left viewport"},
    {"setCameraTarget","setCameraTarget(camera, pointOrObjectOrNil) - continuous aim"},
    {"setFollowTarget","setFollowTarget(camera, pointOrObjectOrNil, worldOffset)"},
    {"setCameraShake","setCameraShake(camera, intensity, durationSeconds)"},
    {"worldToScreen","worldToScreen(camera, worldPosition) -> normalized {x,y,zDepth}"},
    {"screenToWorld","screenToWorld(camera, normalized {x,y,zDepth}) -> world position"},
    {"Camera","Camera - camera component type token"},
    {"onStart","onStart() - called when Execute succeeds"},
    {"onUpdate","onUpdate(deltaTime) - once per frame, seconds"},
    {"onFixedUpdate","onFixedUpdate(deltaTime) - before each 1/60 s physics step"},
    {"onDestroy","onDestroy() - object destruction or runtime shutdown/reset"},
    {"createGameObject","createGameObject(name) -> GameObject"},
    {"destroyGameObject","destroyGameObject(gameObject)"},
    {"getComponent","getComponent(gameObject, Type) / object.getComponent(Type) -> live component or nil"},
    {"addComponent","addComponent(gameObject, componentType) -> live component"},
    {"removeComponent","removeComponent(gameObject, componentType)"},
    {"findGameObjectByName","findGameObjectByName(name) -> GameObject or nil"},
    {"findGameObjectsByComponent","findGameObjectsByComponent(componentType) -> GameObject[]"},
    {"setParent","setParent(child, parentOrNil) - keep world pose; reject cycles"},
    {"getParent","getParent(child) -> GameObject or nil"},
    {"Transform","Transform - component type token"},
    {"RigidBody","RigidBody - component type token"},
    {"Collider","Collider - component type token"},
    {"BoxCollider","BoxCollider - alias of Collider"},
    {"MeshRenderer","MeshRenderer - component type token"},
    {"ObjectName","ObjectName - component type token"},
    {"LuaScript","LuaScript - attached script component (path, enabled)"},
    {"self","self - GameObject owning the attached Lua Script"},
    {"enabled","LuaScript.enabled: boolean"},
    {"path","LuaScript.path: relative asset path"},
    {"parent","GameObject.parent: GameObject or nil"},
    {"position","Transform.position: {x,y,z}"},{"rotation","Transform.rotation: quaternion {x,y,z,w}"},
    {"scale","Transform.scale: {x,y,z}"},{"mass","RigidBody.mass: number"},
    {"useGravity","RigidBody.useGravity: boolean"},{"linearVelocity","RigidBody.linearVelocity: {x,y,z}"},
    {"angularVelocity","RigidBody.angularVelocity: {x,y,z}"},{"shape","Collider.shape: Box | Sphere | Capsule | Cone"},
    {"radius","Collider.radius: number"},{"height","Collider.height: number"},{"size","Collider.size: {x,y,z}"},
    {"center","Collider.center: {x,y,z}"},{"friction","Collider.friction: number"},
    {"bounciness","Collider.bounciness: number"},{"mesh","MeshRenderer.mesh: Cube | Sphere | Capsule | Cone"},
    {"color","MeshRenderer.color: {x,y,z}"},{"checkerboard","MeshRenderer.checkerboard: boolean"},
    {"specular","MeshRenderer.specular: number"},{"name","GameObject.name: string"},
    {"value","ObjectName.value: string"}
};
inline std::string luaCompletion(std::string_view prefix) {
    std::string common;
    if(prefix.empty())return common;
    bool first=true;
    for(const auto& definition:luaDefinitions) {
        const std::string_view name=definition.name;
        if(name.substr(0,prefix.size())!=prefix)continue;
        if(first){common=name;first=false;}
        else {std::size_t n=0;while(n<common.size() && n<name.size() && common[n]==name[n])++n;common.resize(n);}
    }
    return common;
}
