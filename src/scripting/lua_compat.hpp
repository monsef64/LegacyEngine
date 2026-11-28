#pragma once
// Host-side compatibility only: scripts execute with LuaJIT's Lua 5.1 language.
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <luajit.h>
#include <lualib.h>
}
#include <cmath>
#include <limits>

static_assert(LUA_VERSION_NUM == 501, "LegacyEngine embeds LuaJIT, not PUC Lua");
inline int lua_absindex(lua_State* L, int index) {
    return index > 0 || index <= LUA_REGISTRYINDEX ? index : lua_gettop(L) + index + 1;
}
inline bool legacyLuaIntegerInRange(lua_Number n) {
    return std::isfinite(n) && std::floor(n) == n &&
           n >= static_cast<lua_Number>(std::numeric_limits<lua_Integer>::min()) &&
           n < -static_cast<lua_Number>(std::numeric_limits<lua_Integer>::min());
}
inline int lua_isinteger(lua_State* L, int index) {
    return lua_type(L, index) == LUA_TNUMBER && legacyLuaIntegerInRange(lua_tonumber(L, index));
}
inline lua_Integer legacyLuaCheckInteger(lua_State* L, int index) {
    const auto n = luaL_checknumber(L, index);
    if (!legacyLuaIntegerInRange(n)) {
        luaL_argerror(L, index, "integer required (no fractional value or overflow)");
    }
    return static_cast<lua_Integer>(n);
}
inline lua_Integer legacyLuaOptInteger(lua_State* L, int index, lua_Integer fallback) {
    return lua_isnoneornil(L, index) ? fallback : legacyLuaCheckInteger(L, index);
}
#undef luaL_checkinteger
#undef luaL_optinteger
#define luaL_checkinteger legacyLuaCheckInteger
#define luaL_optinteger legacyLuaOptInteger
#define lua_rawlen lua_objlen
inline void lua_pushglobaltable(lua_State* L) {
    lua_pushvalue(L, LUA_GLOBALSINDEX);
}
inline void* lua_newuserdatauv(lua_State* L, std::size_t size, int) {
    return lua_newuserdata(L, size);
}
inline int lua_getiuservalue(lua_State* L, int index, int slot) {
    if (slot != 1) {
        lua_pushnil(L);
        return LUA_TNIL;
    }
    lua_getfenv(L, index);
    return lua_type(L, -1);
}
inline int lua_setiuservalue(lua_State* L, int index, int slot) {
    if (slot != 1) {
        lua_pop(L, 1);
        return 0;
    }
    return lua_setfenv(L, index);
}
inline int lua_geti(lua_State* L, int index, lua_Integer key) {
    index = lua_absindex(L, index);
    lua_pushinteger(L, key);
    lua_gettable(L, index);
    return lua_type(L, -1);
}
inline lua_Integer luaL_len(lua_State* L, int index) {
    if (luaL_callmeta(L, index, "__len")) {
        const auto size = luaL_checkinteger(L, -1);
        lua_pop(L, 1);
        return size;
    }
    return static_cast<lua_Integer>(lua_objlen(L, index));
}
inline const char* luaL_tolstring(lua_State* L, int index, std::size_t* size) {
    index = lua_absindex(L, index);
    if (luaL_callmeta(L, index, "__tostring")) {
        if (!lua_isstring(L, -1)) {
            luaL_error(L, "__tostring must return a string");
        }
    } else {
        switch (lua_type(L, index)) {
        case LUA_TNUMBER:
        case LUA_TSTRING:
            lua_pushvalue(L, index);
            break;
        case LUA_TNIL:
            lua_pushliteral(L, "nil");
            break;
        case LUA_TBOOLEAN:
            lua_pushstring(L, lua_toboolean(L, index) ? "true" : "false");
            break;
        default:
            lua_pushfstring(L, "%s: %p", luaL_typename(L, index), lua_topointer(L, index));
            break;
        }
    }
    return lua_tolstring(L, -1, size);
}
inline void luaL_requiref(lua_State* L, const char* name, lua_CFunction open, int global) {
    luaL_findtable(L, LUA_REGISTRYINDEX, "_LOADED", 1);
    lua_getfield(L, -1, name);
    if (!lua_toboolean(L, -1)) {
        lua_pop(L, 1);
        lua_pushcfunction(L, open);
        lua_pushstring(L, name);
        lua_call(L, 1, 1);
        lua_pushvalue(L, -1);
        lua_setfield(L, -3, name);
    }
    lua_remove(L, -2);
    if (global) {
        lua_pushvalue(L, -1);
        lua_setglobal(L, name);
    }
}
// One owner per VM, shared with its coroutine threads via the registry. Unlike
// native Lua extraspace, lookup creates no new storage in the instruction hook.
inline char legacyLuaOwnerKey;
inline void legacyLuaSetOwner(lua_State* L, void* owner) {
    lua_pushlightuserdata(L, &legacyLuaOwnerKey);
    lua_pushlightuserdata(L, owner);
    lua_rawset(L, LUA_REGISTRYINDEX);
}
inline void* legacyLuaOwner(lua_State* L) {
    lua_pushlightuserdata(L, &legacyLuaOwnerKey);
    lua_rawget(L, LUA_REGISTRYINDEX);
    void* owner = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return owner;
}
// LuaJIT exposes the Lua 5.1 two-argument coroutine resume API.
inline int legacyLuaResume(lua_State* thread, int arguments) {
    return lua_resume(thread, arguments);
}
#define LUA_UTF8LIBNAME "utf8"
int luaopen_utf8(lua_State* L);
