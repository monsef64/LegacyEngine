#include "scripting/lua_scene.hpp"
#include "animation/animation_clip.hpp"
#include "assets/cubemap.hpp"
#include "core/hierarchy.hpp"
#include "editor/rotation_editor.hpp"
#include "effects/laser_io.hpp"
#include "effects/light_shafts_io.hpp"
#include "particles/particle_io.hpp"
#include "physics/queries.hpp"
#include "prefabs/prefab.hpp"
#include "project/layers.hpp"
#include "scene/scene_camera.hpp"
#include "scene/skybox_transition.hpp"
#include "scripting/script_exports.hpp"
#include "ui/game_ui.hpp"
#include <cstdio>
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}
#include <cctype>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace {
struct AudioWaitToken {
    World* world;
    Entity entity;
    int event;
};
struct Handle {
    LuaScene* scene;
    World* world;
    Entity entity;
};
struct Component {
    Handle object;
    int type;
};
struct Vector {
    Component component;
    char field[32];
};
const char* types[]{"Transform",      "RigidBody",
                    "Collider",       "MeshRenderer",
                    "ObjectName",     "LuaScript",
                    "Camera",         "AudioSource",
                    "AudioListener",  "DirectionalLight",
                    "PointLight",     "SpotLight",
                    "Skybox",         "CharacterController",
                    "FPSCamera",      "Raycast",
                    "Animation",      "ParticleSystem",
                    "Laser",          "LightShafts",
                    "CameraEffects",  "Fog",
                    "CloudSystem",    "River",
                    "Vehicle",        "Water",
                    "BoneAttachment", "TPSCamera",
                    "NavMeshAgent",   "NavMeshObstacle"};
int componentType(lua_State* L, int index) {
    if (auto* type = static_cast<int*>(luaL_testudata(L, index, "ComponentType"))) {
        return *type;
    }
    const char* name = luaL_checkstring(L, index);
    if (!std::strcmp(name, "BoxCollider")) {
        return 2;
    }
    for (int i = 0; i < 30; ++i) {
        if (!std::strcmp(name, types[i])) {
            return i;
        }
    }
    return luaL_error(L, "Unknown component type: %s", name);
}
Handle& handle(lua_State* L, int index) {
    auto* h = static_cast<Handle*>(luaL_checkudata(L, index, "GameObject"));
    if (!h->world->alive(h->entity)) {
        luaL_error(L, "GameObject has been destroyed");
    }
    return *h;
}
bool has(const Handle& h, int type) {
    switch (type) {
    case 0:
        return h.world->has<Transform>(h.entity);
    case 1:
        return h.world->has<RigidBody>(h.entity);
    case 2:
        return h.world->has<Collider>(h.entity);
    case 3:
        return h.world->has<MeshRenderer>(h.entity);
    case 4:
        return h.world->has<ObjectName>(h.entity);
    case 5:
        return h.world->has<LuaScript>(h.entity);
    case 6:
        return h.world->has<Camera>(h.entity);
    case 7:
        return h.world->has<AudioSource>(h.entity);
    case 8:
        return h.world->has<AudioListener>(h.entity);
    case 9:
        return h.world->has<DirectionalLight>(h.entity);
    case 10:
        return h.world->has<PointLight>(h.entity);
    case 11:
        return h.world->has<SpotLight>(h.entity);
    case 12:
        return h.world->has<Skybox>(h.entity);
    case 13:
        return h.world->has<CharacterController>(h.entity);
    case 14:
        return h.world->has<FPSCamera>(h.entity);
    case 22:
        return h.world->has<CloudSystem>(h.entity);
    case 23:
        return h.world->has<River>(h.entity);
    case 24:
        return h.world->has<Vehicle>(h.entity);
    case 25:
        return h.world->has<Water>(h.entity);
    case 26:
        return h.world->has<BoneAttachment>(h.entity);
    case 29:
        return h.world->has<NavMeshObstacle>(h.entity);
    case 28:
        return h.world->has<NavMeshAgent>(h.entity);
    case 27:
        return h.world->has<TPSCamera>(h.entity);
    case 21:
        return h.world->has<Fog>(h.entity);
    case 20:
        return h.world->has<CameraEffects>(h.entity);
    case 19:
        return h.world->has<LightShafts>(h.entity);
    case 18:
        return h.world->has<Laser>(h.entity);
    case 17:
        return h.world->has<ParticleSystem>(h.entity);
    case 16:
        return h.world->has<Animation>(h.entity);
    case 15:
        return h.world->has<Raycast>(h.entity);
    default:
        return false;
    }
}
enum class Kind { Missing, Number, Integer, Boolean, String, Vec, Quat, Shape, Mesh };
struct Field {
    Kind kind = Kind::Missing;
    void* value = nullptr;
};
Field field(lua_State* L, const Component& c, const char* key) {
    const Handle& h = c.object;
    if (c.type == 2 && !std::strcmp(key, "convexDetail")) {
        key = "meshDetail";
    }
    if (!h.world->alive(h.entity) || !has(h, c.type)) {
        luaL_error(L, "Component no longer exists");
    }
#define FIELD(name, kind)                                                                          \
    if (!std::strcmp(key, #name))                                                                  \
        return {                                                                                   \
            Kind::kind, &v.name                                                                    \
        }
    if (c.type >= 9 && c.type <= 11) {
        LightSettings& v =
            c.type == 9    ? static_cast<LightSettings&>(h.world->get<DirectionalLight>(h.entity))
            : c.type == 10 ? static_cast<LightSettings&>(h.world->get<PointLight>(h.entity))
                           : static_cast<LightSettings&>(h.world->get<SpotLight>(h.entity));
        FIELD(color, Vec);
        FIELD(intensity, Number);
        FIELD(enabled, Boolean);
        FIELD(shadows, Boolean);
        FIELD(range, Number);
        FIELD(innerAngle, Number);
        FIELD(outerAngle, Number);
        FIELD(shadowDistance, Number);
        FIELD(shadowBias, Number);
        FIELD(shadowQuality, Integer);
        if (c.type == 11) {
            FIELD(cookie, String);
        }
    }
    switch (c.type) {
    case 13: {
        auto& v = h.world->get<CharacterController>(h.entity);
        FIELD(speed, Number);
        FIELD(sprintMultiplier, Number);
        FIELD(jumpSpeed, Number);
        FIELD(groundProbe, Number);
        FIELD(canMove, Boolean);
        FIELD(canJump, Boolean);
        FIELD(canSprint, Boolean);
        FIELD(isGrounded, Boolean);
        FIELD(isJumping, Boolean);
        FIELD(isMoving, Boolean);
        FIELD(isSprinting, Boolean);
        break;
    }
    case 15: {
        auto& v = h.world->get<Raycast>(h.entity);
        FIELD(type, Integer);
        FIELD(distance, Number);
        FIELD(radius, Number);
        FIELD(angle, Number);
        FIELD(samples, Integer);
        FIELD(offset, Vec);
        FIELD(includeTriggers, Boolean);
        break;
    }
    case 14: {
        auto& v = h.world->get<FPSCamera>(h.entity);
        FIELD(isFreecam, Boolean);
        FIELD(mouseLook, Boolean);
        FIELD(sensitivity, Number);
        FIELD(speed, Number);
        return field(L, Component{h, 6}, key);
    }
    case 0: {
        auto& v = h.world->get<Transform>(h.entity);
        FIELD(position, Vec);
        FIELD(rotation, Quat);
        FIELD(scale, Vec);
        break;
    }
    case 1: {
        auto& v = h.world->get<RigidBody>(h.entity);
        FIELD(mass, Number);
        FIELD(useGravity, Boolean);
        FIELD(floatOnRivers, Boolean);
        FIELD(linearVelocity, Vec);
        FIELD(angularVelocity, Vec);
        FIELD(drag, Number);
        FIELD(angularDrag, Number);
        FIELD(isKinematic, Boolean);
        break;
    }
    case 2: {
        auto& v = h.world->get<Collider>(h.entity);
        FIELD(size, Vec);
        FIELD(center, Vec);
        FIELD(isTrigger, Boolean);
        FIELD(friction, Number);
        FIELD(bounciness, Number);
        FIELD(radius, Number);
        FIELD(height, Number);
        FIELD(shape, Shape);
        FIELD(meshDetail, Integer);
        FIELD(model, String);
        break;
    }
    case 3: {
        auto& v = h.world->get<MeshRenderer>(h.entity);
        FIELD(color, Vec);
        FIELD(checkerboard, Boolean);
        FIELD(specular, Number);
        FIELD(mesh, Mesh);
        FIELD(model, String);
        FIELD(material, String);
        break;
    }
    case 4: {
        auto& v = h.world->get<ObjectName>(h.entity);
        FIELD(value, String);
        break;
    }
    case 5: {
        auto& v = h.world->get<LuaScript>(h.entity);
        FIELD(path, String);
        FIELD(enabled, Boolean);
        break;
    }
    case 12: {
        auto& v = h.world->get<Skybox>(h.entity);
        FIELD(affectEnvironment, Boolean);
        FIELD(environmentIntensity, Number);
        FIELD(type, Integer);
        FIELD(color, Vec);
        FIELD(enabled, Boolean);
        FIELD(right, String);
        FIELD(left, String);
        FIELD(top, String);
        FIELD(bottom, String);
        FIELD(front, String);
        FIELD(back, String);
        FIELD(hdr, String);
        FIELD(cubemap, String);
        break;
    }
    case 8: {
        auto& v = h.world->get<AudioListener>(h.entity);
        FIELD(enabled, Boolean);
        break;
    }
    case 6: {
        auto& v = h.world->get<Camera>(h.entity);
        FIELD(active, Boolean);
        FIELD(orthographic, Boolean);
        FIELD(fieldOfView, Number);
        FIELD(nearClip, Number);
        FIELD(farClip, Number);
        FIELD(orthographicSize, Number);
        break;
    }
    }
#undef FIELD
    luaL_error(L, "Unknown %s field: %s", types[c.type], key);
    return {};
}
float* coordinate(Field f, int i) {
    if (f.kind == Kind::Quat) {
        auto* q = static_cast<Quat*>(f.value);
        return i == 0 ? &q->x : i == 1 ? &q->y : i == 2 ? &q->z : &q->w;
    }
    auto* v = static_cast<Vec3*>(f.value);
    return i == 0 ? &v->x : i == 1 ? &v->y : &v->z;
}
const char* objectName(lua_State* L, int index) {
    std::size_t size = 0;
    const char* name = luaL_checklstring(L, index, &size);
    if (size > 256) {
        luaL_error(L, "Object names are limited to 256 bytes");
    }
    return name;
}
float number(lua_State* L, int index) {
    const double n = luaL_checknumber(L, index);
    if (!std::isfinite(n) || std::abs(n) > 1e6) {
        luaL_error(L, "Number must be finite and within +/-1000000");
    }
    return static_cast<float>(n);
}
Vec3 vectorArgument(lua_State* L, int index) {
    if (lua_type(L, index) == LUA_TNUMBER) {
        return {number(L, index), number(L, index + 1), number(L, index + 2)};
    }
    if (!lua_istable(L, index) && !luaL_testudata(L, index, "Vector")) {
        luaL_argerror(L, index, "Expected a Vector3/table or vector property");
    }
    Vec3 v;
    lua_getfield(L, index, "x");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_rawgeti(L, index, 1);
    }
    v.x = number(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, index, "y");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_rawgeti(L, index, 2);
    }
    v.y = number(L, -1);
    lua_pop(L, 1);
    lua_getfield(L, index, "z");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_rawgeti(L, index, 3);
    }
    v.z = number(L, -1);
    lua_pop(L, 1);
    return v;
}
Quat rotationArgument(lua_State* L, int index) {
    const Vec3 v = vectorArgument(L, index);
    if (luaL_testudata(L, index, "Vector")) {
        const auto property = *static_cast<Vector*>(lua_touserdata(L, index));
        const auto f = field(L, property.component, property.field);
        if (f.kind == Kind::Quat) {
            return Quat{v.x, v.y, v.z, *coordinate(f, 3)};
        }
        return composeRotation(
            composeRotation(axisAngle({0, 1, 0}, radians(v.y)), axisAngle({1, 0, 0}, radians(v.x))),
            axisAngle({0, 0, 1}, radians(v.z)));
    }
    const bool table = lua_istable(L, index);
    if (table) {
        lua_getfield(L, index, "w");
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_rawgeti(L, index, 4);
        }
    }
    const int wIndex = table ? -1 : index + 3;
    Quat result;
    if (lua_isnoneornil(L, wIndex)) {
        result = composeRotation(
            composeRotation(axisAngle({0, 1, 0}, radians(v.y)), axisAngle({1, 0, 0}, radians(v.x))),
            axisAngle({0, 0, 1}, radians(v.z)));
    } else {
        const float w = number(L, wIndex);
        if (v.x * v.x + v.y * v.y + v.z * v.z + w * w < 1e-12f) {
            luaL_error(L, "Rotation quaternion must not be zero");
        }
        result = normalize(Quat{v.x, v.y, v.z, w});
    }
    if (table) {
        lua_pop(L, 1);
    }
    return result;
}
void pushVector(lua_State* L, Vec3 v) {
    lua_createtable(L, 0, 3);
    lua_pushnumber(L, v.x);
    lua_setfield(L, -2, "x");
    lua_pushnumber(L, v.y);
    lua_setfield(L, -2, "y");
    lua_pushnumber(L, v.z);
    lua_setfield(L, -2, "z");
}
void component(lua_State* L, const Handle& h, int type) {
    auto* c = static_cast<Component*>(lua_newuserdatauv(L, sizeof(Component), 0));
    *c = {h, type};
    luaL_setmetatable(L, "Component");
}
int trace(lua_State* L) {
    luaL_traceback(
        L, L, lua_type(L, 1) == LUA_TSTRING ? lua_tostring(L, 1) : "Lua callback error", 1);
    return 1;
}
} // namespace
LuaScene::LuaScene(lua_State* L,
                   World& world,
                   MeshHandle cube,
                   MeshHandle sphere,
                   MeshHandle capsule,
                   MeshHandle cone,
                   std::function<void(std::string_view)> error,
                   MeshHandle plane)
    : state_(L), world_(world), meshes_{cube, sphere, capsule, cone, plane},
      error_(std::move(error)) {
    // Keep lifecycle keys interned even when a script exhausts its allocation budget.
    for (const char* key : {"onStart", "onUpdate", "onFixedUpdate", "onDestroy"}) {
        lua_pushboolean(L, 1);
        lua_setfield(L, LUA_REGISTRYINDEX, key);
    }
    lua_newtable(L);
    const char* colorNames[]{"red",
                             "green",
                             "blue",
                             "white",
                             "black",
                             "yellow",
                             "cyan",
                             "magenta",
                             "gray",
                             "orange",
                             "purple",
                             "pink"};
    const Vec3 colors[]{{1, 0, 0},
                        {0, 1, 0},
                        {0, 0, 1},
                        {1, 1, 1},
                        {0, 0, 0},
                        {1, 1, 0},
                        {0, 1, 1},
                        {1, 0, 1},
                        {.5f, .5f, .5f},
                        {1, .5f, 0},
                        {.5f, 0, .5f},
                        {1, .4f, .7f}};
    for (int i = 0; i < 12; ++i) {
        pushVector(L, colors[i]);
        lua_setfield(L, -2, colorNames[i]);
    }
    lua_setglobal(L, "color");
    lua_newtable(L);
    lua_pushcfunction(L, [](lua_State* state) -> int {
        auto* ui = GameUI::current();
        const char* id = luaL_checkstring(state, 1);
        const char* text = luaL_checkstring(state, 2);
        lua_pushboolean(state, ui && ui->setText(id, text));
        return 1;
    });
    lua_setfield(L, -2, "setText");
    lua_pushcfunction(L, [](lua_State* state) -> int {
        auto* ui = GameUI::current();
        const char* id = luaL_checkstring(state, 1);
        lua_pushboolean(state, ui && ui->takeClick(id));
        return 1;
    });
    lua_setfield(L, -2, "wasClicked");
    lua_pushcfunction(L, [](lua_State* state) -> int {
        auto* ui = GameUI::current();
        lua_pushboolean(state, ui && ui->loaded());
        return 1;
    });
    lua_setfield(L, -2, "isLoaded");
    lua_setglobal(L, "ui");

    const char* metas[]{"GameObject", "Component", "Vector"};
    const lua_CFunction reads[]{objectIndex, componentIndex, vectorIndex};
    const lua_CFunction writes[]{objectNewIndex, componentNewIndex, vectorNewIndex};
    for (int i = 0; i < 3; ++i) {
        luaL_newmetatable(L, metas[i]);
        lua_pushcfunction(L, reads[i]);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, writes[i]);
        lua_setfield(L, -2, "__newindex");
        lua_pushliteral(L, "locked");
        lua_setfield(L, -2, "__metatable");
        lua_pop(L, 1);
    }
    // Opaque type tokens cannot be forged by passing a number or a table.
    luaL_newmetatable(L, "ComponentType");
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, componentTypeIndex, 1);
    lua_setfield(L, -2, "__index");
    lua_pushliteral(L, "locked");
    lua_setfield(L, -2, "__metatable");
    lua_pop(L, 1);
    for (int i = 0; i < 30; ++i) {
        auto* type = static_cast<int*>(lua_newuserdatauv(L, sizeof(int), 0));
        *type = i;
        luaL_setmetatable(L, "ComponentType");
        if (i == 2) {
            lua_pushvalue(L, -1);
            lua_setglobal(L, "BoxCollider");
        }
        lua_setglobal(L, types[i]);
    }
    lua_pushcclosure(
        L,
        [](lua_State* L) -> int {
            const float x = lua_isnoneornil(L, 1) ? 0 : number(L, 1),
                        y = lua_isnoneornil(L, 2) ? 0 : number(L, 2),
                        z = lua_isnoneornil(L, 3) ? 0 : number(L, 3);
            lua_createtable(L, 0, 3);
            lua_pushnumber(L, x);
            lua_setfield(L, -2, "x");
            lua_pushnumber(L, y);
            lua_setfield(L, -2, "y");
            lua_pushnumber(L, z);
            lua_setfield(L, -2, "z");
            return 1;
        },
        0);
    lua_setglobal(L, "Vector3");
    luaL_newmetatable(L, "AudioWaitToken");
    lua_pushliteral(L, "Audio event");
    lua_setfield(L, -2, "__metatable");
    lua_pop(L, 1);
    for (int seconds = 0; seconds < 2; ++seconds) {
        lua_pushlightuserdata(L, this);
        lua_pushboolean(L, seconds);
        lua_pushcclosure(L, waitAPI, 2);
        lua_setglobal(L, seconds ? "waitForSeconds" : "wait");
    }
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, spawnAPI, 1);
    lua_setglobal(L, "spawn");
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, taskAPI, 1);
    lua_setglobal(L, "task");
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, destroyAPI, 1);
    lua_setglobal(L, "destroy");
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, audioApi, 1);
    lua_setglobal(L, "createAudioSource");
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, skyboxFadeAPI, 1);
    lua_setglobal(L, "fadeSkybox");
    lua_pushlightuserdata(L, this);
    lua_pushcclosure(L, fadeColorAPI, 1);
    lua_setglobal(L, "fadeToColor");
    lua_pushcfunction(
        L, +[](lua_State* L) -> int {
            luaL_checkstring(L, 1);
            lua_settop(L, 1);
            return 1;
        });
    lua_setglobal(L, "AudioClip");
    const char* cameraFunctions[]{"createCamera",
                                  "setActiveCamera",
                                  "getActiveCamera",
                                  "setCameraPosition",
                                  "setCameraRotation",
                                  "lookAt",
                                  "setFieldOfView",
                                  "setProjectionMode",
                                  "setNearClipPlane",
                                  "setFarClipPlane",
                                  "setViewport",
                                  "setCameraTarget",
                                  "setFollowTarget",
                                  "setCameraShake",
                                  "worldToScreen",
                                  "screenToWorld"};
    for (int i = 0; i < 16; ++i) {
        lua_pushlightuserdata(L, this);
        lua_pushinteger(L, i);
        lua_pushcclosure(L, cameraApi, 2);
        lua_setglobal(L, cameraFunctions[i]);
    }
    lua_newtable(L);
    lua_pushlightuserdata(L, this);
    lua_pushinteger(L, 0);
    lua_pushcclosure(L, api, 2);
    lua_setfield(L, -2, "new");
    lua_pushlightuserdata(L, this);
    lua_pushinteger(L, 5);
    lua_pushcclosure(L, api, 2);
    lua_setfield(L, -2, "find");
    lua_setglobal(L, "GameObject");
    const char* names[]{"createGameObject",
                        "destroyGameObject",
                        "getComponent",
                        "addComponent",
                        "removeComponent",
                        "findGameObjectByName",
                        "findGameObjectsByComponent",
                        "setParent",
                        "getParent",
                        "getComponentInParent",
                        "getComponentInChildren",
                        "findComponentInParent",
                        "findComponentInChildren"};
    for (int i = 0; i < 13; ++i) {
        lua_pushlightuserdata(L, this);
        lua_pushinteger(L, i);
        lua_pushcclosure(L, api, 2);
        lua_setglobal(L, names[i]);
    }
}
void LuaScene::pushObject(Entity e, bool owned) {
    for (auto& entry : entries_) {
        if (entry.entity == e) {
            lua_rawgeti(state_, LUA_REGISTRYINDEX, entry.reference);
            return;
        }
    }
    if (entries_.size() >= 4096) {
        luaL_error(state_, "GameObject handle limit (4096) exceeded");
        return;
    }
    auto* h = static_cast<Handle*>(lua_newuserdatauv(state_, sizeof(Handle), 1));
    *h = {this, &world_, e};
    luaL_setmetatable(state_, "GameObject");
    lua_newtable(state_);
    lua_setiuservalue(state_, -2, 1);
    lua_pushvalue(state_, -1);
    const int reference = luaL_ref(state_, LUA_REGISTRYINDEX);
    entries_.push_back({e, reference, owned, false, false});
}
int LuaScene::api(lua_State* L) {
    auto& self = *static_cast<LuaScene*>(lua_touserdata(L, lua_upvalueindex(1)));
    const int op = static_cast<int>(lua_tointeger(L, lua_upvalueindex(2)));
    if (((op >= 2 && op <= 4) || (op >= 9 && op <= 12)) && lua_gettop(L) == 1) {
        if (!self.world_.alive(self.selfEntity_)) {
            return luaL_error(L, "Implicit component access requires attached self");
        }
        self.pushObject(self.selfEntity_);
        lua_insert(L, 1);
    }
    if (op == 7 || op == 8) {
        const Handle child = handle(L, 1);
        if (op == 8) {
            const Entity p = getParent(self.world_, child.entity);
            if (p == Entity{}) {
                lua_pushnil(L);
            } else {
                self.pushObject(p);
            }
            return 1;
        }
        const Entity parent = lua_isnoneornil(L, 2) ? Entity{} : handle(L, 2).entity;
        if (!setParent(self.world_, child.entity, parent)) {
            return luaL_error(L, "Invalid parent: cycle, stale object or missing Transform");
        }
        return 0;
    }
    if (op == 0) {
        if (self.stopping_) {
            return luaL_error(L, "Cannot create objects during shutdown");
        }
        if (self.entries_.size() >= 4096) {
            return luaL_error(L, "GameObject handle limit exceeded");
        }
        const char* name = objectName(L, 1);
        // Complete fallible Lua allocations before adding anything to the ECS.
        self.pushObject({}, true);
        const Entity e = self.world_.create();
        self.world_.add(e, Transform{});
        self.world_.add(e, ObjectName{name});
        static_cast<Handle*>(lua_touserdata(L, -1))->entity = e;
        self.entries_.back().entity = e;
        return 1;
    }
    if (op == 5) {
        const char* name = luaL_checkstring(L, 1);
        Entity found;
        self.world_.each<ObjectName>([&](Entity e, ObjectName& n) {
            if (found == Entity{} && n.value == name) {
                found = e;
            }
        });
        if (found == Entity{}) {
            lua_pushnil(L);
        } else {
            self.pushObject(found);
        }
        return 1;
    }
    if (op == 6) {
        const int type = componentType(L, 1);
        // Keep collection outside Lua calls: an allocation error must not jump over a live vector.
        self.query_.clear();
        // Include objects without Transform as well.
        auto collect = [&](Entity e) { self.query_.push_back(e); };
        switch (type) {
        case 0:
            self.world_.each<Transform>([&](Entity e, Transform&) { collect(e); });
            break;
        case 1:
            self.world_.each<RigidBody>([&](Entity e, RigidBody&) { collect(e); });
            break;
        case 2:
            self.world_.each<Collider>([&](Entity e, Collider&) { collect(e); });
            break;
        case 3:
            self.world_.each<MeshRenderer>([&](Entity e, MeshRenderer&) { collect(e); });
            break;
        case 4:
            self.world_.each<ObjectName>([&](Entity e, ObjectName&) { collect(e); });
            break;
        case 5:
            self.world_.each<LuaScript>([&](Entity e, LuaScript&) { collect(e); });
            break;
        case 9:
            self.world_.each<DirectionalLight>([&](Entity e, DirectionalLight&) { collect(e); });
            break;
        case 10:
            self.world_.each<PointLight>([&](Entity e, PointLight&) { collect(e); });
            break;
        case 11:
            self.world_.each<SpotLight>([&](Entity e, SpotLight&) { collect(e); });
            break;
        case 13:
            self.world_.each<CharacterController>(
                [&](Entity e, CharacterController&) { collect(e); });
            break;
        case 22:
            self.world_.each<CloudSystem>([&](Entity e, CloudSystem&) { collect(e); });
            break;
        case 25:
            self.world_.each<Water>([&](Entity e, Water&) { collect(e); });
            break;
        case 29:
            self.world_.each<NavMeshObstacle>([&](Entity e, NavMeshObstacle&) { collect(e); });
            break;
        case 28:
            self.world_.each<NavMeshAgent>([&](Entity e, NavMeshAgent&) { collect(e); });
            break;
        case 27:
            self.world_.each<TPSCamera>([&](Entity e, TPSCamera&) { collect(e); });
            break;
        case 26:
            self.world_.each<BoneAttachment>([&](Entity e, BoneAttachment&) { collect(e); });
            break;
        case 24:
            self.world_.each<Vehicle>([&](Entity e, Vehicle&) { collect(e); });
            break;
        case 23:
            self.world_.each<River>([&](Entity e, River&) { collect(e); });
            break;
        case 21:
            self.world_.each<Fog>([&](Entity e, Fog&) { collect(e); });
            break;
        case 20:
            self.world_.each<CameraEffects>([&](Entity e, CameraEffects&) { collect(e); });
            break;
        case 19:
            self.world_.each<LightShafts>([&](Entity e, LightShafts&) { collect(e); });
            break;
        case 18:
            self.world_.each<Laser>([&](Entity e, Laser&) { collect(e); });
            break;
        case 17:
            self.world_.each<ParticleSystem>([&](Entity e, ParticleSystem&) { collect(e); });
            break;
        case 16:
            self.world_.each<Animation>([&](Entity e, Animation&) { collect(e); });
            break;
        case 15:
            self.world_.each<Raycast>([&](Entity e, Raycast&) { collect(e); });
            break;
        case 14:
            self.world_.each<FPSCamera>([&](Entity e, FPSCamera&) { collect(e); });
            break;
        case 12:
            self.world_.each<Skybox>([&](Entity e, Skybox&) { collect(e); });
            break;
        case 8:
            self.world_.each<AudioListener>([&](Entity e, AudioListener&) { collect(e); });
            break;
        case 7:
            self.world_.each<AudioSource>([&](Entity e, AudioSource&) { collect(e); });
            break;
        case 6:
            self.world_.each<Camera>([&](Entity e, Camera&) { collect(e); });
            break;
        }
        lua_newtable(L);
        int index = 1;
        for (Entity e : self.query_) {
            self.pushObject(e);
            lua_rawseti(L, -2, index++);
        }
        return 1;
    }
    const Handle h = handle(L, 1);
    if (h.scene != &self) {
        return luaL_error(L, "Foreign GameObject");
    }
    if (op == 1) {
        self.destroy(h.entity);
        return 0;
    }
    const int type = componentType(L, 2);
    if (op >= 9 && op <= 12) {
        if (has(h, type)) {
            component(L, h, type);
            return 1;
        }
        Entity match;
        // Finish all C++ traversal allocations before calling back into Lua.
        // Include self; parent walks are nearest-first, children use breadth-first order.
        {
            std::unordered_set<std::uint32_t> visited;
            self.query_.clear();
            self.query_.push_back(h.entity);
            visited.insert(h.entity.index);
            const bool parents = op == 9 || op == 11;
            std::unordered_map<std::uint32_t, std::vector<Entity>> children;
            if (!parents) {
                self.world_.each<Parent>([&](Entity child, const Parent& parent) {
                    if (self.world_.alive(parent.entity)) {
                        children[parent.entity.index].push_back(child);
                    }
                });
            }
            for (std::size_t index = 0; index < self.query_.size(); ++index) {
                const auto entity = self.query_[index];
                if (!self.world_.alive(entity)) {
                    continue;
                }
                if (has(Handle{h.scene, h.world, entity}, type)) {
                    match = entity;
                    break;
                }
                if (parents) {
                    const auto parent = getParent(self.world_, entity);
                    if (parent != Entity{} && visited.insert(parent.index).second) {
                        self.query_.push_back(parent);
                    }
                } else {
                    const auto found = children.find(entity.index);
                    if (found != children.end()) {
                        for (auto child : found->second) {
                            if (self.world_.alive(child) && visited.insert(child.index).second) {
                                self.query_.push_back(child);
                            }
                        }
                    }
                }
            }
        }
        if (match == Entity{}) {
            lua_pushnil(L);
        } else {
            component(L, Handle{h.scene, h.world, match}, type);
        }
        return 1;
    }
    if (op == 2) {
        if (has(h, type)) {
            component(L, h, type);
        } else {
            lua_pushnil(L);
        }
        return 1;
    }
    if (op == 4) {
        if (type == 0 &&
            (self.world_.has<CloudSystem>(h.entity) || self.world_.has<River>(h.entity))) {
            return luaL_error(L, "Remove CloudSystem / River before Transform");
        }
        if (((type == 0 || type == 1 || type == 2) &&
             (self.world_.has<CharacterController>(h.entity) ||
              self.world_.has<Vehicle>(h.entity))) ||
            ((type == 0 || type == 6) &&
             (self.world_.has<FPSCamera>(h.entity) || self.world_.has<TPSCamera>(h.entity)))) {
            return luaL_error(
                L, "Remove the dependent Vehicle, CharacterController or FPSCamera first");
        }
        switch (type) {
        case 0:
            self.world_.remove<Transform>(h.entity);
            break;
        case 1:
            self.world_.remove<RigidBody>(h.entity);
            break;
        case 2:
            self.world_.remove<Collider>(h.entity);
            break;
        case 3:
            self.world_.remove<MeshRenderer>(h.entity);
            break;
        case 4:
            self.world_.remove<ObjectName>(h.entity);
            break;
        case 5:
            self.world_.remove<LuaScript>(h.entity);
            break;
        case 6:
            self.world_.remove<Camera>(h.entity);
            break;
        case 7:
            self.world_.remove<AudioSource>(h.entity);
            break;
        case 8:
            self.world_.remove<AudioListener>(h.entity);
            break;
        case 9:
            self.world_.remove<DirectionalLight>(h.entity);
            break;
        case 10:
            self.world_.remove<PointLight>(h.entity);
            break;
        case 11:
            self.world_.remove<SpotLight>(h.entity);
            break;
        case 13:
            self.world_.remove<CharacterController>(h.entity);
            break;
        case 22:
            self.world_.remove<CloudSystem>(h.entity);
            break;
        case 25:
            self.world_.remove<Water>(h.entity);
            break;
        case 29:
            self.world_.remove<NavMeshObstacle>(h.entity);
            break;
        case 28:
            self.world_.remove<NavMeshAgent>(h.entity);
            break;
        case 27:
            self.world_.remove<TPSCamera>(h.entity);
            break;
        case 26:
            self.world_.remove<BoneAttachment>(h.entity);
            break;
        case 24:
            self.world_.remove<Vehicle>(h.entity);
            break;
        case 23:
            self.world_.remove<River>(h.entity);
            break;
        case 21:
            self.world_.remove<Fog>(h.entity);
            break;
        case 20:
            self.world_.remove<CameraEffects>(h.entity);
            break;
        case 19:
            self.world_.remove<LightShafts>(h.entity);
            break;
        case 18:
            self.world_.remove<Laser>(h.entity);
            break;
        case 17:
            self.world_.remove<ParticleSystem>(h.entity);
            break;
        case 16:
            self.world_.remove<Animation>(h.entity);
            break;
        case 15:
            self.world_.remove<Raycast>(h.entity);
            break;
        case 14:
            self.world_.remove<FPSCamera>(h.entity);
            break;
        case 12:
            self.world_.remove<Skybox>(h.entity);
            break;
        }
        return 0;
    }
    if (!has(h, type)) {
        switch (type) {
        case 0:
            self.world_.add(h.entity, Transform{});
            break;
        case 1:
            self.world_.add(h.entity, RigidBody{});
            break;
        case 2:
            self.world_.add(h.entity, Collider{});
            break;
        case 3:
            self.world_.add(h.entity, MeshRenderer{self.meshes_[0]});
            break;
        case 4:
            self.world_.add(h.entity, ObjectName{"GameObject"});
            break;
        case 5:
            self.world_.add(h.entity, LuaScript{});
            break;
        case 6:
            self.world_.add(h.entity, Camera{});
            break;
        case 7:
            self.world_.add(h.entity, AudioSource{});
            break;
        case 9:
            if (!self.world_.has<Transform>(h.entity)) {
                self.world_.add(h.entity, Transform{});
            }
            self.world_.add(h.entity, DirectionalLight{});
            break;
        case 10:
            if (!self.world_.has<Transform>(h.entity)) {
                self.world_.add(h.entity, Transform{});
            }
            self.world_.add(h.entity, PointLight{});
            break;
        case 13:
            if (self.world_.has<Vehicle>(h.entity)) {
                return luaL_error(L, "Remove Vehicle before adding CharacterController");
            }
            addCharacterController(self.world_, h.entity);
            break;
        case 22:
            if (!self.world_.has<Transform>(h.entity)) {
                self.world_.add(h.entity, Transform{{0, 20, 0}});
            }
            self.world_.add(h.entity, CloudSystem{});
            break;
        case 25:
            if (self.world_.has<River>(h.entity)) {
                return luaL_error(L, "Remove River before adding Water");
            }
            if (!self.world_.has<Transform>(h.entity)) {
                self.world_.add(h.entity, Transform{});
            }
            self.world_.add(h.entity, Water{});
            break;
        case 29:
            if (!self.world_.has<Transform>(h.entity)) {
                self.world_.add(h.entity, Transform{});
            }
            self.world_.add(h.entity, NavMeshObstacle{});
            break;
        case 28:
            if (!self.world_.has<Transform>(h.entity)) {
                self.world_.add(h.entity, Transform{});
            }
            self.world_.add(h.entity, NavMeshAgent{});
            break;
        case 27:
            if (self.world_.has<FPSCamera>(h.entity)) {
                return luaL_error(L, "Remove FPSCamera first");
            }
            if (!self.world_.has<Transform>(h.entity)) {
                self.world_.add(h.entity, Transform{});
            }
            if (!self.world_.has<Camera>(h.entity)) {
                self.world_.add(h.entity, Camera{});
            }
            self.world_.get<Camera>(h.entity).tracking = false;
            self.world_.get<Camera>(h.entity).following = false;
            self.world_.add(h.entity, TPSCamera{});
            break;
        case 26:
            if (!self.world_.has<Transform>(h.entity)) {
                self.world_.add(h.entity, Transform{});
            }
            self.world_.add(h.entity, BoneAttachment{});
            break;
        case 24:
            if (self.world_.has<CharacterController>(h.entity)) {
                return luaL_error(L, "Remove CharacterController before adding Vehicle");
            }
            if (!self.world_.has<Transform>(h.entity)) {
                self.world_.add(h.entity, Transform{});
            }
            if (!self.world_.has<RigidBody>(h.entity)) {
                RigidBody rb;
                rb.mass = 1000;
                self.world_.add(h.entity, rb);
            }
            if (!self.world_.has<Collider>(h.entity)) {
                Collider c;
                c.size = {1.8f, .6f, 4};
                self.world_.add(h.entity, c);
            }
            self.world_.add(h.entity, Vehicle{});
            break;
        case 23:
            if (self.world_.has<Water>(h.entity)) {
                return luaL_error(L, "Remove Water before adding River");
            }
            if (!self.world_.has<Transform>(h.entity)) {
                self.world_.add(h.entity, Transform{});
            }
            self.world_.add(h.entity, River{});
            break;
        case 21:
            self.world_.add(h.entity, Fog{});
            break;
        case 20:
            self.world_.add(h.entity, CameraEffects{});
            break;
        case 19:
            if (!self.world_.has<Transform>(h.entity)) {
                self.world_.add(h.entity, Transform{});
            }
            self.world_.add(h.entity, LightShafts{});
            break;
        case 18:
            if (!self.world_.has<Transform>(h.entity)) {
                self.world_.add(h.entity, Transform{});
            }
            self.world_.add(h.entity, Laser{});
            break;
        case 17:
            if (!self.world_.has<Transform>(h.entity)) {
                self.world_.add(h.entity, Transform{});
            }
            self.world_.add(h.entity, ParticleSystem{});
            break;
        case 16:
            self.world_.add(h.entity, Animation{});
            break;
        case 15:
            if (!self.world_.has<Transform>(h.entity)) {
                self.world_.add(h.entity, Transform{});
            }
            self.world_.add(h.entity, Raycast{});
            break;
        case 14:
            if (self.world_.has<TPSCamera>(h.entity)) {
                return luaL_error(L, "Remove TPSCamera first");
            }
            addFPSCamera(self.world_, h.entity);
            break;
        case 12:
            self.world_.add(h.entity, Skybox{});
            break;
        case 11:
            if (!self.world_.has<Transform>(h.entity)) {
                self.world_.add(h.entity, Transform{});
            }
            self.world_.add(h.entity, SpotLight{});
            break;
        case 8:
            if (!self.world_.has<Transform>(h.entity)) {
                self.world_.add(h.entity, Transform{});
            }
            self.world_.add(h.entity, AudioListener{});
            break;
        }
    }
    component(L, h, type);
    return 1;
}
// A bound closure supports both obj.getComponent(Type) and obj:getComponent(Type).
int LuaScene::objectMethod(lua_State* L) {
    const Handle bound = handle(L, lua_upvalueindex(3));
    auto* selfComponent = static_cast<Component*>(luaL_testudata(L, 1, "Component"));
    if (luaL_testudata(L, 1, "GameObject") || selfComponent) {
        if (selfComponent && (!selfComponent->object.world->alive(selfComponent->object.entity) ||
                              !has(selfComponent->object, selfComponent->type))) {
            return luaL_error(L, "Component no longer exists");
        }
        const Handle explicitSelf = selfComponent ? selfComponent->object : handle(L, 1);
        const int operation = static_cast<int>(lua_tointeger(L, lua_upvalueindex(2)));
        if (explicitSelf.entity == bound.entity && explicitSelf.scene == bound.scene &&
            !(operation == 7 && lua_gettop(L) == 1)) {
            lua_remove(L, 1);
        }
    }
    lua_pushvalue(L, lua_upvalueindex(3));
    lua_insert(L, 1);
    const int operation = static_cast<int>(lua_tointeger(L, lua_upvalueindex(2)));
    if (operation >= 200) {
        return objectApi(L);
    }
    if (operation >= 100) {
        return cameraApi(L);
    }
    return api(L);
}
int LuaScene::objectIndex(lua_State* L) {
    auto& h = handle(L, 1);
    const char* key = luaL_checkstring(L, 2);
    const char* methods[]{"getComponent", "addComponent", "removeComponent"};
    for (int i = 0; i < 3; ++i) {
        if (!std::strcmp(key, methods[i])) {
            lua_pushlightuserdata(L, h.scene);
            lua_pushinteger(L, i + 2);
            lua_pushvalue(L, 1);
            lua_pushcclosure(L, objectMethod, 3);
            return 1;
        }
    }
    struct Method {
        const char* name;
        int operation;
    };
    const Method objectMethods[]{{"setActive", 210},
                                 {"isActive", 211},
                                 {"setPosition", 200},
                                 {"getPosition", 201},
                                 {"setRotation", 202},
                                 {"getRotation", 203},
                                 {"setScale", 204},
                                 {"getScale", 205},
                                 {"translate", 206},
                                 {"lookAt", 207},
                                 {"setParent", 7},
                                 {"getParent", 8},
                                 {"destroy", 1},
                                 {"setName", 208},
                                 {"getName", 209},
                                 {"getForward", 220},
                                 {"getBackward", 221},
                                 {"getRight", 222},
                                 {"getLeft", 223},
                                 {"getUp", 224},
                                 {"getDown", 225},
                                 {"getComponentInParent", 9},
                                 {"getComponentInChildren", 10},
                                 {"findComponentInParent", 11},
                                 {"findComponentInChildren", 12}};
    for (const auto& method : objectMethods) {
        if (!std::strcmp(key, method.name)) {
            lua_pushlightuserdata(L, h.scene);
            lua_pushinteger(L, method.operation);
            lua_pushvalue(L, 1);
            lua_pushcclosure(L, objectMethod, 3);
            return 1;
        }
    }
    if ((h.world->has<CharacterController>(h.entity) &&
         (!std::strcmp(key, "move") || !std::strcmp(key, "jump") || !std::strcmp(key, "sprint"))) ||
        (h.world->has<FPSCamera>(h.entity) &&
         (!std::strcmp(key, "linkToCharacter") || !std::strcmp(key, "setIsFreecam") ||
          !std::strcmp(key, "getIsFreecam")))) {
        const int type =
            (!std::strcmp(key, "move") || !std::strcmp(key, "jump") || !std::strcmp(key, "sprint"))
                ? 13
                : 14;
        component(L, h, type);
        lua_replace(L, 1);
        return componentIndex(L);
    }
    if (h.world->has<Camera>(h.entity)) {
        const Method cameraMethods[]{{"setActive", 101},
                                     {"setFieldOfView", 106},
                                     {"setProjectionMode", 107},
                                     {"setNearClipPlane", 108},
                                     {"setFarClipPlane", 109},
                                     {"setViewport", 110},
                                     {"setTarget", 111},
                                     {"setCameraTarget", 111},
                                     {"setFollowTarget", 112},
                                     {"setShake", 113},
                                     {"setCameraShake", 113},
                                     {"worldToScreen", 114},
                                     {"screenToWorld", 115}};
        for (const auto& method : cameraMethods) {
            if (!std::strcmp(key, method.name)) {
                lua_pushlightuserdata(L, h.scene);
                lua_pushinteger(L, method.operation);
                lua_pushvalue(L, 1);
                lua_pushcclosure(L, objectMethod, 3);
                return 1;
            }
        }
    }
    if (!std::strcmp(key, "isFreecam") && h.world->has<FPSCamera>(h.entity)) {
        lua_pushboolean(L, h.world->get<FPSCamera>(h.entity).isFreecam);
        return 1;
    }
    if (!std::strcmp(key, "layer")) {
        lua_pushstring(L,
                       LayerSettings::names[LayerSettings::effective(*h.world, h.entity)].c_str());
        return 1;
    }
    if (!std::strcmp(key, "parent")) {
        const Entity p = getParent(*h.world, h.entity);
        if (p == Entity{}) {
            lua_pushnil(L);
        } else {
            h.scene->pushObject(p);
        }
        return 1;
    }
    if (!std::strcmp(key, "name")) {
        auto* n = h.world->tryGet<ObjectName>(h.entity);
        lua_pushstring(L, n ? n->value.c_str() : "");
        return 1;
    }
    lua_getiuservalue(L, 1, 1);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    return 1;
}
int LuaScene::objectNewIndex(lua_State* L) {
    auto& h = handle(L, 1);
    const char* key = luaL_checkstring(L, 2);
    if (!std::strcmp(key, "isFreecam") && h.world->has<FPSCamera>(h.entity)) {
        luaL_checktype(L, 3, LUA_TBOOLEAN);
        h.world->get<FPSCamera>(h.entity).isFreecam = lua_toboolean(L, 3);
        return 0;
    }
    if (!std::strcmp(key, "layer")) {
        int layer = lua_type(L, 3) == LUA_TNUMBER ? int(luaL_checkinteger(L, 3))
                                                  : LayerSettings::find(luaL_checkstring(L, 3));
        if (layer < 0 || layer > 31 || LayerSettings::names[layer].empty()) {
            return luaL_error(L, "Unknown layer");
        }
        h.world->add(h.entity, ObjectLayer{layer});
        return 0;
    }
    if (!std::strcmp(key, "parent")) {
        const Entity p = lua_isnil(L, 3) ? Entity{} : handle(L, 3).entity;
        if (!setParent(*h.world, h.entity, p)) {
            return luaL_error(L, "Invalid parent");
        }
        return 0;
    }
    if (!std::strcmp(key, "name")) {
        const char* n = objectName(L, 3);
        h.world->add(h.entity, ObjectName{n});
        return 0;
    }
    lua_getiuservalue(L, 1, 1);
    lua_pushvalue(L, 2);
    lua_pushvalue(L, 3);
    lua_rawset(L, -3);
    return 0;
}
int LuaScene::componentIndex(lua_State* L) {
    const auto c = *static_cast<Component*>(luaL_checkudata(L, 1, "Component"));
    const char* key = luaL_checkstring(L, 2);
    if (!c.object.world->alive(c.object.entity) || !has(c.object, c.type)) {
        return luaL_error(L, "Component no longer exists");
    }
    const char* sharedMethods[]{"getComponent",
                                "addComponent",
                                "removeComponent",
                                "getComponentInParent",
                                "getComponentInChildren",
                                "findComponentInParent",
                                "findComponentInChildren",
                                "setActive",
                                "isActive",
                                "setParent",
                                "getParent",
                                "destroy",
                                "getPosition",
                                "setPosition",
                                "getRotation",
                                "setRotation",
                                "getScale",
                                "setScale",
                                "getForward",
                                "getRight",
                                "getUp",
                                "getBackward",
                                "getLeft",
                                "getDown",
                                "translate",
                                "lookAt",
                                "setName",
                                "getName"};
    bool shared = false;
    for (const auto* method : sharedMethods) {
        shared |= !std::strcmp(key, method);
    }
    if (c.type == 28 && !std::strcmp(key, "setRotation")) {
        lua_pushnil(L);
        return 1;
    }
    if (shared) {
        c.object.scene->pushObject(c.object.entity);
        lua_replace(L, 1);
        return objectIndex(L);
    }
    // Component aliases resolve on the same owner, without storing raw pointers.
    for (int type = 0; type < 30; ++type) {
        std::string alias = type == 14   ? "fpsCamera"
                            : type == 27 ? "tpsCamera"
                            : type == 28 ? "navMeshAgent"
                                         : types[type];
        alias[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(alias[0])));
        if (!std::strcmp(key, alias.c_str())) {
            if (has(c.object, type)) {
                component(L, c.object, type);
            } else {
                lua_pushnil(L);
            }
            return 1;
        }
    }
    if (!std::strcmp(key, "gameObject")) {
        c.object.scene->pushObject(c.object.entity);
        return 1;
    }
    if (!std::strcmp(key, "transform")) {
        if (c.object.world->has<Transform>(c.object.entity)) {
            component(L, c.object, 0);
        } else {
            lua_pushnil(L);
        }
        return 1;
    }
    if (c.type == 24) {
        const auto& v = c.object.world->get<Vehicle>(c.object.entity);
        if (!std::strcmp(key, "throttle")) {
            lua_pushnumber(L, v.throttle);
            return 1;
        }
        if (!std::strcmp(key, "steering")) {
            lua_pushnumber(L, v.steering);
            return 1;
        }
        if (!std::strcmp(key, "brake")) {
            lua_pushnumber(L, v.brake);
            return 1;
        }
        if (!std::strcmp(key, "handbrake")) {
            lua_pushboolean(L, v.handbrake);
            return 1;
        }
        if (!std::strcmp(key, "speed")) {
            lua_pushnumber(L, v.speed);
            return 1;
        }
        if (!std::strcmp(key, "groundedWheels")) {
            lua_pushinteger(L, v.groundedWheels);
            return 1;
        }
        if (!std::strcmp(key, "isGrounded")) {
            lua_pushboolean(L, v.groundedWheels > 0);
            return 1;
        }
        if (!std::strcmp(key, "wheelCompression")) {
            lua_createtable(L, 4, 0);
            for (int i = 0; i < 4; ++i) {
                lua_pushnumber(L, v.compression[i]);
                lua_rawseti(L, -2, i + 1);
            }
            return 1;
        }
        const auto j = Vehicles::settings(v);
        if (!j.contains(key)) {
            return luaL_error(L, "Unknown Vehicle property: %s", key);
        }
        if (j.at(key).is_boolean()) {
            lua_pushboolean(L, j.at(key).get<bool>());
        } else {
            lua_pushnumber(L, j.at(key).get<double>());
        }
        return 1;
    }
    if (c.type == 29) {
        const auto j =
            Navigation::obstacleSettings(c.object.world->get<NavMeshObstacle>(c.object.entity));
        if (!j.contains(key)) {
            return luaL_error(L, "Unknown NavMeshObstacle property");
        }
        if (j.at(key).is_boolean()) {
            lua_pushboolean(L, j.at(key).get<bool>());
        } else {
            pushVector(L,
                       {j.at(key).at(0).get<float>(),
                        j.at(key).at(1).get<float>(),
                        j.at(key).at(2).get<float>()});
        }
        return 1;
    }

    if (c.type == 28) {
        auto& a = c.object.world->get<NavMeshAgent>(c.object.entity);
        if (!std::strcmp(key, "setDestination") || !std::strcmp(key, "resetPath")) {
            lua_pushstring(L, key);
            lua_pushcclosure(
                L,
                [](lua_State* L) -> int {
                    const auto c = *static_cast<Component*>(luaL_checkudata(L, 1, "Component"));
                    if (!c.object.world->alive(c.object.entity) ||
                        !c.object.world->has<NavMeshAgent>(c.object.entity)) {
                        return luaL_error(L, "NavMeshAgent no longer exists");
                    }
                    auto& a = c.object.world->get<NavMeshAgent>(c.object.entity);
                    if (!std::strcmp(lua_tostring(L, lua_upvalueindex(1)), "resetPath")) {
                        a.destinationSet = false;
                        a.requested = a.pathPending = a.hasPath = false;
                        a.corners.clear();
                        a.velocity = {};
                        a.remainingDistance = 0;
                        a.status = "Idle";
                        return 0;
                    }
                    a.destination = vectorArgument(L, 2);
                    a.destinationSet = true;
                    a.requested = a.pathPending = true;
                    a.stopped = false;
                    a.status = "Pending";
                    return 0;
                },
                1);
            return 1;
        }
        if (!std::strcmp(key, "velocity") || !std::strcmp(key, "destination")) {
            const auto v = !std::strcmp(key, "velocity") ? a.velocity : a.destination;
            lua_createtable(L, 0, 3);
            lua_pushnumber(L, v.x);
            lua_setfield(L, -2, "x");
            lua_pushnumber(L, v.y);
            lua_setfield(L, -2, "y");
            lua_pushnumber(L, v.z);
            lua_setfield(L, -2, "z");
            return 1;
        }
        if (!std::strcmp(key, "hasPath")) {
            lua_pushboolean(L, a.hasPath);
            return 1;
        }
        if (!std::strcmp(key, "pathPending")) {
            lua_pushboolean(L, a.pathPending);
            return 1;
        }
        if (!std::strcmp(key, "stopped")) {
            lua_pushboolean(L, a.stopped);
            return 1;
        }
        if (!std::strcmp(key, "remainingDistance")) {
            lua_pushnumber(L, a.remainingDistance);
            return 1;
        }
        if (!std::strcmp(key, "status")) {
            lua_pushstring(L, a.status.c_str());
            return 1;
        }
        const auto j = Navigation::agentSettings(a);
        if (!j.contains(key)) {
            return luaL_error(L, "Unknown NavMeshAgent property");
        }
        if (j.at(key).is_boolean()) {
            lua_pushboolean(L, j.at(key).get<bool>());
        } else {
            lua_pushnumber(L, j.at(key).get<float>());
        }
        return 1;
    }
    if (c.type == 27) {
        const auto& t = c.object.world->get<TPSCamera>(c.object.entity);
        if (!std::strcmp(key, "target")) {
            if (c.object.world->alive(t.target)) {
                c.object.scene->pushObject(t.target);
            } else {
                lua_pushnil(L);
            }
            return 1;
        }
        if (!std::strcmp(key, "resolvedDistance")) {
            lua_pushnumber(L, t.resolvedDistance);
            return 1;
        }
        auto j = TPSCameras::settings(t);
        if (!j.contains(key)) {
            return luaL_error(L, "Unknown TPSCamera property");
        }
        auto v = j.at(key);
        if (v.is_boolean()) {
            lua_pushboolean(L, v.get<bool>());
        } else if (v.is_number()) {
            lua_pushnumber(L, v.get<double>());
        } else {
            lua_createtable(L, 0, 3);
            const char* names[] = {"x", "y", "z"};
            for (int i = 0; i < 3; ++i) {
                lua_pushnumber(L, v[i].get<float>());
                lua_setfield(L, -2, names[i]);
            }
        }
        return 1;
    }
    if (c.type == 26) {
        const auto& b = c.object.world->get<BoneAttachment>(c.object.entity);
        if (!std::strcmp(key, "source")) {
            if (c.object.world->alive(b.source)) {
                c.object.scene->pushObject(b.source);
            } else {
                lua_pushnil(L);
            }
            return 1;
        }
        if (!std::strcmp(key, "error")) {
            lua_pushstring(L, b.error.c_str());
            return 1;
        }
        const auto j = BoneAttachments::settings(b);
        if (!j.contains(key)) {
            return luaL_error(L, "Unknown BoneAttachment property");
        }
        const auto& v = j.at(key);
        if (v.is_boolean()) {
            lua_pushboolean(L, v.get<bool>());
        } else if (v.is_number()) {
            lua_pushnumber(L, v.get<double>());
        } else {
            lua_createtable(L, 0, 3);
            const char* axes[] = {"x", "y", "z"};
            for (int i = 0; i < 3; ++i) {
                lua_pushnumber(L, v[i].get<float>());
                lua_setfield(L, -2, axes[i]);
            }
        }
        return 1;
    }
    if (c.type == 22 || c.type == 23 || c.type == 25) {
        auto j =
            c.type == 22 ? Environment::settings(c.object.world->get<CloudSystem>(c.object.entity))
            : c.type == 25 ? Waters::settings(c.object.world->get<Water>(c.object.entity))
                           : Environment::settings(c.object.world->get<River>(c.object.entity));
        if (!j.contains(key)) {
            return luaL_error(L, "Unknown environment property: %s", key);
        }
        const auto& value = j.at(key);
        auto pushVector = [&](const Json& v) {
            lua_createtable(L, 0, 3);
            const char* axes[]{"x", "y", "z"};
            for (int i = 0; i < 3; ++i) {
                lua_pushnumber(L, v.at(i).get<float>());
                lua_setfield(L, -2, axes[i]);
            }
        };
        if (value.is_boolean()) {
            lua_pushboolean(L, value.get<bool>());
        } else if (value.is_number()) {
            lua_pushnumber(L, value.get<double>());
        } else if (value.is_string()) {
            lua_pushstring(L, value.get_ref<const std::string&>().c_str());
        } else if (!std::strcmp(key, "points")) {
            lua_createtable(L, int(value.size()), 0);
            int i = 1;
            for (const auto& v : value) {
                pushVector(v);
                lua_rawseti(L, -2, i++);
            }
        } else {
            pushVector(value);
        }
        return 1;
    }
    if (c.type == 21) {
        const auto& f = c.object.world->get<Fog>(c.object.entity);
        if (!std::strcmp(key, "enabled")) {
            lua_pushboolean(L, f.enabled);
        } else if (!std::strcmp(key, "density")) {
            lua_pushnumber(L, f.density);
        } else if (!std::strcmp(key, "start")) {
            lua_pushnumber(L, f.start);
        } else if (!std::strcmp(key, "end")) {
            lua_pushnumber(L, f.end);
        } else if (!std::strcmp(key, "color")) {
            lua_createtable(L, 0, 3);
            for (auto pair : {std::pair<const char*, float>{"x", f.color.x},
                              {"y", f.color.y},
                              {"z", f.color.z}}) {
                lua_pushnumber(L, pair.second);
                lua_setfield(L, -2, pair.first);
            }
        } else {
            return luaL_error(L, "Unknown Fog property: %s", key);
        }
        return 1;
    }
    if (c.type == 20) {
        const auto& fx = c.object.world->get<CameraEffects>(c.object.entity);
        for (const auto& f : CameraFx::boolFields) {
            if (!std::strcmp(key, f.name)) {
                lua_pushboolean(L, fx.*f.member);
                return 1;
            }
        }
        for (const auto& f : CameraFx::floatFields) {
            if (!std::strcmp(key, f.name)) {
                lua_pushnumber(L, fx.*f.member);
                return 1;
            }
        }
        return luaL_error(L, "Unknown CameraEffects property: %s", key);
    }
    if (c.type == 19) {
        if (!c.object.world->alive(c.object.entity) || !has(c.object, 19)) {
            return luaL_error(L, "LightShafts no longer exists");
        }
        const auto& p = c.object.world->get<LightShafts>(c.object.entity);
        bool found = false;
        {
            const auto data = Shafts::settings(p);
            const auto it = data.find(key);
            if (it != data.end()) {
                found = true;
                const auto& v = *it;
                if (v.is_boolean()) {
                    lua_pushboolean(L, v.get<bool>());
                } else if (v.is_number()) {
                    lua_pushnumber(L, v.get<double>());
                } else if (v.is_string()) {
                    const auto value = v.get<std::string>();
                    lua_pushlstring(L, value.data(), value.size());
                } else {
                    lua_newtable(L);
                    const char* axes[]{"x", "y", "z"};
                    for (int a = 0; a < 3; ++a) {
                        lua_pushnumber(L, v[a].get<double>());
                        lua_setfield(L, -2, axes[a]);
                        lua_pushnumber(L, v[a].get<double>());
                        lua_rawseti(L, -2, a + 1);
                    }
                }
            }
        }
        if (!found) {
            return luaL_error(L, "Unknown LightShafts property: %s", key);
        }
        return 1;
    }
    if (c.type == 18) {
        if (!c.object.world->alive(c.object.entity) || !has(c.object, 18)) {
            return luaL_error(L, "Laser no longer exists");
        }
        const auto& p = c.object.world->get<Laser>(c.object.entity);
        if (!std::strcmp(key, "hit")) {
            lua_pushboolean(L, p.hit);
            return 1;
        }
        if (!std::strcmp(key, "endPosition")) {
            pushVector(L, p.end);
            return 1;
        }
        if (!std::strcmp(key, "hitNormal")) {
            pushVector(L, p.hitNormal);
            return 1;
        }
        bool found = false;
        {
            const auto data = Lasers::settings(p);
            const auto it = data.find(key);
            if (it != data.end()) {
                found = true;
                const auto& v = *it;
                if (v.is_boolean()) {
                    lua_pushboolean(L, v.get<bool>());
                } else if (v.is_number()) {
                    lua_pushnumber(L, v.get<double>());
                } else if (v.is_string()) {
                    const auto value = v.get<std::string>();
                    lua_pushlstring(L, value.data(), value.size());
                } else {
                    lua_newtable(L);
                    const char* axes[]{"x", "y", "z"};
                    for (int a = 0; a < 3; ++a) {
                        lua_pushnumber(L, v[a].get<double>());
                        lua_setfield(L, -2, axes[a]);
                        lua_pushnumber(L, v[a].get<double>());
                        lua_rawseti(L, -2, a + 1);
                    }
                }
            }
        }
        if (!found) {
            return luaL_error(L, "Unknown Laser property: %s", key);
        }
        return 1;
    }
    if (c.type == 17) {
        if (!c.object.world->alive(c.object.entity) || !has(c.object, 17)) {
            return luaL_error(L, "ParticleSystem no longer exists");
        }
        const auto& p = c.object.world->get<ParticleSystem>(c.object.entity);
        const char* methods[]{
            "play", "pause", "resume", "stop", "restart", "clear", "emit", "setPreset"};
        for (int i = 0; i < 8; ++i) {
            if (!std::strcmp(key, methods[i])) {
                lua_pushvalue(L, 1);
                lua_pushinteger(L, i);
                lua_pushcclosure(L, particleMethod, 2);
                return 1;
            }
        }
        if (!std::strcmp(key, "isPlaying")) {
            lua_pushboolean(L, p.playing);
            return 1;
        }
        if (!std::strcmp(key, "paused")) {
            lua_pushboolean(L, p.paused);
            return 1;
        }
        if (!std::strcmp(key, "particleCount")) {
            lua_pushinteger(L, p.particles.size());
            return 1;
        }
        if (!std::strcmp(key, "time")) {
            lua_pushnumber(L, p.time);
            return 1;
        }
        bool found = false;
        {
            const auto data = Particles::settings(p);
            const auto it = data.find(key);
            if (it != data.end()) {
                found = true;
                const auto& v = *it;
                if (v.is_boolean()) {
                    lua_pushboolean(L, v.get<bool>());
                } else if (v.is_number()) {
                    lua_pushnumber(L, v.get<double>());
                } else if (v.is_string()) {
                    const auto value = v.get<std::string>();
                    lua_pushlstring(L, value.data(), value.size());
                } else {
                    lua_newtable(L);
                    const char* axes[]{"x", "y", "z"};
                    for (int a = 0; a < 3; ++a) {
                        lua_pushnumber(L, v[a].get<double>());
                        lua_setfield(L, -2, axes[a]);
                        lua_pushnumber(L, v[a].get<double>());
                        lua_rawseti(L, -2, a + 1);
                    }
                }
            }
        }
        if (!found) {
            return luaL_error(L, "Unknown ParticleSystem property: %s", key);
        }
        return 1;
    }
    if (c.type == 16) {
        const auto& a = c.object.world->get<Animation>(c.object.entity);
        const char* methods[]{"play", "stop", "pause", "resume", "reset", "crossFade"};
        for (int i = 0; i < 6; ++i) {
            if (!std::strcmp(key, methods[i])) {
                lua_pushvalue(L, 1);
                lua_pushinteger(L, i);
                lua_pushcclosure(L, animationMethod, 2);
                return 1;
            }
        }
        if (!std::strcmp(key, "clip")) {
            lua_pushstring(L, a.clip.c_str());
        } else if (!std::strcmp(key, "loop")) {
            lua_pushboolean(L, a.loop);
        } else if (!std::strcmp(key, "playOnStart")) {
            lua_pushboolean(L, a.playOnStart);
        } else if (!std::strcmp(key, "isPlaying")) {
            lua_pushboolean(L, a.isPlaying);
        } else if (!std::strcmp(key, "paused")) {
            lua_pushboolean(L, a.paused);
        } else if (!std::strcmp(key, "time")) {
            lua_pushnumber(L, a.time);
        } else if (!std::strcmp(key, "error")) {
            lua_pushstring(L, a.error.c_str());
        } else {
            return luaL_error(L, "Unknown Animation property: %s", key);
        }
        return 1;
    }
    if (c.type == 1) {
        if (!std::strcmp(key, "velocity")) {
            auto alias = c;
            const auto f = field(L, alias, "linearVelocity");
            auto* v = static_cast<Vector*>(lua_newuserdatauv(L, sizeof(Vector), 0));
            *v = {alias, {}};
            std::strcpy(v->field, "linearVelocity");
            luaL_setmetatable(L, "Vector");
            (void)f;
            return 1;
        }
        const char* methods[]{"addForce",
                              "addImpulse",
                              "setVelocity",
                              "getVelocity",
                              "addVelocity",
                              "movePosition",
                              "moveRotation",
                              "stop"};
        for (int i = 0; i < 8; ++i) {
            if (!std::strcmp(key, methods[i])) {
                lua_pushvalue(L, 1);
                lua_pushinteger(L, i);
                lua_pushcclosure(L, rigidBodyMethod, 2);
                return 1;
            }
        }
    }
    if (c.type == 15) {
        const char* methods[]{"cast", "castAll", "setLayers"};
        for (int i = 0; i < 3; ++i) {
            if (!std::strcmp(key, methods[i])) {
                lua_pushvalue(L, 1);
                lua_pushinteger(L, i);
                lua_pushcclosure(L, queryMethod, 2);
                return 1;
            }
        }
    }
    if (c.type == 13 || c.type == 14) {
        if (!c.object.world->alive(c.object.entity) || !has(c.object, c.type)) {
            return luaL_error(L, "Component no longer exists");
        }
        const char* names[]{
            "move", "jump", "sprint", "linkToCharacter", "setIsFreecam", "getIsFreecam"};
        for (int i = 0; i < 6; ++i) {
            if (!std::strcmp(key, names[i]) &&
                ((c.type == 13 && i < 3) || (c.type == 14 && i >= 3))) {
                lua_pushvalue(L, 1);
                lua_pushinteger(L, i);
                lua_pushcclosure(L, gameplayMethod, 2);
                return 1;
            }
        }
    }
    if (c.type == 14) {
        struct CameraMethod {
            const char* name;
            int operation;
        };
        const CameraMethod methods[]{{"setActive", 101},
                                     {"setFieldOfView", 106},
                                     {"setProjectionMode", 107},
                                     {"setNearClipPlane", 108},
                                     {"setFarClipPlane", 109},
                                     {"setViewport", 110},
                                     {"setTarget", 111},
                                     {"setCameraTarget", 111},
                                     {"setFollowTarget", 112},
                                     {"setShake", 113},
                                     {"setCameraShake", 113},
                                     {"worldToScreen", 114},
                                     {"screenToWorld", 115}};
        for (const auto& method : methods) {
            if (!std::strcmp(key, method.name)) {
                lua_pushlightuserdata(L, c.object.scene);
                lua_pushinteger(L, method.operation);
                lua_pushvalue(L, 1);
                lua_pushcclosure(L, fpsCameraMethod, 3);
                return 1;
            }
        }
    }
    if (c.type == 12) {
        if (!c.object.world->alive(c.object.entity) || !has(c.object, 12)) {
            return luaL_error(L, "Skybox no longer exists");
        }
        const char* methods[]{"setType",
                              "setColor",
                              "setEnabled",
                              "setHDR",
                              "setFace",
                              "setTextures",
                              "fadeTo",
                              "setCubemap"};
        for (int i = 0; i < 8; ++i) {
            if (!std::strcmp(key, methods[i])) {
                lua_pushvalue(L, 1);
                lua_pushinteger(L, i);
                lua_pushcclosure(L, skyboxMethod, 2);
                return 1;
            }
        }
    }
    if (c.type == 3) {
        if (!c.object.world->alive(c.object.entity) || !has(c.object, 3)) {
            return luaL_error(L, "MeshRenderer no longer exists");
        }
        const char* methods[]{"setMaterial",
                              "setMaterialColor",
                              "setMaterialTexture",
                              "setMaterialNormalMap",
                              "setMaterialShader",
                              "setMaterialNormalStrength",
                              "setMaterialTiling",
                              "setMaterialSpecular",
                              "resetMaterialOverrides"};
        for (int i = 0; i < 9; ++i) {
            if (!std::strcmp(key, methods[i])) {
                lua_pushvalue(L, 1);
                lua_pushinteger(L, i);
                lua_pushcclosure(L, materialMethod, 2);
                return 1;
            }
        }
    }
    if (c.type >= 9 && c.type <= 11) {
        const char* methods[]{"setColor",
                              "setIntensity",
                              "setRange",
                              "setEnabled",
                              "setShadows",
                              "setShadowQuality",
                              "setSpotAngles"};
        for (int i = 0; i < 7; ++i) {
            if (!std::strcmp(key, methods[i])) {
                lua_pushvalue(L, 1);
                lua_pushinteger(L, i);
                lua_pushcclosure(L, lightMethod, 2);
                return 1;
            }
        }
    }
    if (c.type == 8 && (!std::strcmp(key, "pause") || !std::strcmp(key, "start"))) {
        if (!c.object.world->alive(c.object.entity) ||
            !c.object.world->has<AudioListener>(c.object.entity)) {
            return luaL_error(L, "AudioListener no longer exists");
        }
        lua_pushvalue(L, 1);
        lua_pushboolean(L, !std::strcmp(key, "start"));
        lua_pushcclosure(L, listenerMethod, 2);
        return 1;
    }
    if (c.type == 7) {
        for (int event = 0; event < 5; ++event) {
            if (!std::strcmp(key, AudioSource::eventNames[event])) {
                if (!c.object.world->has<AudioSource>(c.object.entity)) {
                    return luaL_error(L, "AudioSource no longer exists");
                }
                auto* token =
                    static_cast<AudioWaitToken*>(lua_newuserdatauv(L, sizeof(AudioWaitToken), 0));
                *token = {c.object.world, c.object.entity, event};
                luaL_setmetatable(L, "AudioWaitToken");
                return 1;
            }
        }
        if (!c.object.world->alive(c.object.entity) ||
            !c.object.world->has<AudioSource>(c.object.entity)) {
            return luaL_error(L, "AudioSource no longer exists");
        }
        const char* names[]{"play",
                            "stop",
                            "pause",
                            "resume",
                            "setPitch",
                            "fadeIn",
                            "fadeOut",
                            "setVolume",
                            "mute",
                            "unmute",
                            "setLooping",
                            "setPan",
                            "setDistance",
                            "isPlaying",
                            "playOneShot",
                            "spatialBlend",
                            "setSpatialBlend"};
        for (int i = 0; i < 17; ++i) {
            if (!std::strcmp(key, names[i])) {
                lua_pushvalue(L, 1);
                lua_pushinteger(L, i);
                lua_pushcclosure(L, audioMethod, 2);
                return 1;
            }
        }
        auto& a = c.object.world->get<AudioSource>(c.object.entity);
        if (!std::strcmp(key, "clip")) {
            lua_pushstring(L, a.clip.c_str());
            return 1;
        }
        lua_pushnil(L);
        return 1;
    }
    const Field f = field(L, c, key);
    switch (f.kind) {
    case Kind::Integer:
        lua_pushinteger(L, *static_cast<int*>(f.value));
        break;
    case Kind::Number:
        lua_pushnumber(L, *static_cast<float*>(f.value));
        break;
    case Kind::Boolean:
        lua_pushboolean(L, *static_cast<bool*>(f.value));
        break;
    case Kind::String:
        lua_pushstring(L, static_cast<std::string*>(f.value)->c_str());
        break;
    case Kind::Shape:
        lua_pushstring(L,
                       (*static_cast<ColliderShape*>(f.value) == ColliderShape::Box)      ? "Box"
                       : (*static_cast<ColliderShape*>(f.value) == ColliderShape::Sphere) ? "Sphere"
                       : (*static_cast<ColliderShape*>(f.value) == ColliderShape::Capsule)
                           ? "Capsule"
                       : (*static_cast<ColliderShape*>(f.value) == ColliderShape::Mesh) ? "Mesh"
                                                                                        : "Cone");
        break;
    case Kind::Mesh: {
        const auto m = *static_cast<MeshHandle*>(f.value);
        const auto& meshes = c.object.scene->meshes_;
        lua_pushstring(L,
                       m.id == meshes[1].id   ? "Sphere"
                       : m.id == meshes[2].id ? "Capsule"
                       : m.id == meshes[3].id ? "Cone"
                       : m.id == meshes[4].id ? "Plane"
                                              : "Cube");
        break;
    }
    default: {
        auto* v = static_cast<Vector*>(lua_newuserdatauv(L, sizeof(Vector), 0));
        v->component = c;
        std::strncpy(v->field, key, 31);
        v->field[31] = 0;
        luaL_setmetatable(L, "Vector");
        break;
    }
    }
    return 1;
}
int LuaScene::componentNewIndex(lua_State* L) {
    const auto c = *static_cast<Component*>(luaL_checkudata(L, 1, "Component"));
    if (c.type == 29) {
        if (!c.object.world->alive(c.object.entity) || !has(c.object, c.type)) {
            return luaL_error(L, "Component no longer exists");
        }
        const char* key = luaL_checkstring(L, 2);
        if (c.type == 29) {
            auto& o = c.object.world->get<NavMeshObstacle>(c.object.entity);
            auto j = Navigation::obstacleSettings(o);
            if (!j.contains(key)) {
                return luaL_error(L, "Unknown obstacle property");
            }
            if (j.at(key).is_boolean()) {
                luaL_checktype(L, 3, LUA_TBOOLEAN);
                j[key] = bool(lua_toboolean(L, 3));
            } else {
                j[key] = Environment::vec(vectorArgument(L, 3));
            }
            try {
                o = Navigation::parseObstacle(j);
            } catch (const std::exception& e) {
                return luaL_error(L, "%s", e.what());
            }
            return 0;
        }
    }
    if (c.type == 28) {
        if (!c.object.world->alive(c.object.entity) || !has(c.object, 28)) {
            return luaL_error(L, "NavMeshAgent no longer exists");
        }
        auto& a = c.object.world->get<NavMeshAgent>(c.object.entity);
        const char* key = luaL_checkstring(L, 2);
        if (!std::strcmp(key, "destination")) {
            a.destination = vectorArgument(L, 3);
            a.destinationSet = true;
            a.requested = a.pathPending = true;
            a.status = "Pending";
            return 0;
        }
        if (!std::strcmp(key, "stopped")) {
            luaL_checktype(L, 3, LUA_TBOOLEAN);
            a.stopped = bool(lua_toboolean(L, 3));
            return 0;
        }
        auto j = Navigation::agentSettings(a);
        if (!j.contains(key)) {
            return luaL_error(L, "Unknown or read-only NavMeshAgent property");
        }
        if (j.at(key).is_boolean()) {
            luaL_checktype(L, 3, LUA_TBOOLEAN);
            j[key] = bool(lua_toboolean(L, 3));
        } else {
            j[key] = number(L, 3);
        }
        try {
            const auto next = Navigation::parseAgent(j);
            a.enabled = next.enabled;
            a.speed = next.speed;
            a.stoppingDistance = next.stoppingDistance;
            a.updateRotation = next.updateRotation;
            a.angularSpeed = next.angularSpeed;
        } catch (const std::exception& e) {
            return luaL_error(L, "%s", e.what());
        }
        return 0;
    }
    if (c.type == 27) {
        if (!c.object.world->alive(c.object.entity) || !has(c.object, 27)) {
            return luaL_error(L, "TPSCamera no longer exists");
        }
        auto& t = c.object.world->get<TPSCamera>(c.object.entity);
        const char* key = luaL_checkstring(L, 2);
        if (!std::strcmp(key, "target")) {
            if (lua_isnil(L, 3)) {
                t.target = {};
            } else {
                auto h = handle(L, 3);
                if (h.world != c.object.world || h.entity == c.object.entity) {
                    return luaL_error(L, "Invalid TPS target");
                }
                t.target = h.entity;
            }
            t.initialized = false;
            return 0;
        }
        auto j = TPSCameras::settings(t);
        if (!j.contains(key)) {
            return luaL_error(L, "Unknown or read-only TPSCamera property");
        }
        if (j.at(key).is_boolean()) {
            luaL_checktype(L, 3, LUA_TBOOLEAN);
            j[key] = bool(lua_toboolean(L, 3));
        } else if (j.at(key).is_number()) {
            j[key] = luaL_checknumber(L, 3);
        } else {
            j[key] = Environment::vec(vectorArgument(L, 3));
        }
        try {
            auto next = TPSCameras::parse(j);
            next.target = t.target;
            next.initialized = t.initialized;
            next.resolvedDistance = t.resolvedDistance;
            next.previousPosition = t.previousPosition;
            t = next;
        } catch (const std::exception& ex) {
            return luaL_error(L, "%s", ex.what());
        }
        return 0;
    }
    if (c.type == 26) {
        if (!c.object.world->alive(c.object.entity) || !has(c.object, 26)) {
            return luaL_error(L, "BoneAttachment no longer exists");
        }
        auto& b = c.object.world->get<BoneAttachment>(c.object.entity);
        const char* key = luaL_checkstring(L, 2);
        if (!std::strcmp(key, "source")) {
            b.source = lua_isnil(L, 3) ? Entity{} : handle(L, 3).entity;
            return 0;
        }
        auto j = BoneAttachments::settings(b);
        if (!j.contains(key)) {
            return luaL_error(L, "Unknown or read-only BoneAttachment property");
        }
        if (j.at(key).is_boolean()) {
            luaL_checktype(L, 3, LUA_TBOOLEAN);
            j[key] = bool(lua_toboolean(L, 3));
        } else if (j.at(key).is_number_integer()) {
            j[key] = luaL_checkinteger(L, 3);
        } else {
            j[key] = Environment::vec(vectorArgument(L, 3));
        }
        try {
            auto next = BoneAttachments::parse(j);
            next.source = b.source;
            b = next;
        } catch (const std::exception& ex) {
            return luaL_error(L, "%s", ex.what());
        }
        return 0;
    }

    if (c.type == 24) {
        if (!c.object.world->alive(c.object.entity) || !has(c.object, 24)) {
            return luaL_error(L, "Vehicle no longer exists");
        }
        auto& v = c.object.world->get<Vehicle>(c.object.entity);
        const char* key = luaL_checkstring(L, 2);
        if (!std::strcmp(key, "handbrake")) {
            luaL_checktype(L, 3, LUA_TBOOLEAN);
            v.handbrake = lua_toboolean(L, 3);
            return 0;
        }
        if (!std::strcmp(key, "throttle") || !std::strcmp(key, "steering") ||
            !std::strcmp(key, "brake")) {
            const float value = number(L, 3);
            const float minimum = !std::strcmp(key, "brake") ? 0.f : -1.f;
            if (value < minimum || value > 1) {
                return luaL_error(L, "Vehicle input outside supported range");
            }
            if (!std::strcmp(key, "throttle")) {
                v.throttle = value;
            } else if (!std::strcmp(key, "steering")) {
                v.steering = value;
            } else {
                v.brake = value;
            }
            return 0;
        }
        auto j = Vehicles::settings(v);
        if (!j.contains(key)) {
            return luaL_error(L, "Unknown or read-only Vehicle property: %s", key);
        }
        if (j.at(key).is_boolean()) {
            luaL_checktype(L, 3, LUA_TBOOLEAN);
            j[key] = bool(lua_toboolean(L, 3));
        } else {
            j[key] = number(L, 3);
        }
        try {
            const auto next = Vehicles::parse(j);
            v.enabled = next.enabled;
            v.keepUpright = next.keepUpright;
#define COPY_VEHICLE(name, value, lo, hi) v.name = next.name;
            VEHICLE_SETTINGS(COPY_VEHICLE)
#undef COPY_VEHICLE
        } catch (const std::exception& e) {
            return luaL_error(L, "%s", e.what());
        }
        return 0;
    }

    if (c.type == 22 || c.type == 23 || c.type == 25) {
        if (!c.object.world->alive(c.object.entity) || !has(c.object, c.type)) {
            return luaL_error(L, "Environment component no longer exists");
        }
        const char* key = luaL_checkstring(L, 2);
        auto j =
            c.type == 22 ? Environment::settings(c.object.world->get<CloudSystem>(c.object.entity))
            : c.type == 25 ? Waters::settings(c.object.world->get<Water>(c.object.entity))
                           : Environment::settings(c.object.world->get<River>(c.object.entity));
        if (!j.contains(key)) {
            return luaL_error(L, "Unknown environment property: %s", key);
        }
        if (j.at(key).is_boolean()) {
            luaL_checktype(L, 3, LUA_TBOOLEAN);
            j[key] = bool(lua_toboolean(L, 3));
        } else if (j.at(key).is_number_integer()) {
            auto n = luaL_checkinteger(L, 3);
            if ((!std::strcmp(key, "shaderStyle") ? (n < 0 || n > 7) : (n < 2 || n > 32))) {
                return luaL_error(L, "Environment integer outside supported range");
            }
            j[key] = n;
        } else if (j.at(key).is_string()) {
            j[key] = luaL_checkstring(L, 3);
        } else if (j.at(key).is_number()) {
            j[key] = luaL_checknumber(L, 3);
        } else if (!std::strcmp(key, "points")) {
            luaL_checktype(L, 3, LUA_TTABLE);
            auto n = lua_rawlen(L, 3);
            if (n < 2 || n > 64) {
                return luaL_error(L, "River needs 2..64 points");
            }
            Json points = Json::array();
            for (std::size_t i = 1; i <= n; ++i) {
                lua_rawgeti(L, 3, i);
                points.push_back(Environment::vec(vectorArgument(L, lua_gettop(L))));
                lua_pop(L, 1);
            }
            j[key] = points;
        } else {
            j[key] = Environment::vec(vectorArgument(L, 3));
        }
        char error[256]{};
        try {
            if (c.type == 22) {
                c.object.world->get<CloudSystem>(c.object.entity) =
                    Environment::parse<CloudSystem>(j);
            } else if (c.type == 25) {
                c.object.world->get<Water>(c.object.entity) = Waters::parse(j);
            } else {
                bool edit = c.object.world->get<River>(c.object.entity).editCurve;
                auto next = Environment::parse<River>(j);
                next.editCurve = edit;
                c.object.world->get<River>(c.object.entity) = std::move(next);
            }
        } catch (const std::exception& e) {
            std::snprintf(error, sizeof(error), "%s", e.what());
        }
        if (error[0]) {
            return luaL_error(L, "%s", error);
        }
        return 0;
    }
    if (c.type == 21) {
        if (!c.object.world->alive(c.object.entity) || !has(c.object, 21)) {
            return luaL_error(L, "Fog no longer exists");
        }
        auto& f = c.object.world->get<Fog>(c.object.entity);
        auto next = f;
        const char* key = luaL_checkstring(L, 2);
        if (!std::strcmp(key, "enabled")) {
            luaL_checktype(L, 3, LUA_TBOOLEAN);
            next.enabled = lua_toboolean(L, 3);
        } else if (!std::strcmp(key, "color")) {
            next.color = vectorArgument(L, 3);
        } else if (!std::strcmp(key, "density")) {
            next.density = luaL_checknumber(L, 3);
        } else if (!std::strcmp(key, "start")) {
            next.start = luaL_checknumber(L, 3);
        } else if (!std::strcmp(key, "end")) {
            next.end = luaL_checknumber(L, 3);
        } else {
            return luaL_error(L, "Unknown Fog property: %s", key);
        }
        if (!Fogs::valid(next)) {
            return luaL_error(L, "Invalid Fog value or start/end range");
        }
        f = next;
        return 0;
    }
    if (c.type == 20) {
        if (!c.object.world->alive(c.object.entity) || !has(c.object, 20)) {
            return luaL_error(L, "CameraEffects no longer exists");
        }
        auto& fx = c.object.world->get<CameraEffects>(c.object.entity);
        const char* key = luaL_checkstring(L, 2);
        for (const auto& f : CameraFx::boolFields) {
            if (!std::strcmp(key, f.name)) {
                luaL_checktype(L, 3, LUA_TBOOLEAN);
                fx.*f.member = bool(lua_toboolean(L, 3));
                return 0;
            }
        }
        for (const auto& f : CameraFx::floatFields) {
            if (!std::strcmp(key, f.name)) {
                double value = luaL_checknumber(L, 3);
                if (!std::isfinite(value) || value < f.low || value > f.high) {
                    return luaL_error(L, "CameraEffects property outside supported range: %s", key);
                }
                fx.*f.member = float(value);
                return 0;
            }
        }
        return luaL_error(L, "Unknown CameraEffects property: %s", key);
    }
    if (c.type == 19) {
        if (!c.object.world->alive(c.object.entity) || !has(c.object, 19)) {
            return luaL_error(L, "LightShafts no longer exists");
        }
        const char* key = luaL_checkstring(L, 2);
        auto& p = c.object.world->get<LightShafts>(c.object.entity);
        auto& error = c.object.scene->apiError_;
        error.clear();
        try {
            auto data = Shafts::settings(p);
            auto it = data.find(key);
            if (it == data.end()) {
                throw std::runtime_error("Unknown or read-only LightShafts property");
            }
            if (it->is_boolean()) {
                if (!lua_isboolean(L, 3)) {
                    throw std::runtime_error("Expected boolean");
                }
                *it = bool(lua_toboolean(L, 3));
            } else if (it->is_number()) {
                if (lua_type(L, 3) != LUA_TNUMBER) {
                    throw std::runtime_error("Expected number");
                }
                const double v = lua_tonumber(L, 3);
                if (!std::isfinite(v) || std::abs(v) > 1e9) {
                    throw std::runtime_error("Invalid number");
                }
                if (it->is_number_integer()) {
                    if (std::floor(v) != v) {
                        throw std::runtime_error("Expected integer");
                    }
                    *it = int(v);
                } else {
                    *it = v;
                }
            } else if (it->is_string()) {
                if (lua_type(L, 3) != LUA_TSTRING) {
                    throw std::runtime_error("Expected texture path string");
                }
                std::size_t size;
                const auto* text = lua_tolstring(L, 3, &size);
                *it = std::string(text, size);
            } else {
                if (!lua_istable(L, 3)) {
                    throw std::runtime_error(
                        "Expected vector table {x,y,z} or {x=...,y=...,z=...}");
                }
                const char* axes[]{"x", "y", "z"};
                Json values = Json::array();
                for (int a = 0; a < 3; ++a) {
                    lua_getfield(L, 3, axes[a]);
                    if (lua_isnil(L, -1)) {
                        lua_pop(L, 1);
                        lua_rawgeti(L, 3, a + 1);
                    }
                    if (lua_type(L, -1) != LUA_TNUMBER) {
                        lua_pop(L, 1);
                        throw std::runtime_error("Expected three numeric vector values");
                    }
                    values.push_back(lua_tonumber(L, -1));
                    lua_pop(L, 1);
                }
                *it = values;
            }
            const auto settings = Shafts::parse(data);
            static_cast<LightShafts&>(p) = settings;
        } catch (const std::exception& e) {
            error = e.what();
        }
        if (!error.empty()) {
            lua_pushlstring(L, error.data(), error.size());
            error.clear();
            return lua_error(L);
        }
        return 0;
    }
    if (c.type == 18) {
        if (!c.object.world->alive(c.object.entity) || !has(c.object, 18)) {
            return luaL_error(L, "Laser no longer exists");
        }
        const char* key = luaL_checkstring(L, 2);
        auto& p = c.object.world->get<Laser>(c.object.entity);
        auto& error = c.object.scene->apiError_;
        error.clear();
        try {
            auto data = Lasers::settings(p);
            auto it = data.find(key);
            if (it == data.end()) {
                throw std::runtime_error("Unknown or read-only Laser property");
            }
            if (it->is_boolean()) {
                if (!lua_isboolean(L, 3)) {
                    throw std::runtime_error("Expected boolean");
                }
                *it = bool(lua_toboolean(L, 3));
            } else if (it->is_number()) {
                if (lua_type(L, 3) != LUA_TNUMBER) {
                    throw std::runtime_error("Expected number");
                }
                const double v = lua_tonumber(L, 3);
                if (!std::isfinite(v) || std::abs(v) > 1e9) {
                    throw std::runtime_error("Invalid number");
                }
                if (it->is_number_integer()) {
                    if (std::floor(v) != v) {
                        throw std::runtime_error("Expected integer");
                    }
                    *it = int(v);
                } else {
                    *it = v;
                }
            } else if (it->is_string()) {
                if (lua_type(L, 3) != LUA_TSTRING) {
                    throw std::runtime_error("Expected texture path string");
                }
                std::size_t size;
                const auto* text = lua_tolstring(L, 3, &size);
                *it = std::string(text, size);
            } else {
                if (!lua_istable(L, 3)) {
                    throw std::runtime_error(
                        "Expected vector table {x,y,z} or {x=...,y=...,z=...}");
                }
                const char* axes[]{"x", "y", "z"};
                Json values = Json::array();
                for (int a = 0; a < 3; ++a) {
                    lua_getfield(L, 3, axes[a]);
                    if (lua_isnil(L, -1)) {
                        lua_pop(L, 1);
                        lua_rawgeti(L, 3, a + 1);
                    }
                    if (lua_type(L, -1) != LUA_TNUMBER) {
                        lua_pop(L, 1);
                        throw std::runtime_error("Expected three numeric vector values");
                    }
                    values.push_back(lua_tonumber(L, -1));
                    lua_pop(L, 1);
                }
                *it = values;
            }
            const auto settings = Lasers::parse(data);
            static_cast<LaserSettings&>(p) = settings;
            p.ready = false;
            p.hit = false;
        } catch (const std::exception& e) {
            error = e.what();
        }
        if (!error.empty()) {
            lua_pushlstring(L, error.data(), error.size());
            error.clear();
            return lua_error(L);
        }
        return 0;
    }
    if (c.type == 17) {
        if (!c.object.world->alive(c.object.entity) || !has(c.object, 17)) {
            return luaL_error(L, "ParticleSystem no longer exists");
        }
        const char* key = luaL_checkstring(L, 2);
        auto& p = c.object.world->get<ParticleSystem>(c.object.entity);
        auto& error = c.object.scene->apiError_;
        error.clear();
        try {
            auto data = Particles::settings(p);
            auto it = data.find(key);
            if (it == data.end()) {
                throw std::runtime_error("Unknown or read-only ParticleSystem property");
            }
            if (it->is_boolean()) {
                if (!lua_isboolean(L, 3)) {
                    throw std::runtime_error("Expected boolean");
                }
                *it = bool(lua_toboolean(L, 3));
            } else if (it->is_number()) {
                if (lua_type(L, 3) != LUA_TNUMBER) {
                    throw std::runtime_error("Expected number");
                }
                const double v = lua_tonumber(L, 3);
                if (!std::isfinite(v) || std::abs(v) > 1e9) {
                    throw std::runtime_error("Invalid number");
                }
                if (it->is_number_integer()) {
                    if (std::floor(v) != v) {
                        throw std::runtime_error("Expected integer");
                    }
                    *it = int(v);
                } else {
                    *it = v;
                }
            } else if (it->is_string()) {
                if (lua_type(L, 3) != LUA_TSTRING) {
                    throw std::runtime_error("Expected texture path string");
                }
                std::size_t size;
                const auto* text = lua_tolstring(L, 3, &size);
                *it = std::string(text, size);
            } else {
                if (!lua_istable(L, 3)) {
                    throw std::runtime_error(
                        "Expected vector table {x,y,z} or {x=...,y=...,z=...}");
                }
                const char* axes[]{"x", "y", "z"};
                Json values = Json::array();
                for (int a = 0; a < 3; ++a) {
                    lua_getfield(L, 3, axes[a]);
                    if (lua_isnil(L, -1)) {
                        lua_pop(L, 1);
                        lua_rawgeti(L, 3, a + 1);
                    }
                    if (lua_type(L, -1) != LUA_TNUMBER) {
                        lua_pop(L, 1);
                        throw std::runtime_error("Expected three numeric vector values");
                    }
                    values.push_back(lua_tonumber(L, -1));
                    lua_pop(L, 1);
                }
                *it = values;
            }
            const auto settings = Particles::parse(data);
            const bool reset = settings.localSpace != p.localSpace || settings.seed != p.seed;
            static_cast<ParticleSettings&>(p) = settings;
            if (reset) {
                p.restart();
            }
        } catch (const std::exception& e) {
            error = e.what();
        }
        if (!error.empty()) {
            lua_pushlstring(L, error.data(), error.size());
            error.clear();
            return lua_error(L);
        }
        return 0;
    }
    if (c.type == 16) {
        if (!c.object.world->alive(c.object.entity) || !has(c.object, 16)) {
            return luaL_error(L, "Animation no longer exists");
        }
        auto& a = c.object.world->get<Animation>(c.object.entity);
        const char* key = luaL_checkstring(L, 2);
        if (!std::strcmp(key, "clip")) {
            const char* path = luaL_checkstring(L, 3);
            if (*path && !animationAsset(path)) {
                return luaL_error(L, "Expected project-relative .leanim clip");
            }
            a.clip = path;
        } else if (!std::strcmp(key, "loop")) {
            luaL_checktype(L, 3, LUA_TBOOLEAN);
            a.loop = lua_toboolean(L, 3);
        } else if (!std::strcmp(key, "playOnStart")) {
            luaL_checktype(L, 3, LUA_TBOOLEAN);
            a.playOnStart = lua_toboolean(L, 3);
        } else {
            return luaL_error(L, "Animation property is read-only or unknown: %s", key);
        }
        return 0;
    }
    if (c.type == 1 && !std::strcmp(luaL_checkstring(L, 2), "velocity")) {
        lua_pushliteral(L, "linearVelocity");
        lua_replace(L, 2);
    }
    if (c.type == 14) {
        const char* key = luaL_checkstring(L, 2);
        if (!std::strcmp(key, "active") || !std::strcmp(key, "orthographic") ||
            !std::strcmp(key, "fieldOfView") || !std::strcmp(key, "nearClip") ||
            !std::strcmp(key, "farClip") || !std::strcmp(key, "orthographicSize")) {
            if (!c.object.world->alive(c.object.entity) || !has(c.object, 14)) {
                return luaL_error(L, "FPSCamera no longer exists");
            }
            component(L, c.object, 6);
            lua_replace(L, 1);
            return componentNewIndex(L);
        }
    }
    if (c.type == 7) {
        if (!c.object.world->alive(c.object.entity) ||
            !c.object.world->has<AudioSource>(c.object.entity)) {
            return luaL_error(L, "AudioSource no longer exists");
        }
        if (!std::strcmp(luaL_checkstring(L, 2), "clip")) {
            c.object.world->get<AudioSource>(c.object.entity).clip = luaL_checkstring(L, 3);
            return 0;
        }
        return luaL_error(L, "Use AudioSource setter methods");
    }
    if (c.type == 3 && !std::strcmp(luaL_checkstring(L, 2), "material")) {
        if (!c.object.world->alive(c.object.entity) || !has(c.object, 3)) {
            return luaL_error(L, "MeshRenderer no longer exists");
        }
        std::string path = luaL_checkstring(L, 3);
        if (!path.empty() &&
            (!materialPathValid(path) || std::filesystem::path(path).extension() != ".mat")) {
            return luaL_error(L, "Expected project .mat path");
        }
        auto& r = c.object.world->get<MeshRenderer>(c.object.entity);
        r.material = path;
        r.materialOverrides = 0;
        return 0;
    }
    const char* gameplayKey = luaL_checkstring(L, 2);
    if (c.type == 13 &&
        (!std::strcmp(gameplayKey, "isGrounded") || !std::strcmp(gameplayKey, "isJumping") ||
         !std::strcmp(gameplayKey, "isMoving") || !std::strcmp(gameplayKey, "isSprinting"))) {
        return luaL_error(L, "Controller state is read-only");
    }
    const Field f = field(L, c, luaL_checkstring(L, 2));
    if (c.type == 6 && !std::strcmp(lua_tostring(L, 2), "active")) {
        luaL_checktype(L, 3, LUA_TBOOLEAN);
        if (lua_toboolean(L, 3)) {
            if (!c.object.world->has<Transform>(c.object.entity)) {
                return luaL_error(L, "Camera needs a Transform");
            }
            activateCamera(*c.object.world, c.object.entity);
        } else {
            c.object.world->get<Camera>(c.object.entity).active = false;
        }
        return 0;
    }
    switch (f.kind) {
    case Kind::Integer: {
        auto value = luaL_checkinteger(L, 3);
        if (c.type == 2 && (!std::strcmp(gameplayKey, "meshDetail") ||
                            !std::strcmp(gameplayKey, "convexDetail"))) {
            if (value < 2 || value > 15) {
                return luaL_error(L, "Mesh detail must be between 2 and 15");
            }
            *static_cast<int*>(f.value) = int(value);
            break;
        }
        if (value < 0 || value > (c.type == 15   ? (!std::strcmp(gameplayKey, "samples") ? 181 : 2)
                                  : c.type == 12 ? 1
                                                 : 3)) {
            return luaL_error(L, "Component integer out of range");
        }
        if (c.type == 15 && !std::strcmp(gameplayKey, "samples") && value < 2) {
            return luaL_error(L, "At least two fan samples required");
        }
        *static_cast<int*>(f.value) = int(value);
        break;
    }
    case Kind::Number: {
        const float value = number(L, 3);
        if (c.type == 12 && !std::strcmp(gameplayKey, "environmentIntensity") &&
            (value < 0 || value > 5)) {
            return luaL_error(L, "Environment intensity must be 0..5");
        }
        if ((c.type == 13 || c.type == 14 || c.type == 15 ||
             (c.type == 1 &&
              (!std::strcmp(gameplayKey, "drag") || !std::strcmp(gameplayKey, "angularDrag") ||
               !std::strcmp(gameplayKey, "mass")))) &&
            (value < 0 || value > 1000 ||
             (!std::strcmp(gameplayKey, "sprintMultiplier") && value < 1))) {
            return luaL_error(L, "Gameplay setting out of range");
        }
        if (c.type == 15 &&
            ((!std::strcmp(gameplayKey, "angle") && value > 360) ||
             ((!std::strcmp(gameplayKey, "radius") || !std::strcmp(gameplayKey, "distance")) &&
              value <= 0))) {
            return luaL_error(L, "Invalid raycast extent");
        }
        *static_cast<float*>(f.value) = value;
        break;
    }
    case Kind::Boolean:
        luaL_checktype(L, 3, LUA_TBOOLEAN);
        *static_cast<bool*>(f.value) = lua_toboolean(L, 3) != 0;
        break;
    case Kind::String: {
        if (c.type == 11 && !std::strcmp(gameplayKey, "cookie")) {
            std::size_t length = 0;
            const char* text = luaL_checklstring(L, 3, &length);
            const std::string path(text, length);
            if (!validLightCookie(path)) {
                return luaL_error(L, "Invalid spotlight cookie image path");
            }
            *static_cast<std::string*>(f.value) = path;
        } else if ((c.type == 2 || c.type == 3) && !std::strcmp(gameplayKey, "model")) {
            std::string path = luaL_checkstring(L, 3);
            if (!path.empty() && (!materialPathValid(path) || !modelAsset(path))) {
                return luaL_error(L, "Expected a project .mesh path");
            }
            *static_cast<std::string*>(f.value) = path;
            if (c.type == 3 && path.empty()) {
                c.object.world->get<MeshRenderer>(c.object.entity).mesh =
                    c.object.scene->meshes_[0];
            }
        } else if (c.type == 12) {
            std::size_t length = 0;
            const char* value = luaL_checklstring(L, 3, &length);
            std::string path(value, length);
            bool hdr = !std::strcmp(luaL_checkstring(L, 2), "hdr");
            if (!((!std::strcmp(lua_tostring(L, 2), "cubemap"))
                      ? (path.empty() || cubemapAsset(path))
                  : hdr ? skyboxHDRPath(path)
                        : materialTexturePath(path))) {
                return luaL_error(L, "Invalid Skybox texture path");
            }
            *static_cast<std::string*>(f.value) = path;
        } else {
            *static_cast<std::string*>(f.value) = objectName(L, 3);
        }
        break;
    }

    case Kind::Shape: {
        const char* s = luaL_checkstring(L, 3);
        int i = !std::strcmp(s, "Box")                                   ? 0
                : !std::strcmp(s, "Sphere")                              ? 1
                : !std::strcmp(s, "Capsule")                             ? 2
                : (!std::strcmp(s, "Mesh") || !std::strcmp(s, "Convex")) ? 4
                : !std::strcmp(s, "Cone")                                ? 3
                                                                         : -1;
        if (i < 0) {
            return luaL_error(L, "Expected Box, Sphere, Capsule, Cone or Mesh");
        }
        *static_cast<ColliderShape*>(f.value) = static_cast<ColliderShape>(i);
        break;
    }
    case Kind::Mesh: {
        const char* s = luaL_checkstring(L, 3);
        int i = !std::strcmp(s, "Cube")      ? 0
                : !std::strcmp(s, "Sphere")  ? 1
                : !std::strcmp(s, "Capsule") ? 2
                : !std::strcmp(s, "Cone")    ? 3
                : !std::strcmp(s, "Plane")   ? 4
                                             : -1;
        if (i < 0) {
            return luaL_error(L, "Expected Cube, Sphere, Capsule, Cone or Plane");
        }
        if (i == 4 && c.object.scene->meshes_[4].id == UINT32_MAX) {
            return luaL_error(L, "Plane mesh is not configured");
        }
        *static_cast<MeshHandle*>(f.value) = c.object.scene->meshes_[i];
        c.object.world->get<MeshRenderer>(c.object.entity).model.clear();
        break;
    }
    default: {
        float values[4]{};
        const char* keys[]{"x", "y", "z", "w"};
        const int count = f.kind == Kind::Quat ? 4 : 3;
        for (int i = 0; i < count; ++i) {
            lua_getfield(L, 3, keys[i]);
            values[i] = number(L, -1);
            lua_pop(L, 1);
        }
        if (count == 4) {
            *static_cast<Quat*>(f.value) =
                normalize(Quat{values[0], values[1], values[2], values[3]});
        } else {
            *static_cast<Vec3*>(f.value) = {values[0], values[1], values[2]};
        }
        break;
    }
    }
    return 0;
}
int LuaScene::vectorIndex(lua_State* L) {
    const auto v = *static_cast<Vector*>(luaL_checkudata(L, 1, "Vector"));
    const Field f = field(L, v.component, v.field);
    const char* key = luaL_checkstring(L, 2);
    const int i = !std::strcmp(key, "x")                             ? 0
                  : !std::strcmp(key, "y")                           ? 1
                  : !std::strcmp(key, "z")                           ? 2
                  : (f.kind == Kind::Quat && !std::strcmp(key, "w")) ? 3
                                                                     : -1;
    if (i < 0) {
        return luaL_error(L, "Unknown vector coordinate");
    }
    lua_pushnumber(L, *coordinate(f, i));
    return 1;
}
int LuaScene::vectorNewIndex(lua_State* L) {
    const auto v = *static_cast<Vector*>(luaL_checkudata(L, 1, "Vector"));
    const Field f = field(L, v.component, v.field);
    const char* key = luaL_checkstring(L, 2);
    const int i = !std::strcmp(key, "x")                             ? 0
                  : !std::strcmp(key, "y")                           ? 1
                  : !std::strcmp(key, "z")                           ? 2
                  : (f.kind == Kind::Quat && !std::strcmp(key, "w")) ? 3
                                                                     : -1;
    if (i < 0) {
        return luaL_error(L, "Unknown vector coordinate");
    }
    *coordinate(f, i) = number(L, 3);
    return 0;
}
void LuaScene::resume(Task& task, int arguments) {
    auto* main = state_;
    auto* previous = currentTask_;
    state_ = task.thread;
    currentTask_ = &task;
    int results = 0;
    int status = lua_resume(task.thread, main, arguments, &results);
    state_ = main;
    currentTask_ = previous;
    if (status != LUA_OK && status != LUA_YIELD) {
        luaL_traceback(main, task.thread, lua_tostring(task.thread, -1), 1);
        error_(lua_tostring(main, -1));
        lua_pop(main, 1);
        // Isolate callback failure; future callbacks and other objects still run.
    }
    if (status != LUA_YIELD) {
        task.seconds = -1;
    }
    lua_settop(task.thread, 0);
}
void LuaScene::call(
    const char* name, int reference, float dt, bool delta, Entity other, bool colliderArgument) {
    if (tasks_.size() >= 128) {
        error_("Too many waiting callbacks");
        return;
    }
    for (const auto& task : tasks_) {
        if (task.callback == name && task.objectReference == reference && task.seconds >= 0) {
            return;
        }
    }
    const int base = lua_gettop(state_);
    if (reference == LUA_NOREF) {
        lua_getglobal(state_, name);
    } else {
        lua_rawgeti(state_, LUA_REGISTRYINDEX, reference);
        lua_getiuservalue(state_, -1, 1);
        lua_getfield(state_, -1, name);
        lua_remove(state_, -2);
        lua_remove(state_, -2);
    }
    if (!lua_isfunction(state_, -1)) {
        lua_settop(state_, base);
        return;
    }
    auto* thread = lua_newthread(state_);
    int ref = luaL_ref(state_, LUA_REGISTRYINDEX);
    lua_xmove(state_, thread, 1);
    if (delta) {
        lua_pushnumber(thread, dt);
    }
    if (colliderArgument) {
        if (world_.alive(other) && world_.has<Collider>(other)) {
            component(state_, Handle{this, &world_, other}, 2);
        } else {
            lua_pushnil(state_);
        }
        lua_xmove(state_, thread, 1);
    }
    tasks_.push_back({thread, ref, reference, name});
    auto it = std::prev(tasks_.end());
    it->owner = selfEntity_;
    if (reference != LUA_NOREF) {
        for (const auto& entry : entries_) {
            if (entry.reference == reference) {
                it->owner = entry.entity;
                break;
            }
        }
    }
    resume(*it, delta || colliderArgument ? 1 : 0);
    if (it->seconds < 0) {
        luaL_unref(state_, LUA_REGISTRYINDEX, it->reference);
        tasks_.erase(it);
    }
    lua_settop(state_, base);
}
void LuaScene::advanceTasks(float dt) {
    const auto count = tasks_.size();
    auto it = tasks_.begin();
    for (std::size_t i = 0; i < count && it != tasks_.end(); ++i) {
        auto current = it++;
        if (current->owner != Entity{}) {
            if (!world_.alive(current->owner)) {
                luaL_unref(state_, LUA_REGISTRYINDEX, current->reference);
                tasks_.erase(current);
                continue;
            }
            if (!isActive(world_, current->owner)) {
                continue;
            }
        }
        bool ready = false;
        if (current->audioEvent >= 0) {
            auto* audio = world_.tryGet<AudioSource>(current->audioEntity);
            if (!audio) {
                luaL_unref(state_, LUA_REGISTRYINDEX, current->reference);
                tasks_.erase(current);
                continue;
            }
            ready = audio->eventSequence[current->audioEvent] != current->sequence;
        } else {
            current->seconds -= std::max(0.f, dt);
            ready = current->seconds <= 0;
        }
        if (ready) {
            current->seconds = 0;
            current->audioEvent = -1;
            resume(*current, 0);
        }
        if (current->seconds < 0) {
            luaL_unref(state_, LUA_REGISTRYINDEX, current->reference);
            tasks_.erase(current);
        }
        if (!active_) {
            break;
        }
    }
}
int LuaScene::taskAPI(lua_State* L) {
    auto& scene = *static_cast<LuaScene*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!scene.currentTask_ || scene.stopping_ || scene.currentTask_->callback == "onDestroy") {
        return luaL_error(
            L,
            "task requires a running lifecycle/object callback or task, not top-level/listener/shutdown code");
    }
    luaL_checktype(L, 1, LUA_TFUNCTION);
    if (scene.tasks_.size() >= 128) {
        return luaL_error(L, "Too many running/waiting tasks (maximum 128)");
    }
    // Capture owner and callback binding before resume changes currentTask_.
    // Registry ownership keeps the closure/upvalues alive after its parent ends.
    const Entity owner = scene.currentTask_->owner;
    const int objectReference = scene.currentTask_->objectReference;
    auto* thread = lua_newthread(L);
    const int reference = luaL_ref(L, LUA_REGISTRYINDEX);
    lua_pushvalue(L, 1);
    lua_xmove(L, thread, 1);
    scene.tasks_.push_back({thread, reference, objectReference, "<task>"});
    auto current = std::prev(scene.tasks_.end());
    current->owner = owner;
    // Run up to the first wait now, then let the caller continue. This is
    // cooperative scheduling on the script thread, not an OS background thread.
    scene.resume(*current, 0);
    if (current->seconds < 0) {
        luaL_unref(L, LUA_REGISTRYINDEX, current->reference);
        scene.tasks_.erase(current);
    }
    return 0;
}
int LuaScene::waitAPI(lua_State* L) {
    auto& scene = *static_cast<LuaScene*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (!scene.currentTask_ || scene.stopping_ || scene.currentTask_->callback == "onDestroy") {
        return luaL_error(L, "wait is only supported inside running lifecycle/object callbacks");
    }
    if (lua_toboolean(L, lua_upvalueindex(2)) || lua_type(L, 1) == LUA_TNUMBER) {
        float seconds = number(L, 1);
        if (seconds < 0 || seconds > 86400) {
            return luaL_error(L, "Wait seconds must be between 0 and 86400");
        }
        scene.currentTask_->seconds = seconds;
    } else {
        // Event tokens are opaque, generation-safe AudioSource references.
        auto* token = static_cast<struct AudioWaitToken*>(luaL_checkudata(L, 1, "AudioWaitToken"));
        if (token->world != &scene.world_) {
            return luaL_error(L, "Audio event belongs to another world");
        }
        auto* audio = scene.world_.tryGet<AudioSource>(token->entity);
        if (!audio) {
            return luaL_error(L, "AudioSource no longer exists");
        }
        scene.currentTask_->audioEntity = token->entity;
        scene.currentTask_->audioEvent = token->event;
        scene.currentTask_->sequence = audio->eventSequence[token->event];
    }
    return lua_yield(L, 0);
}
void LuaScene::objectEvent(Entity entity, const char* name, Entity other, bool colliderArgument) {
    if (!active_ || !isActive(world_, entity)) {
        return;
    }
    if (selfEntity_ == entity) {
        call(name, LUA_NOREF, 0, false, other, colliderArgument);
    }
    const auto count = entries_.size();
    for (std::size_t i = 0; i < count && active_; ++i) {
        if (entries_[i].entity == entity && !entries_[i].destroying) {
            call(name, entries_[i].reference, 0, false, other, colliderArgument);
        }
    }
}
void LuaScene::start() {
    for (auto& task : tasks_) {
        luaL_unref(state_, LUA_REGISTRYINDEX, task.reference);
    }
    tasks_.clear();
    started_ = true;
    active_ = true;
    call("onStart", LUA_NOREF, 0, false);
}
void LuaScene::destroy(Entity root) {
    const auto nodes = hierarchySubtree(world_, root);
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
        const Entity e = *it;
        if (!world_.alive(e)) {
            continue;
        }
        bool destroying = false;
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].entity == e) {
                destroying = entries_[i].destroying;
                if (!destroying) {
                    entries_[i].destroying = true;
                    const int ref = entries_[i].reference;
                    call("onDestroy", ref, 0, false);
                }
                break;
            }
        }
        if (!destroying) {
            world_.destroy(e);
        }
    }
}

void LuaScene::tick(const char* name, float dt) {
    // Snapshot count: objects created by callbacks start on the next tick.
    const std::size_t count = entries_.size();
    for (std::size_t i = 0; i < count; ++i) {
        if (!world_.alive(entries_[i].entity) && !entries_[i].destroying) {
            entries_[i].destroying = true;
            call("onDestroy", entries_[i].reference, 0, false);
        }
    }
    if (active_) {
        for (std::size_t i = 0; i < count && active_; ++i) {
            if (entries_[i].destroying || !isActive(world_, entries_[i].entity) ||
                entries_[i].started) {
                continue;
            }
            entries_[i].started = true;
            call("onStart", entries_[i].reference, 0, false);
        }
        if (active_) {
            call(name, LUA_NOREF, dt, true);
        }
        for (std::size_t i = 0; i < count && active_; ++i) {
            if (entries_[i].destroying || !isActive(world_, entries_[i].entity)) {
                continue;
            }
            call(name, entries_[i].reference, dt, true);
        }
    }
    for (std::size_t i = 0; i < entries_.size();) {
        if (!world_.alive(entries_[i].entity)) {
            luaL_unref(state_, LUA_REGISTRYINDEX, entries_[i].reference);
            entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(i));
        } else {
            ++i;
        }
    }
}
void LuaScene::update(float dt) {
    if (selfEntity_ != Entity{} && !isActive(world_, selfEntity_)) {
        return;
    }
    if (active_) {
        advanceTasks(dt);
    }
    tick("onUpdate", dt);
    updateHierarchy(world_);
}
void LuaScene::fixedUpdate(float dt) {
    if (selfEntity_ != Entity{} && !isActive(world_, selfEntity_)) {
        return;
    }
    tick("onFixedUpdate", dt);
    updateHierarchy(world_);
}
void LuaScene::stop() {
    if (stopping_) {
        return;
    }
    stopping_ = true;
    if (started_) {
        call("onDestroy", LUA_NOREF, 0, false);
    }
    started_ = false;
    active_ = false;
    for (std::size_t i = 0; i < entries_.size(); ++i) {
        if (!entries_[i].destroying) {
            entries_[i].destroying = true;
            call("onDestroy", entries_[i].reference, 0, false);
        }
        if (entries_[i].owned) {
            world_.destroy(entries_[i].entity);
        }
        luaL_unref(state_, LUA_REGISTRYINDEX, entries_[i].reference);
    }
    entries_.clear();
    for (auto& task : tasks_) {
        luaL_unref(state_, LUA_REGISTRYINDEX, task.reference);
    }
    tasks_.clear();
    stopping_ = false;
}

void LuaScene::setSelf(Entity entity) {
    selfEntity_ = entity;
    pushObject(entity);
    lua_setglobal(state_, "self");
}

#include "scripting/lua_camera.inc"

#include "scripting/lua_objects.inc"

int LuaScene::audioApi(lua_State* L) {
    auto& self = *static_cast<LuaScene*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (lua_gettop(L) == 0) {
        lua_getglobal(L, "self");
        if (lua_isnil(L, -1)) {
            return luaL_error(L, "createAudioSource requires an owner GameObject");
        }
    }
    const auto h = handle(L, 1);
    if (h.world != &self.world_) {
        return luaL_error(L, "Foreign GameObject");
    }
    if (!self.world_.has<AudioSource>(h.entity)) {
        self.world_.add(h.entity, AudioSource{});
    }
    if (!lua_isnoneornil(L, 2)) {
        self.world_.get<AudioSource>(h.entity).clip = luaL_checkstring(L, 2);
    }
    component(L, h, 7);
    return 1;
}
int LuaScene::skyboxMethod(lua_State* L) {
    const auto c = *static_cast<Component*>(luaL_checkudata(L, lua_upvalueindex(1), "Component"));
    if (!c.object.world->alive(c.object.entity) || !has(c.object, 12)) {
        return luaL_error(L, "Skybox no longer exists");
    }
    if (luaL_testudata(L, 1, "Component")) {
        lua_remove(L, 1);
    }
    auto& sky = c.object.world->get<Skybox>(c.object.entity);
    auto edited = sky;
    const int op = int(lua_tointeger(L, lua_upvalueindex(2)));
    if (op == 6) {
        Entity target;
        if (auto* component = static_cast<Component*>(luaL_testudata(L, 1, "Component"))) {
            if (component->object.world != c.object.world || component->type != 12) {
                return luaL_error(L, "Expected a Skybox component");
            }
            target = component->object.entity;
        } else {
            auto& object = handle(L, 1);
            if (object.world != c.object.world) {
                return luaL_error(L, "Skybox belongs to another world");
            }
            target = object.entity;
        }
        const float speed = number(L, 2);
        if (const char* error = beginSkyboxFade(*c.object.world, c.object.entity, target, speed)) {
            return luaL_error(L, "%s", error);
        }
        return 0;
    }
    if (op == 0) {
        if (lua_type(L, 1) == LUA_TSTRING) {
            const char* type = lua_tostring(L, 1);
            if (!std::strcmp(type, "HDR")) {
                edited.type = 1;
            } else if (!std::strcmp(type, "Textures") || !std::strcmp(type, "Cubemap")) {
                edited.type = 0;
            } else {
                return luaL_error(L, "Expected Textures or HDR");
            }
        } else {
            edited.type = int(luaL_checkinteger(L, 1));
        }
    }
    if (op == 1) {
        edited.color = {number(L, 1), number(L, 2), number(L, 3)};
    }
    if (op == 2) {
        luaL_checktype(L, 1, LUA_TBOOLEAN);
        edited.enabled = lua_toboolean(L, 1);
    }
    if (op == 3) {
        edited.hdr = lua_isnil(L, 1) ? "" : luaL_checkstring(L, 1);
        edited.type = 1;
    }
    if (op == 7) {
        edited.cubemap = lua_isnil(L, 1) ? "" : luaL_checkstring(L, 1);
        edited.type = 0;
    }
    if (op == 4) {
        const char* face = luaL_checkstring(L, 1);
        std::string* path = nullptr;
        if (!std::strcmp(face, "right")) {
            path = &edited.right;
        }
        if (!std::strcmp(face, "left")) {
            path = &edited.left;
        }
        if (!std::strcmp(face, "top")) {
            path = &edited.top;
        }
        if (!std::strcmp(face, "bottom")) {
            path = &edited.bottom;
        }
        if (!std::strcmp(face, "front")) {
            path = &edited.front;
        }
        if (!std::strcmp(face, "back")) {
            path = &edited.back;
        }
        if (!path) {
            return luaL_error(L, "Expected right, left, top, bottom, front or back");
        }
        *path = lua_isnil(L, 2) ? "" : luaL_checkstring(L, 2);
    }
    if (op == 5) {
        int i = 1;
        for (auto* path : {&edited.right,
                           &edited.left,
                           &edited.top,
                           &edited.bottom,
                           &edited.front,
                           &edited.back}) {
            *path = lua_isnil(L, i) ? "" : luaL_checkstring(L, i);
            ++i;
        }
    }
    if (!validSkybox(edited)) {
        return luaL_error(L, "Invalid Skybox type, color or project texture path");
    }
    sky = edited;
    return 0;
}
int LuaScene::materialMethod(lua_State* L) {
    const auto c = *static_cast<Component*>(luaL_checkudata(L, lua_upvalueindex(1), "Component"));
    if (!c.object.world->alive(c.object.entity) || !has(c.object, 3)) {
        return luaL_error(L, "MeshRenderer no longer exists");
    }
    auto& r = c.object.world->get<MeshRenderer>(c.object.entity);
    if (luaL_testudata(L, 1, "Component")) {
        lua_remove(L, 1);
    }
    const int op = int(lua_tointeger(L, lua_upvalueindex(2)));
    auto edited = r.materialValues;
    unsigned mask = r.materialOverrides;
    if (op == 0) {
        std::string path = luaL_checkstring(L, 1);
        if (!path.empty() &&
            (!materialPathValid(path) || std::filesystem::path(path).extension() != ".mat")) {
            return luaL_error(L, "Expected project .mat path");
        }
        r.material = path;
        r.materialOverrides = 0;
        return 0;
    }
    if (op == 1) {
        edited.color = {number(L, 1), number(L, 2), number(L, 3)};
        mask |= MaterialColor;
    }
    if (op == 2) {
        edited.texture = lua_isnil(L, 1) ? "" : luaL_checkstring(L, 1);
        mask |= MaterialTexture;
    }
    if (op == 3) {
        edited.normalMap = lua_isnil(L, 1) ? "" : luaL_checkstring(L, 1);
        mask |= MaterialNormal;
    }
    if (op == 4) {
        const char* shader = luaL_checkstring(L, 1);
        if (!std::strcmp(shader, "Bumped Diffuse")) {
            shader = "Diffuse Bumped";
        }
        if (std::strcmp(shader, "Diffuse") && std::strcmp(shader, "Diffuse Bumped") &&
            std::strcmp(shader, "Diffuse Transparent") &&
            std::strcmp(shader, "Diffuse Reflection")) {
            return luaL_error(
                L, "Expected Diffuse, Diffuse Bumped, Diffuse Transparent or Diffuse Reflection");
        }
        edited.shader = !std::strcmp(shader, "Diffuse Reflection") ? 3
                        : !std::strcmp(shader, "Diffuse Transparent")
                            ? 2
                            : !std::strcmp(shader, "Diffuse Bumped");
        mask |= MaterialShader;
    }
    if (op == 5) {
        edited.normalStrength = number(L, 1);
        mask |= MaterialStrength;
    }
    if (op == 6) {
        edited.tilingX = number(L, 1);
        edited.tilingY = number(L, 2);
        mask |= MaterialTiling;
    }
    if (op == 7) {
        edited.specular = number(L, 1);
        mask |= MaterialSpecular;
    }
    if (op == 8) {
        r.materialOverrides = 0;
        return 0;
    }
    if (!validMaterial(edited)) {
        return luaL_error(L, "Invalid material properties or texture path");
    }
    r.materialValues = edited;
    r.materialOverrides = mask;
    return 0;
}
int LuaScene::lightMethod(lua_State* L) {
    const auto c = *static_cast<Component*>(luaL_checkudata(L, lua_upvalueindex(1), "Component"));
    if (!c.object.world->alive(c.object.entity) || !has(c.object, c.type)) {
        return luaL_error(L, "Light no longer exists");
    }
    auto& world = *c.object.world;
    const auto e = c.object.entity;
    LightSettings& light = c.type == 9 ? static_cast<LightSettings&>(world.get<DirectionalLight>(e))
                           : c.type == 10 ? static_cast<LightSettings&>(world.get<PointLight>(e))
                                          : static_cast<LightSettings&>(world.get<SpotLight>(e));
    if (luaL_testudata(L, 1, "Component")) {
        lua_remove(L, 1);
    }
    auto edited = light;
    const int op = int(lua_tointeger(L, lua_upvalueindex(2)));
    if (op == 0) {
        edited.color = {number(L, 1), number(L, 2), number(L, 3)};
    }
    if (op == 1) {
        edited.intensity = number(L, 1);
    }
    if (op == 2) {
        edited.range = number(L, 1);
    }
    if (op == 3 || op == 4) {
        luaL_checktype(L, 1, LUA_TBOOLEAN);
        if (op == 3) {
            edited.enabled = lua_toboolean(L, 1);
        } else {
            edited.shadows = lua_toboolean(L, 1);
        }
    }
    if (op == 5) {
        auto quality = luaL_checkinteger(L, 1);
        if (quality < 0 || quality > 3) {
            return luaL_error(L, "Shadow quality must be 0..3");
        }
        edited.shadowQuality = int(quality);
    }
    if (op == 6) {
        edited.innerAngle = number(L, 1);
        edited.outerAngle = number(L, 2);
    }
    if (!validLight(edited)) {
        return luaL_error(L, "Invalid light settings");
    }
    light = edited;
    return 0;
}
int LuaScene::listenerMethod(lua_State* L) {
    const auto c = *static_cast<Component*>(luaL_checkudata(L, lua_upvalueindex(1), "Component"));
    if (!c.object.world->alive(c.object.entity) ||
        !c.object.world->has<AudioListener>(c.object.entity)) {
        return luaL_error(L, "AudioListener no longer exists");
    }
    auto& listener = c.object.world->get<AudioListener>(c.object.entity);
    if (lua_toboolean(L, lua_upvalueindex(2))) {
        listener.start();
    } else {
        listener.pause();
    }
    return 0;
}
int LuaScene::audioMethod(lua_State* L) {
    const auto c = *static_cast<Component*>(luaL_checkudata(L, lua_upvalueindex(1), "Component"));
    if (!c.object.world->alive(c.object.entity) ||
        !c.object.world->has<AudioSource>(c.object.entity)) {
        return luaL_error(L, "AudioSource no longer exists");
    }
    auto& a = c.object.world->get<AudioSource>(c.object.entity);
    if (luaL_testudata(L, 1, "Component")) {
        lua_remove(L, 1);
    }
    const int op = lua_tointeger(L, lua_upvalueindex(2));
    if (op == 14) {
        a.playOneShot(luaL_checkstring(L, 1));
        return 0;
    }
    if (op == 10) {
        luaL_checktype(L, 1, LUA_TBOOLEAN);
        lua_pushboolean(L, a.setLooping(lua_toboolean(L, 1)));
        return 1;
    }
    float value = 0;
    if (op == 4 || op == 5 || op == 6 || op == 7 || op == 11 || op == 12 || op == 15 || op == 16) {
        value = luaL_checknumber(L, 1);
        if (!std::isfinite(value)) {
            return luaL_error(L, "Expected finite audio value");
        }
    }
    switch (op) {
    case 0:
        a.play();
        break;
    case 1:
        a.stop();
        break;
    case 2:
        a.pause();
        break;
    case 3:
        a.resume();
        break;
    case 4:
        lua_pushnumber(L, a.setPitch(value));
        return 1;
    case 5:
        a.fadeIn(value);
        break;
    case 6:
        a.fadeOut(value);
        break;
    case 7:
        lua_pushnumber(L, a.setVolume(value));
        return 1;
    case 8:
        a.mute();
        break;
    case 9:
        a.unmute();
        break;
    case 11:
        lua_pushnumber(L, a.setPan(value));
        return 1;
    case 12:
        lua_pushnumber(L, a.setDistance(value));
        return 1;
    case 13:
        lua_pushboolean(L, a.isPlaying());
        return 1;
    case 15:
    case 16:
        lua_pushnumber(L, a.spatialBlend(value));
        return 1;
    }
    return 0;
}

int LuaScene::fpsCameraMethod(lua_State* L) {
    const auto c = *static_cast<Component*>(luaL_checkudata(L, lua_upvalueindex(3), "Component"));
    if (!c.object.world->alive(c.object.entity) || !has(c.object, 14)) {
        return luaL_error(L, "FPSCamera no longer exists");
    }
    if (auto* receiver = static_cast<Component*>(luaL_testudata(L, 1, "Component"))) {
        if (receiver->type == 14 && receiver->object.world == c.object.world &&
            receiver->object.entity == c.object.entity) {
            lua_remove(L, 1);
        }
    }
    c.object.scene->pushObject(c.object.entity);
    lua_insert(L, 1);
    return cameraApi(L);
}

int LuaScene::rigidBodyMethod(lua_State* L) {
    const auto c = *static_cast<Component*>(luaL_checkudata(L, lua_upvalueindex(1), "Component"));
    if (!c.object.world->alive(c.object.entity) || !has(c.object, 1)) {
        return luaL_error(L, "RigidBody no longer exists");
    }
    if (auto* receiver = static_cast<Component*>(luaL_testudata(L, 1, "Component"));
        receiver && receiver->object.entity == c.object.entity && receiver->type == 1) {
        lua_remove(L, 1);
    }
    auto& rb = c.object.world->get<RigidBody>(c.object.entity);
    const int op = int(lua_tointeger(L, lua_upvalueindex(2)));
    if (op == 3) {
        pushVector(L, rb.linearVelocity);
        return 1;
    }
    if (op == 7) {
        rb.linearVelocity = rb.angularVelocity = rb.force = rb.impulse = {};
        return 0;
    }
    const auto value = vectorArgument(L, 1);
    if (op == 0) {
        rb.force = rb.force + value;
    }
    if (op == 1) {
        rb.impulse = rb.impulse + value;
    }
    if (op == 2) {
        rb.linearVelocity = value;
    }
    if (op == 4) {
        rb.linearVelocity = rb.linearVelocity + value;
    }
    if (op == 5 || op == 6) {
        auto* pose = c.object.world->tryGet<Transform>(c.object.entity);
        if (!pose) {
            return luaL_error(L, "Transform required");
        }
        if (op == 5) {
            pose->position = value;
        } else {
            pose->rotation = rotationArgument(L, 1);
        }
    }
    return 0;
}
int LuaScene::queryMethod(lua_State* L) {
    const auto c = *static_cast<Component*>(luaL_checkudata(L, lua_upvalueindex(1), "Component"));
    if (!c.object.world->alive(c.object.entity) || !has(c.object, 15)) {
        return luaL_error(L, "Raycast no longer exists");
    }
    if (auto* receiver = static_cast<Component*>(luaL_testudata(L, 1, "Component"));
        receiver && receiver->object.entity == c.object.entity && receiver->type == 15) {
        lua_remove(L, 1);
    }
    auto& query = c.object.world->get<Raycast>(c.object.entity);
    const int op = int(lua_tointeger(L, lua_upvalueindex(2)));
    if (op == 2) {
        luaL_checktype(L, 1, LUA_TTABLE);
        unsigned mask = 0;
        for (lua_Integer i = 1; i <= luaL_len(L, 1); ++i) {
            lua_geti(L, 1, i);
            const int id = LayerSettings::find(luaL_checkstring(L, -1));
            lua_pop(L, 1);
            if (id < 0) {
                return luaL_error(L, "Unknown layer");
            }
            mask |= 1u << id;
        }
        query.layerMask = mask;
        return 0;
    }
    const auto hits =
        sceneQuery ? sceneQuery(*c.object.world, c.object.entity, query) : std::vector<RayHit>{};
    auto pushHit = [&](const RayHit& hit) {
        lua_createtable(L, 0, 6);
        const Handle h{c.object.scene, c.object.world, hit.entity};
        component(L, h, 2);
        lua_setfield(L, -2, "collider");
        component(L, h, 0);
        lua_setfield(L, -2, "transform");
        c.object.scene->pushObject(hit.entity);
        lua_setfield(L, -2, "gameObject");
        pushVector(L, hit.point);
        lua_setfield(L, -2, "point");
        pushVector(L, hit.normal);
        lua_setfield(L, -2, "normal");
        lua_pushnumber(L, hit.distance);
        lua_setfield(L, -2, "distance");
    };
    if (op == 0) {
        if (hits.empty()) {
            lua_pushnil(L);
        } else {
            pushHit(hits.front());
        }
        return 1;
    }
    lua_createtable(L, int(hits.size()), 0);
    int index = 1;
    for (const auto& hit : hits) {
        pushHit(hit);
        lua_rawseti(L, -2, index++);
    }
    return 1;
}

int LuaScene::gameplayMethod(lua_State* L) {
    const auto c = *static_cast<Component*>(luaL_checkudata(L, lua_upvalueindex(1), "Component"));
    auto& world = *c.object.world;
    const Entity e = c.object.entity;
    if (!world.alive(e) || !has(c.object, c.type)) {
        return luaL_error(L, "Component no longer exists");
    }
    if (auto* receiver = static_cast<Component*>(luaL_testudata(L, 1, "Component"))) {
        if (receiver->type == c.type && receiver->object.world == &world &&
            receiver->object.entity == e) {
            lua_remove(L, 1);
        }
    } else if (auto* receiver = static_cast<Handle*>(luaL_testudata(L, 1, "GameObject"))) {
        if (receiver->world == &world && receiver->entity == e) {
            lua_remove(L, 1);
        }
    }
    const int op = int(lua_tointeger(L, lua_upvalueindex(2)));
    if (c.type == 13) {
        auto& controller = world.get<CharacterController>(e);
        if (op == 0) {
            Vec3 movement;
            if (lua_type(L, 1) == LUA_TTABLE) {
                movement = vectorArgument(L, 1);
            } else if (lua_gettop(L) >= 3) {
                movement = {number(L, 1), number(L, 2), number(L, 3)};
            } else {
                movement = {number(L, 1), 0, number(L, 2)};
            }
            movement.y = 0;
            const float length = std::sqrt(dot(movement, movement));
            if (length > 1) {
                movement = movement * (1 / length);
            }
            controller.movement = movement;
        }
        if (op == 1) {
            controller.jumpRequested = true;
        }
        if (op == 2) {
            if (!lua_isnone(L, 1)) {
                luaL_checktype(L, 1, LUA_TBOOLEAN);
            }
            controller.sprintRequested = lua_isnone(L, 1) || lua_toboolean(L, 1);
        }
        return 0;
    }
    auto& fps = world.get<FPSCamera>(e);
    if (op == 4) {
        luaL_checktype(L, 1, LUA_TBOOLEAN);
        fps.isFreecam = lua_toboolean(L, 1);
        return 0;
    }
    if (op == 5) {
        lua_pushboolean(L, fps.isFreecam);
        return 1;
    }
    Entity character;
    if (auto* ref = static_cast<Component*>(luaL_testudata(L, 1, "Component"))) {
        if (ref->type != 13 || ref->object.world != &world) {
            return luaL_error(L, "Expected CharacterController");
        }
        character = ref->object.entity;
    } else {
        auto target = handle(L, 1);
        if (target.world != &world) {
            return luaL_error(L, "Foreign character");
        }
        character = target.entity;
    }
    if (!world.has<CharacterController>(character) || !world.has<Transform>(character)) {
        return luaL_error(L, "Expected CharacterController GameObject");
    }
    const Vec3 offset = lua_isnoneornil(L, 2) ? Vec3{0, .7f, 0} : vectorArgument(L, 2);
    if (!setParent(world, e, character)) {
        return luaL_error(L, "Invalid character parent");
    }
    auto pose = localTransform(world, e);
    pose.position = offset;
    pose.rotation = {};
    setLocalTransform(world, e, pose);
    fps.yaw = fps.pitch = 0;
    fps.initialized = true;
    if (auto* camera = world.tryGet<Camera>(e)) {
        camera->tracking = false;
        camera->following = false;
    }
    return 0;
}

void LuaScene::setExports(const std::vector<ScriptField>& fields) {
    for (const auto& field : fields) {
        if (!exportName(field.name)) {
            continue;
        }
        if (field.type == "number") {
            lua_pushnumber(state_, field.number);
        } else if (field.type == "boolean") {
            lua_pushboolean(state_, field.boolean);
        } else if (field.type == "Texture" &&
                   !(materialTexturePath(field.text) || skyboxHDRPath(field.text))) {
            lua_pushnil(state_);
        } else if (field.type == "string" || exportAssetType(field.type)) {
            lua_pushlstring(state_, field.text.data(), field.text.size());
        } else if (exportHasComponent(world_, field.entity, field.type)) {
            if (field.type == "GameObject") {
                pushObject(field.entity);
            } else {
                int type = 0;
                for (; type < 30; ++type) {
                    if (field.type == types[type]) {
                        break;
                    }
                }
                component(state_, Handle{this, &world_, field.entity}, type);
            }
        } else {
            lua_pushnil(state_);
        }
        lua_setglobal(state_, field.name.c_str());
    }
}

int LuaScene::skyboxFadeAPI(lua_State* L) {
    auto& self = *static_cast<LuaScene*>(lua_touserdata(L, lua_upvalueindex(1)));
    auto skyObject = [&](int index) -> Entity {
        if (auto* component = static_cast<Component*>(luaL_testudata(L, index, "Component"))) {
            if (component->type != 12 || component->object.world != &self.world_) {
                luaL_error(L, "Expected a Skybox from this world");
            }
            return component->object.entity;
        }
        auto& object = handle(L, index);
        if (object.world != &self.world_) {
            luaL_error(L, "Skybox belongs to another world");
        }
        return object.entity;
    };
    Entity source, target;
    float speed;
    if (lua_gettop(L) == 3) {
        source = skyObject(1);
        target = skyObject(2);
        speed = number(L, 3);
    } else {
        luaL_argcheck(
            L, lua_gettop(L) == 2, 1, "Expected destination, speed OR source, destination, speed");
        target = skyObject(1);
        speed = number(L, 2);
        self.world_.eachReadOnly<Skybox>([&](Entity e, const Skybox& sky) {
            if (source == Entity{} && sky.enabled) {
                source = e;
            }
        });
    }
    if (const char* error = beginSkyboxFade(self.world_, source, target, speed)) {
        return luaL_error(L, "%s", error);
    }
    return 0;
}

int LuaScene::destroyAPI(lua_State* L) {
    auto& self = *static_cast<LuaScene*>(lua_touserdata(L, lua_upvalueindex(1)));
    auto object = handle(L, 1);
    if (object.world != &self.world_) {
        return luaL_error(L, "Object belongs to another world");
    }
    float delay = lua_isnoneornil(L, 2) ? 0 : number(L, 2);
    if (delay < 0) {
        return luaL_error(L, "Destroy delay must be nonnegative");
    }
    if (delay == 0) {
        self.destroy(object.entity);
    } else {
        self.world_.add(object.entity, DestroyAfter{delay});
    }
    return 0;
}
int LuaScene::spawnAPI(lua_State* L) {
    auto& self = *static_cast<LuaScene*>(lua_touserdata(L, lua_upvalueindex(1)));
    if (self.stopping_) {
        return luaL_error(L, "Cannot spawn during shutdown");
    }
    Vec3 position = vectorArgument(L, 2);
    Quat rotation = rotationArgument(L, 3);
    Entity result;
    self.apiError_.clear();
    try {
        if (lua_type(L, 1) == LUA_TSTRING) {
            const ProjectIO::Meshes meshes{MeshHandle{},
                                           self.meshes_[4],
                                           self.meshes_[0],
                                           self.meshes_[1],
                                           self.meshes_[2],
                                           self.meshes_[3]};
            result = Prefabs::spawn(
                self.assets_, lua_tostring(L, 1), self.world_, meshes, position, rotation);
        } else {
            auto object = handle(L, 1);
            if (object.world != &self.world_) {
                return luaL_error(L, "Object belongs to another world");
            }
            if (!self.world_.has<Transform>(object.entity)) {
                return luaL_error(L, "Spawn source needs a Transform");
            }
            result = Prefabs::clone(self.world_, object.entity, self.world_);
            auto& t = self.world_.get<Transform>(result);
            t.position = position;
            t.rotation = rotation;
            updateHierarchy(self.world_);
        }
    } catch (const std::exception& e) {
        self.apiError_ = e.what();
    }
    if (!self.apiError_.empty()) {
        lua_pushlstring(L, self.apiError_.data(), self.apiError_.size());
        self.apiError_.clear();
        return lua_error(L);
    }
    self.pushObject(result, true);
    return 1;
}

int LuaScene::animationMethod(lua_State* L) {
    const auto c = *static_cast<Component*>(luaL_checkudata(L, lua_upvalueindex(1), "Component"));
    if (!c.object.world->alive(c.object.entity) || !has(c.object, 16)) {
        return luaL_error(L, "Animation no longer exists");
    }
    auto& animation = c.object.world->get<Animation>(c.object.entity);
    const int method = int(lua_tointeger(L, lua_upvalueindex(2)));
    if (method == 5) {
        int arg = luaL_testudata(L, 1, "Component") ? 2 : 1;
        const char* clip = luaL_checkstring(L, arg);
        float duration = float(luaL_optnumber(L, arg + 1, .25));
        if (!std::isfinite(duration) || duration < 0 || duration > 3600) {
            return luaL_error(L, "Cross-fade duration must be 0..3600 seconds");
        }
        if (!animation.request(AnimationAction::CrossFade, clip, duration)) {
            return luaL_error(L, "Animation command queue is full");
        }
        return 0;
    }
    const AnimationAction actions[]{AnimationAction::Play,
                                    AnimationAction::Stop,
                                    AnimationAction::Pause,
                                    AnimationAction::Resume,
                                    AnimationAction::Reset};
    if (!animation.request(actions[method])) {
        return luaL_error(L, "Animation command queue is full");
    }
    return 0;
}

int LuaScene::particleMethod(lua_State* L) {
    const auto c = *static_cast<Component*>(luaL_checkudata(L, lua_upvalueindex(1), "Component"));
    if (!c.object.world->alive(c.object.entity) || !has(c.object, 17)) {
        return luaL_error(L, "ParticleSystem no longer exists");
    }
    auto& p = c.object.world->get<ParticleSystem>(c.object.entity);
    const int method = int(lua_tointeger(L, lua_upvalueindex(2)));
    const int arg = luaL_testudata(L, 1, "Component") ? 2 : 1;
    switch (method) {
    case 0:
        p.play();
        break;
    case 1:
        p.pause();
        break;
    case 2:
        p.resume();
        break;
    case 3:
        if (!lua_isnone(L, arg)) {
            luaL_checktype(L, arg, LUA_TBOOLEAN);
        }
        p.stop(lua_isnone(L, arg) || lua_toboolean(L, arg));
        break;
    case 4:
        p.restart();
        break;
    case 5:
        p.clear();
        break;
    case 6: {
        const auto count = luaL_checkinteger(L, arg);
        if (count < 0 || count > 10000) {
            return luaL_error(L, "emit count must be 0..10000");
        }
        p.emit(int(count));
        break;
    }
    case 7: {
        const char* preset = luaL_checkstring(L, arg);
        int index = -1;
        for (int i = 0; i < particlePresetCount; ++i) {
            if (!std::strcmp(preset, particlePresetNames[i])) {
                index = i;
                break;
            }
        }
        if (index < 0) {
            return luaL_error(L, "Expected Fire, Smoke, Sparkles, Blood or Explosion");
        }
        applyParticlePreset(p, index);
        break;
    }
    }
    return 0;
}

int LuaScene::fadeColorAPI(lua_State* L) {
    auto& self = *static_cast<LuaScene*>(lua_touserdata(L, lua_upvalueindex(1)));
    Vec3 color = vectorArgument(L, 1);
    if (!std::isfinite(color.x) || !std::isfinite(color.y) || !std::isfinite(color.z) ||
        color.x < 0 || color.x > 1 || color.y < 0 || color.y > 1 || color.z < 0 || color.z > 1) {
        return luaL_error(L, "Fade color must be an RGB vector in 0..1");
    }
    Entity e = activeCamera(self.world_);
    if (e == Entity{}) {
        return luaL_error(L, "fadeToColor requires an active camera");
    }
    auto& fx = self.world_.has<CameraEffects>(e) ? self.world_.get<CameraEffects>(e)
                                                 : self.world_.add(e, CameraEffects{});
    fx.fadeR = color.x;
    fx.fadeG = color.y;
    fx.fadeB = color.z;
    lua_createtable(L, 0, 2);
    for (int i = 0; i < 2; ++i) {
        self.pushObject(e);
        lua_pushinteger(L, i == 0 ? 1 : 0);
        lua_pushcclosure(L, fadeColorControl, 2);
        lua_setfield(L, -2, i == 0 ? "start" : "stop");
    }
    return 1;
}
int LuaScene::fadeColorControl(lua_State* L) {
    const auto h = handle(L, lua_upvalueindex(1));
    if (!h.world->has<CameraEffects>(h.entity)) {
        return luaL_error(L, "Fade camera effects no longer exist");
    }
    int argument = lua_istable(L, 1) ? 2 : 1;
    double duration = luaL_optnumber(L, argument, 1);
    if (!std::isfinite(duration) || duration < 0 || duration > 3600) {
        return luaL_error(L, "Fade duration must be 0..3600 seconds");
    }
    auto& fx = h.world->get<CameraEffects>(h.entity);
    fx.fadeTarget = float(lua_tointeger(L, lua_upvalueindex(2)));
    fx.fadeSpeed = duration > 0 ? float(std::abs(fx.fadeTarget - fx.fadeAmount) / duration) : 0;
    if (duration == 0) {
        fx.fadeAmount = fx.fadeTarget;
    }
    return 0;
}
