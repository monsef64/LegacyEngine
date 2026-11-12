#pragma once
#include "components.hpp"
#include "ecs.hpp"
#include <functional>
#include <string_view>
#include <vector>
struct lua_State;

// Lua handles contain entity generations, never pointers into component pools.
class LuaScene {
public:
    LuaScene(lua_State* state, World& world, MeshHandle cube, MeshHandle sphere, MeshHandle capsule, MeshHandle cone,
             std::function<void(std::string_view)> error);
    void setSelf(Entity entity);
    void start();
    void update(float dt);
    void fixedUpdate(float dt);
    void stop();
private:
    struct Entry { Entity entity; int reference; bool owned=false, started=false, destroying=false; };
    lua_State* state_;
    World& world_;
    MeshHandle meshes_[4];
    std::function<void(std::string_view)> error_;
    std::vector<Entry> entries_;
    std::vector<Entity> query_;
    bool active_=false, stopping_=false, started_=false;
    void pushObject(Entity e, bool owned=false);
    void call(const char* name, int reference, float dt, bool delta);
    void destroy(Entity e);
    void tick(const char* name,float dt);
    static int api(lua_State* state);
    static int cameraApi(lua_State* state);
    static int objectMethod(lua_State* state);
    static int objectIndex(lua_State* state);
    static int objectNewIndex(lua_State* state);
    static int componentIndex(lua_State* state);
    static int componentNewIndex(lua_State* state);
    static int vectorIndex(lua_State* state);
    static int vectorNewIndex(lua_State* state);
};
