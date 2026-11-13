#pragma once
#include "assets/assets.hpp"
#include "core/components.hpp"
#include "core/ecs.hpp"
#include <functional>
#include <list>
#include <string>
#include <string_view>
#include <vector>
struct lua_State;

// Lua handles contain entity generations, never pointers into component pools.
class LuaScene {
  public:
    LuaScene(lua_State* state,
             World& world,
             MeshHandle cube,
             MeshHandle sphere,
             MeshHandle capsule,
             MeshHandle cone,
             std::function<void(std::string_view)> error,
             MeshHandle plane = {});
    void setSelf(Entity entity);
    void setAssetRoot(const std::filesystem::path& root) {
        assets_.root = root;
    }
    void setExports(const std::vector<ScriptField>& fields);
    void start();
    void objectEvent(Entity entity, const char* name);
    bool receivesEvents() const {
        return active_ && (selfEntity_ == Entity{} || isActive(world_, selfEntity_));
    }
    void update(float dt);
    void fixedUpdate(float dt);
    void stop();

  private:
    struct Entry {
        Entity entity;
        int reference;
        bool owned = false, started = false, destroying = false;
    };
    struct Task {
        lua_State* thread = nullptr;
        int reference = 0, objectReference = 0;
        std::string callback;
        float seconds = 0;
        Entity audioEntity;
        int audioEvent = -1;
        unsigned sequence = 0;
        Entity owner;
    };
    std::list<Task> tasks_;
    Task* currentTask_ = nullptr;
    Entity selfEntity_;
    void resume(Task& task, int arguments);
    void advanceTasks(float dt);
    static int waitAPI(lua_State* state);
    lua_State* state_;
    World& world_;
    AssetManager assets_;
    std::string apiError_;
    MeshHandle meshes_[5];
    std::function<void(std::string_view)> error_;
    std::vector<Entry> entries_;
    std::vector<Entity> query_;
    bool active_ = false, stopping_ = false, started_ = false;
    void pushObject(Entity e, bool owned = false);
    void call(const char* name, int reference, float dt, bool delta);
    void destroy(Entity e);
    void tick(const char* name, float dt);
    static int api(lua_State* state);
    static int spawnAPI(lua_State* state);
    static int destroyAPI(lua_State* state);
    static int audioApi(lua_State* state);
    static int skyboxFadeAPI(lua_State* state);
    static int skyboxMethod(lua_State* state);
    static int materialMethod(lua_State* state);
    static int lightMethod(lua_State* state);
    static int listenerMethod(lua_State* state);
    static int audioMethod(lua_State* state);
    static int cameraApi(lua_State* state);
    static int objectApi(lua_State* state);
    static int gameplayMethod(lua_State* state);
    static int fpsCameraMethod(lua_State* state);
    static int componentTypeIndex(lua_State* state);
    static int objectMethod(lua_State* state);
    static int objectIndex(lua_State* state);
    static int objectNewIndex(lua_State* state);
    static int componentIndex(lua_State* state);
    static int componentNewIndex(lua_State* state);
    static int vectorIndex(lua_State* state);
    static int vectorNewIndex(lua_State* state);
};
