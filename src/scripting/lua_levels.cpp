#include "levels/level_system.hpp"
#include "scripting/lua_console.hpp"
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}
namespace {
int levelCall(lua_State* L) {
    const int op = int(lua_tointeger(L, lua_upvalueindex(1)));
    auto* levels = LevelSystem::active;
    if (op == 4) {
        if (!levels || levels->current().empty()) {
            lua_pushnil(L);
        } else {
            lua_pushlstring(L, levels->current().data(), levels->current().size());
        }
        return 1;
    }
    if (!levels) {
        lua_pushboolean(L, false);
        lua_pushliteral(L, "No level system is available");
        return 2;
    }
    std::string name;
    if (op == 0 || op == 1 || op == 2 || op == 5) {
        std::size_t length = 0;
        const char* text = luaL_optlstring(L, 1, "", &length);
        if (length > 128 || std::string_view(text, length).find('\0') != std::string_view::npos) {
            return luaL_error(L, "Invalid level name");
        }
        name.assign(text, length);
    }
    bool accepted = false;
    if (op == 0 || op == 1 || op == 5) {
        accepted = levels->load(name, op == 1);
    } else if (op == 2) {
        accepted = levels->unload(name);
    } else if (op == 3) {
        if (levels->current().empty()) {
            levels->error = "No current level";
        } else {
            accepted = levels->load(levels->current());
        }
    } else {
        accepted = levels->relative(op == 6 ? 1 : -1);
    }
    lua_pushboolean(L, accepted);
    if (accepted) {
        lua_pushnil(L);
    } else {
        lua_pushstring(L, levels->error.c_str());
    }
    return 2;
}
} // namespace
void LuaConsole::installLevels() {
    const char* names[]{"loadLevel",
                        "loadLevelAsync",
                        "unloadLevel",
                        "restartLevel",
                        "getCurrentLevel",
                        "setCurrentLevel",
                        "nextLevel",
                        "previousLevel"};
    for (int i = 0; i < 8; ++i) {
        lua_pushinteger(state_, i);
        lua_pushcclosure(state_, levelCall, 1);
        lua_setglobal(state_, names[i]);
    }
}
