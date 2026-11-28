#include "scripting/lua_console.hpp"
#include "scripting/game_services.hpp"
#include "scripting/legacy_script.hpp"
#include "scripting/lua_scene.hpp"

#include "scripting/lua_compat.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdlib>

void* LuaConsole::allocate(void* user, void* pointer, std::size_t oldSize, std::size_t newSize) {
    auto& self = *static_cast<LuaConsole*>(user);
    if (!pointer) {
        oldSize = 0; // Lua uses oldSize as a type tag for new allocations.
    }
    if (newSize == 0) {
        std::free(pointer);
        self.allocated_ -= oldSize;
        return nullptr;
    }
    if (newSize > memoryLimit || self.allocated_ - oldSize > memoryLimit - newSize) {
        return nullptr;
    }
    void* result = std::realloc(pointer, newSize);
    if (result) {
        self.allocated_ = self.allocated_ - oldSize + newSize;
    }
    return result;
}

LuaConsole::LuaConsole() {
    reset();
}
LuaConsole::~LuaConsole() {
    shutdown();
    if (state_) {
        lua_close(state_);
    }
}

void LuaConsole::append(Kind kind, std::string_view text) {
    if (lines_.size() >= maxLines) {
        lines_.pop_front();
    }
    // Bound the host transcript as well as Lua's own memory.
    std::string message = kind == Kind::Output ? "lua > " : "";
    message.append(text.substr(0, maxLineBytes));
    if (text.size() > maxLineBytes) {
        message += " [truncated]";
    }
    if (kind == Kind::Error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "LegacyScript: %s", message.c_str());
    } else if (kind == Kind::Warning) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "LegacyScript: %s", message.c_str());
    } else {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "LegacyScript: %s", message.c_str());
    }
    lines_.push_back({kind, std::move(message)});
}

void LuaConsole::reset() {
    shutdown();
    if (state_) {
        lua_close(state_);
    }
    state_ = nullptr;
    allocated_ = 0;
    state_ = lua_newstate(allocate, this);
    if (!state_) {
        append(Kind::Error, "Unable to create Lua runtime");
        return;
    }
    legacyLuaSetOwner(state_, this);
    // Snippets have basic computation libraries, but no host filesystem, process,
    // native module loading or debug access. No coroutines or catchable pcall:
    // scripts must not swallow the instruction-limit error and run forever.
    luaL_requiref(state_, "_G", luaopen_base, 1);
    lua_pop(state_, 1);
    luaL_requiref(state_, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(state_, 1);
    luaL_requiref(state_, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(state_, 1);
    luaL_requiref(state_, LUA_MATHLIBNAME, luaopen_math, 1);
    lua_pop(state_, 1);
    luaL_requiref(state_, LUA_UTF8LIBNAME, luaopen_utf8, 1);
    lua_pop(state_, 1);
    luaL_requiref(state_, LUA_BITLIBNAME, luaopen_bit, 1);
    lua_pop(state_, 1);
    for (const char* name : {"loadstring",
                             "getfenv",
                             "setfenv",
                             "newproxy",
                             "gcinfo",
                             "coroutine",
                             "dofile",
                             "loadfile",
                             "load",
                             "collectgarbage",
                             "pcall",
                             "xpcall",
                             "warn",
                             "setmetatable",
                             "getmetatable"}) {
        lua_pushnil(state_);
        lua_setglobal(state_, name);
    }
    lua_pushlightuserdata(state_, this);
    lua_pushcclosure(state_, print, 1);
    lua_setglobal(state_, "print");
    installScene();
    installInput();
    installLevels();
    installEvents();
    GameServices::install(state_);
    lua_newtable(state_);
    const char* names[]{"log", "error", "warning"};
    const Kind kinds[]{Kind::Output, Kind::Error, Kind::Warning};
    for (int i = 0; i < 3; ++i) {
        lua_pushlightuserdata(state_, this);
        lua_pushinteger(state_, static_cast<int>(kinds[i]));
        lua_pushcclosure(state_, log, 2);
        lua_setfield(state_, -2, names[i]);
    }
    lua_setglobal(state_, "debug");
}

int LuaConsole::print(lua_State* state) {
    auto* self = static_cast<LuaConsole*>(lua_touserdata(state, lua_upvalueindex(1)));
    // Do all Lua calls before constructing C++ objects: a Lua error longjmps.
    const int count = lua_gettop(state);
    luaL_checkstack(state, count + 8, "Too many print arguments");
    for (int i = 1; i <= count; ++i) {
        luaL_tolstring(state, i, nullptr);
    }
    std::string output;
    for (int i = 0; i < count && output.size() < maxLineBytes; ++i) {
        if (i) {
            output += '\t';
        }
        std::size_t length = 0;
        const char* text = lua_tolstring(state, count + 1 + i, &length);
        const std::size_t take = std::min(length, maxLineBytes - output.size());
        for (std::size_t j = 0; j < take; ++j) {
            output += text[j] == '\0' ? '?' : text[j];
        }
        if (length > take) {
            output += " [truncated]";
            break;
        }
    }
    self->append(Kind::Output, output);
    return 0;
}

int LuaConsole::log(lua_State* state) {
    auto* self = static_cast<LuaConsole*>(lua_touserdata(state, lua_upvalueindex(1)));
    const auto kind = static_cast<Kind>(lua_tointeger(state, lua_upvalueindex(2)));
    const int count = lua_gettop(state);
    luaL_checkstack(state, count + 8, "Too many log arguments");
    for (int i = 1; i <= count; ++i) {
        luaL_tolstring(state, i, nullptr);
    }
    std::string message;
    for (int i = 0; i < count && message.size() < maxLineBytes; ++i) {
        if (i) {
            message += '\t';
        }
        std::size_t size = 0;
        const char* value = lua_tolstring(state, count + i + 1, &size);
        for (std::size_t j = 0; j < size && message.size() < maxLineBytes; ++j) {
            message += value[j] == '\0' ? '?' : value[j];
        }
    }
    self->append(kind, message);
    return 0;
}

int LuaConsole::traceback(lua_State* state) {
    const char* message =
        lua_type(state, 1) == LUA_TSTRING ? lua_tostring(state, 1) : "(non-string Lua error)";
    luaL_traceback(state, state, message, 1);
    return 1;
}

void LuaConsole::instructionHook(lua_State* state, lua_Debug*) {
    auto* self = static_cast<LuaConsole*>(legacyLuaOwner(state));
    self->remainingInstructions_ -= 1000;
    if (self->remainingInstructions_ <= 0) {
        luaL_error(state, "Execution stopped: instruction limit exceeded");
    }
}

bool LuaConsole::execute(std::string_view source, std::string_view sourceName, bool startScene) {
    if (!state_) {
        append(Kind::Error, "Lua runtime is unavailable");
        return false;
    }
    if (source.size() > maxSourceBytes) {
        append(Kind::Error, "Script exceeds the 64 KiB editor limit");
        return false;
    }
    const auto errorsBefore = errorCount_;
    const auto prepared = preprocessLegacyScript(source);
    lua_settop(state_, 0);
    beginBudget();
    lua_pushcfunction(state_, traceback);
    const std::string chunk = "@" + std::string(sourceName.substr(0, 512));
    int status = luaL_loadbufferx(state_, prepared.data(), prepared.size(), chunk.c_str(), "t");
    if (status == LUA_OK) {
        status = lua_pcall(state_, 0, 0, 1);
    }
    if (status == LUA_OK && scene_ && startScene) {
        scene_->start();
    }
    endBudget();
    if (status != LUA_OK) {
        const char* message = lua_tostring(state_, -1);
        append(Kind::Error, message ? message : "Unknown Lua error");
    }
    lua_settop(state_, 0);
    lua_gc(state_, LUA_GCCOLLECT, 0);
    return status == LUA_OK && errorCount_ == errorsBefore;
}

void LuaConsole::startRuntime() {
    if (scene_) {
        beginBudget();
        scene_->start();
        endBudget();
    }
}
void LuaConsole::beginBudget() {
    if (budgetDepth_++ > 0) {
        return;
    }
    remainingInstructions_ = 1000000;
    // LuaJIT traces must never bypass the sandbox's instruction-count hook.
    // CHECKHOOK also guards any previously recorded traces in this VM.
    luaJIT_setmode(state_, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_OFF);
    lua_sethook(state_, instructionHook, LUA_MASKCOUNT, 1000);
}
void LuaConsole::endBudget() {
    if (budgetDepth_ <= 0 || --budgetDepth_ > 0) {
        return;
    }
    lua_sethook(state_, nullptr, 0, 0);
    luaJIT_setmode(state_, 0, LUAJIT_MODE_ENGINE | LUAJIT_MODE_ON);
}
void LuaConsole::installScene() {
    if (world_ && state_) {
        scene_ = std::make_unique<LuaScene>(
            state_,
            *world_,
            meshes_[0],
            meshes_[1],
            meshes_[2],
            meshes_[3],
            [this](std::string_view message) {
                ++errorCount_;
                append(Kind::Error, message);
            },
            meshes_[4]);
        scene_->setAssetRoot(assetRoot_);
    }
}
void LuaConsole::setAssetRoot(const std::filesystem::path& root) {
    assetRoot_ = root;
    if (scene_) {
        scene_->setAssetRoot(root);
    }
}

void LuaConsole::bindWorld(World& world,
                           MeshHandle cube,
                           MeshHandle sphere,
                           MeshHandle capsule,
                           MeshHandle cone,
                           MeshHandle plane) {
    shutdown();
    world_ = &world;
    meshes_[0] = cube;
    meshes_[1] = sphere;
    meshes_[2] = capsule;
    meshes_[3] = cone;
    meshes_[4] = plane;
    installScene();
    installEvents();
}
void LuaConsole::update(float dt, double waitDelta) {
    if (scene_) {
        beginBudget();
        scene_->update(dt, waitDelta);
        endBudget();
    }
}
void LuaConsole::fixedUpdate(float dt) {
    if (scene_) {
        beginBudget();
        scene_->fixedUpdate(dt);
        endBudget();
    }
}
void LuaConsole::shutdown() {
    namedScripts_.erase(std::remove(namedScripts_.begin(), namedScripts_.end(), this),
                        namedScripts_.end());
    scriptName_.clear();
    scriptPath_.clear();
    removeEventListeners();
    if (scene_) {
        beginBudget();
        scene_->stop();
        endBudget();
        scene_.reset();
    }
    removeEventListeners();
}

bool LuaConsole::bindObject(Entity entity) {
    if (!state_ || !scene_ || !world_->alive(entity)) {
        return false;
    }
    struct Bind {
        LuaScene* scene;
        Entity entity;
    } bind{scene_.get(), entity};
    lua_pushcfunction(
        state_, +[](lua_State* L) -> int {
            auto* bind = static_cast<Bind*>(lua_touserdata(L, 1));
            bind->scene->setSelf(bind->entity);
            return 0;
        });
    lua_pushlightuserdata(state_, &bind);
    if (lua_pcall(state_, 1, 0, 0) != LUA_OK) {
        append(Kind::Error, lua_tostring(state_, -1));
        lua_pop(state_, 1);
        return false;
    }
    return true;
}

bool LuaConsole::bindExports(const std::vector<ScriptField>& fields) {
    if (!state_ || !scene_) {
        return false;
    }
    struct Binding {
        LuaScene* scene;
        const std::vector<ScriptField>* fields;
    } binding{scene_.get(), &fields};
    lua_pushcfunction(
        state_, +[](lua_State* L) -> int {
            auto* b = static_cast<Binding*>(lua_touserdata(L, 1));
            b->scene->setExports(*b->fields);
            return 0;
        });
    lua_pushlightuserdata(state_, &binding);
    if (lua_pcall(state_, 1, 0, 0) != LUA_OK) {
        append(Kind::Error, lua_tostring(state_, -1));
        lua_pop(state_, 1);
        return false;
    }
    return true;
}

void LuaConsole::objectEvent(Entity entity, const char* name, Entity other, bool colliderArgument) {
    if (!scene_) {
        return;
    }
    beginBudget();
    scene_->objectEvent(entity, name, other, colliderArgument);
    endBudget();
}

// Named script components retain isolated Lua states. Only bounded scalar values
// cross the boundary; engine handles, closures, threads and tables never escape.
#include "assets/assets.hpp"
#include <cmath>
#include <cstring>
std::vector<LuaConsole*> LuaConsole::namedScripts_;
namespace {
struct ScriptScalar {
    int type = LUA_TNIL;
    lua_Number number = 0;
    lua_Integer integer = 0;
    bool integral = false;
    bool boolean = false;
    std::size_t size = 0;
    char text[4097]{};
};
bool captureScalar(lua_State* L, int index, ScriptScalar& value) {
    value.type = lua_type(L, index);
    switch (value.type) {
    case LUA_TNIL:
        return true;
    case LUA_TBOOLEAN:
        value.boolean = lua_toboolean(L, index);
        return true;
    case LUA_TNUMBER:
        value.integral = lua_isinteger(L, index);
        value.integer = value.integral ? lua_tointeger(L, index) : 0;
        value.number = lua_tonumber(L, index);
        return std::isfinite(value.number);
    case LUA_TSTRING: {
        const char* text = lua_tolstring(L, index, &value.size);
        if (value.size > 4096) {
            return false;
        }
        std::memcpy(value.text, text, value.size);
        return true;
    }
    default:
        return false;
    }
}
void pushScalar(lua_State* L, const ScriptScalar& value) {
    switch (value.type) {
    case LUA_TBOOLEAN:
        lua_pushboolean(L, value.boolean);
        break;
    case LUA_TNUMBER:
        if (value.integral) {
            lua_pushinteger(L, value.integer);
        } else {
            lua_pushnumber(L, value.number);
        }
        break;
    case LUA_TSTRING:
        lua_pushlstring(L, value.text, value.size);
        break;
    default:
        lua_pushnil(L);
        break;
    }
}
thread_local int namedCallDepth = 0;
bool publicScriptMember(const char* name) {
    if (!name || !*name || std::strlen(name) > 128 || name[0] == '_') {
        return false;
    }
    // Lifecycle entry points cannot be spoofed through a function proxy.
    const char* reserved[]{
        "onStart", "onUpdate",       "onFixedUpdate", "onDestroy",    "self",     "wait",
        "task",    "print",          "error",         "assert",       "pcall",    "xpcall",
        "load",    "collectgarbage", "setmetatable",  "getmetatable", "rawset",   "rawget",
        "next",    "pairs",          "ipairs",        "type",         "tostring", "tonumber",
        "select",  "require"};
    for (const auto* key : reserved) {
        if (std::strcmp(key, name) == 0) {
            return false;
        }
    }
    return true;
}
void rawScriptGlobal(lua_State* L, const char* name) {
    lua_pushglobaltable(L);
    lua_pushstring(L, name);
    lua_rawget(L, -2);
    lua_remove(L, -2);
}
} // namespace
void LuaConsole::nameScript(Entity entity, const std::string& path) {
    if (!world_ || !world_->alive(entity)) {
        throw std::runtime_error("Invalid script owner");
    }
    scriptName_ = AssetManager::scriptName(path);
    scriptPath_ = path;
    scriptEntity_ = entity;
    for (auto* script : namedScripts_) {
        if (script != this && script->world_ == world_ && script->scriptEntity_ == entity &&
            script->scriptName_ == scriptName_) {
            throw std::runtime_error("Ambiguous script component: " + scriptName_);
        }
    }
    namedScripts_.push_back(this);
}
LuaConsole* LuaConsole::resolveNamed(const NamedProxy& proxy) {
    if (!proxy.world || !proxy.world->alive(proxy.entity) ||
        !isActive(*proxy.world, proxy.entity)) {
        return nullptr;
    }
    const auto* attached = proxy.world->tryGet<LuaScript>(proxy.entity);
    if (!attached) {
        return nullptr;
    }
    LuaConsole* found = nullptr;
    for (auto* script : namedScripts_) {
        if (script->world_ != proxy.world || script->scriptEntity_ != proxy.entity ||
            script->scriptName_ != proxy.name) {
            continue;
        }
        bool enabled = false;
        for (const auto& item : scriptAttachments(*attached)) {
            enabled |= item.enabled && item.path == script->scriptPath_;
        }
        if (!enabled) {
            continue;
        }
        if (found) {
            return nullptr;
        }
        found = script;
    }
    return found;
}
int LuaConsole::pushNamedScript(lua_State* L, World& world, Entity entity, const char* name) {
    NamedProxy proxy{};
    proxy.world = &world;
    proxy.entity = entity;
    const auto size = std::strlen(name);
    if (!size || size >= sizeof(proxy.name) || std::strpbrk(name, "/\\#")) {
        return luaL_error(L, "Expected a script component name, not a path");
    }
    for (std::size_t i = 0; i < size; ++i) {
        proxy.name[i] = char(std::tolower(static_cast<unsigned char>(name[i])));
    }
    if (size > 4 && std::strcmp(proxy.name + size - 4, ".lua") == 0) {
        proxy.name[size - 4] = 0;
    } else if (size > 3 && std::strcmp(proxy.name + size - 3, ".ls") == 0) {
        proxy.name[size - 3] = 0;
    }
    if (!resolveNamed(proxy)) {
        lua_pushnil(L);
        return 1;
    }
    if (luaL_newmetatable(L, "NamedScriptComponent")) {
        lua_pushcfunction(L, namedIndex);
        lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, namedWrite);
        lua_setfield(L, -2, "__newindex");
        lua_pushliteral(L, "ScriptComponent");
        lua_setfield(L, -2, "__metatable");
    }
    lua_pop(L, 1);
    auto* result = static_cast<NamedProxy*>(lua_newuserdatauv(L, sizeof(NamedProxy), 0));
    *result = proxy;
    luaL_setmetatable(L, "NamedScriptComponent");
    return 1;
}
int LuaConsole::namedIndex(lua_State* L) {
    auto* proxy = static_cast<NamedProxy*>(luaL_checkudata(L, 1, "NamedScriptComponent"));
    return namedTransfer(L, 0, *proxy, luaL_checkstring(L, 2), 0);
}
int LuaConsole::namedWrite(lua_State* L) {
    auto* proxy = static_cast<NamedProxy*>(luaL_checkudata(L, 1, "NamedScriptComponent"));
    return namedTransfer(L, 1, *proxy, luaL_checkstring(L, 2), 3);
}
int LuaConsole::namedInvoke(lua_State* L) {
    auto* proxy =
        static_cast<NamedProxy*>(luaL_checkudata(L, lua_upvalueindex(1), "NamedScriptComponent"));
    const int first = lua_gettop(L) && lua_rawequal(L, 1, lua_upvalueindex(1)) ? 2 : 1;
    return namedTransfer(L, 2, *proxy, lua_tostring(L, lua_upvalueindex(2)), first);
}
int LuaConsole::namedTransfer(
    lua_State* L, int operation, const NamedProxy& proxy, const char* member, int first) {
    if (!publicScriptMember(member)) {
        return luaL_error(L, "Reserved script member");
    }
    if (operation == 2 && ++namedCalls_ > 256) {
        return luaL_error(L, "Script call frame budget exceeded");
    }
    if (namedCallDepth >= 4) {
        return luaL_error(L, "Script call depth limit exceeded");
    }
    auto* target = resolveNamed(proxy);
    if (!target || !target->state_) {
        return luaL_error(L, "Script component no longer exists or is disabled");
    }
    const int arguments = operation == 1 ? 1 : operation == 2 ? lua_gettop(L) - first + 1 : 0;
    if (arguments > 8) {
        return luaL_error(L, "Script calls support at most eight arguments");
    }
    struct Request {
        const char* member;
        int operation, count;
        ScriptScalar arguments[8], results[8];
        int resultCount = 0;
        bool function = false;
    } request{};
    request.member = member;
    request.operation = operation;
    request.count = arguments;
    for (int i = 0; i < arguments; ++i) {
        if (!captureScalar(L, first + i, request.arguments[i])) {
            return luaL_error(
                L,
                "Script access supports nil, boolean, finite number and strings up to 4096 bytes");
        }
    }
    // Self-state access can use the same Lua stack directly; nested external
    // calls into an executing state are rejected to prevent cyclic bridges.
    const bool sameState = static_cast<LuaConsole*>(legacyLuaOwner(L)) == target;
    if (sameState && operation == 2) {
        rawScriptGlobal(L, member);
        if (!lua_isfunction(L, -1)) {
            return luaL_error(L, "Script function not found");
        }
        for (int i = 0; i < arguments; ++i) {
            pushScalar(L, request.arguments[i]);
        }
        ++namedCallDepth;
        const int status = lua_pcall(L, arguments, LUA_MULTRET, 0);
        --namedCallDepth;
        if (status != LUA_OK) {
            return lua_error(L);
        }
        // Apply the same scalar/result limits even when calling our own state.
        const int results = lua_gettop(L) - (first + arguments - 1);
        if (results > 8) {
            return luaL_error(L, "Script calls support at most eight results");
        }
        for (int i = 0; i < results; ++i) {
            if (!captureScalar(L, first + arguments + i, request.results[i])) {
                return luaL_error(L, "Unsupported script result/field type");
            }
        }
        return results;
    }
    auto* T = sameState ? L : target->state_;
    if (!sameState && target->budgetDepth_ > 0) {
        return luaL_error(L, "Cyclic/reentrant script call rejected");
    }
    const int saved = lua_gettop(T);
    // All operations that allocate in the target state run under its pcall.
    lua_pushcfunction(
        T, +[](lua_State* T) -> int {
            auto& r = *static_cast<Request*>(lua_touserdata(T, 1));
            if (r.operation == 1) {
                rawScriptGlobal(T, r.member);
                if (!lua_isnil(T, -1) && !lua_isboolean(T, -1) && !lua_isnumber(T, -1) &&
                    !lua_isstring(T, -1)) {
                    return luaL_error(T, "Only scalar script fields can be changed");
                }
                lua_pop(T, 1);
                lua_pushglobaltable(T);
                lua_pushstring(T, r.member);
                pushScalar(T, r.arguments[0]);
                lua_rawset(T, -3);
                return 0;
            }
            rawScriptGlobal(T, r.member);
            if (r.operation == 0 && lua_isfunction(T, -1)) {
                r.function = true;
                return 0;
            }
            if (r.operation == 2) {
                if (!lua_isfunction(T, -1)) {
                    return luaL_error(T, "Script function not found");
                }
                for (int i = 0; i < r.count; ++i) {
                    pushScalar(T, r.arguments[i]);
                }
                lua_call(T, r.count, LUA_MULTRET);
            }
            r.resultCount = lua_gettop(T) - 1;
            if (r.resultCount > 8) {
                return luaL_error(T, "Script calls support at most eight results");
            }
            for (int i = 0; i < r.resultCount; ++i) {
                if (!captureScalar(T, i + 2, r.results[i])) {
                    return luaL_error(T, "Unsupported script result/field type");
                }
            }
            return 0;
        });
    lua_pushlightuserdata(T, &request);
    target->beginBudget();
    ++namedCallDepth;
    const int status = lua_pcall(T, 1, 0, 0);
    --namedCallDepth;
    target->endBudget();
    char error[1024]{};
    if (status != LUA_OK) {
        std::snprintf(error,
                      sizeof(error),
                      "%s",
                      lua_tostring(T, -1) ? lua_tostring(T, -1) : "Script access failed");
    }
    lua_settop(T, saved);
    if (status != LUA_OK) {
        return luaL_error(L, "%s", error);
    }
    if (request.function) {
        lua_pushvalue(L, 1);
        lua_pushstring(L, member);
        lua_pushcclosure(L, namedInvoke, 2);
        return 1;
    }
    for (int i = 0; i < request.resultCount; ++i) {
        pushScalar(L, request.results[i]);
    }
    return request.resultCount;
}
