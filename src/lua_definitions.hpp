#pragma once
#include <string>
#include <string_view>

struct LuaDefinition { const char* name; const char* signature; };
inline constexpr LuaDefinition luaDefinitions[]{
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
