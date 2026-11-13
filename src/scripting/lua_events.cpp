#include "scripting/lua_console.hpp"
#include <algorithm>
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}
namespace {
struct Event {
    std::string name;
    std::vector<LuaConsole::EventValue> values;
};
std::deque<Event> queue;
std::vector<LuaConsole*> subscribers;
constexpr std::size_t maxQueued = 256, maxListeners = 128, maxArguments = 8;
void pushValue(lua_State* L, const LuaConsole::EventValue& value) {
    if (auto* v = std::get_if<bool>(&value)) {
        lua_pushboolean(L, *v);
    } else if (auto* v = std::get_if<double>(&value)) {
        lua_pushnumber(L, *v);
    } else if (auto* v = std::get_if<std::string>(&value)) {
        lua_pushlstring(L, v->data(), v->size());
    } else {
        lua_pushnil(L);
    }
}
} // namespace
bool LuaConsole::postEvent(std::string name, std::vector<EventValue> values) {
    if (name.empty() || name.size() > 128 || values.size() > maxArguments ||
        queue.size() >= maxQueued) {
        return false;
    }
    for (const auto& value : values) {
        if (auto* s = std::get_if<std::string>(&value); s && s->size() > 4096) {
            return false;
        }
    }
    queue.push_back({std::move(name), std::move(values)});
    return true;
}
void LuaConsole::clearEvents() {
    queue.clear();
}
void LuaConsole::dispatchEvents() {
    // New events wait until the next pump; recursive emits cannot monopolize a frame.
    const auto count = queue.size();
    for (std::size_t i = 0; i < count && !queue.empty(); ++i) {
        auto event = std::move(queue.front());
        queue.pop_front();
        const auto recipients = subscribers;
        for (auto* console : recipients) {
            if (std::find(subscribers.begin(), subscribers.end(), console) != subscribers.end()) {
                console->receiveEvent(event.name, event.values);
            }
        }
    }
}
void LuaConsole::installEvents() {
    removeEventListeners();
    subscribers.push_back(this);
    for (int op = 0; op < 3; ++op) {
        lua_pushinteger(state_, op);
        lua_pushcclosure(state_, eventAPI, 1);
        lua_setglobal(state_, op == 0 ? "on" : op == 1 ? "off" : "emit");
    }
}
void LuaConsole::removeEventListeners() {
    if (state_) {
        for (const auto& listener : listeners_) {
            luaL_unref(state_, LUA_REGISTRYINDEX, listener.reference);
        }
    }
    listeners_.clear();
    subscribers.erase(std::remove(subscribers.begin(), subscribers.end(), this), subscribers.end());
}
int LuaConsole::eventAPI(lua_State* L) {
    auto* self = *static_cast<LuaConsole**>(lua_getextraspace(L));
    const int op = int(lua_tointeger(L, lua_upvalueindex(1)));
    if (op == 1) {
        const int token = int(luaL_checkinteger(L, 1));
        bool removed = false;
        for (auto it = self->listeners_.begin(); it != self->listeners_.end();) {
            if (it->token == token) {
                luaL_unref(L, LUA_REGISTRYINDEX, it->reference);
                it = self->listeners_.erase(it);
                removed = true;
            } else {
                ++it;
            }
        }
        lua_pushboolean(L, removed);
        return 1;
    }
    std::size_t length = 0;
    const char* text = luaL_checklstring(L, 1, &length);
    if (length == 0 || length > 128) {
        return luaL_error(L, "Event name must contain 1 to 128 bytes");
    }
    if (op == 0) {
        luaL_checktype(L, 2, LUA_TFUNCTION);
        if (self->listeners_.size() >= maxListeners) {
            return luaL_error(L, "Event listener limit reached");
        }
        lua_pushvalue(L, 2);
        const int ref = luaL_ref(L, LUA_REGISTRYINDEX);
        const int token = self->nextListener_++;
        self->listeners_.push_back({std::string(text, length), token, ref});
        if (std::find(subscribers.begin(), subscribers.end(), self) == subscribers.end()) {
            subscribers.push_back(self);
        }
        lua_pushinteger(L, token);
        return 1;
    }
    const int count = lua_gettop(L) - 1;
    if (count > int(maxArguments)) {
        return luaL_error(L, "Events support at most 8 arguments");
    }
    // Validate first, before owning C++ values across a Lua error longjmp.
    for (int i = 2; i <= count + 1; ++i) {
        const int type = lua_type(L, i);
        if (type != LUA_TNIL && type != LUA_TBOOLEAN && type != LUA_TNUMBER &&
            type != LUA_TSTRING) {
            return luaL_error(L, "Event payloads must be nil, boolean, number or string");
        }
        if (type == LUA_TSTRING && lua_rawlen(L, i) > 4096) {
            return luaL_error(L, "Event string exceeds 4096 bytes");
        }
    }
    std::vector<EventValue> values;
    for (int i = 2; i <= count + 1; ++i) {
        switch (lua_type(L, i)) {
        case LUA_TNIL:
            values.emplace_back(std::monostate{});
            break;
        case LUA_TBOOLEAN:
            values.emplace_back(bool(lua_toboolean(L, i)));
            break;
        case LUA_TNUMBER:
            values.emplace_back(double(lua_tonumber(L, i)));
            break;
        default: {
            std::size_t size;
            const char* s = lua_tolstring(L, i, &size);
            values.emplace_back(std::string(s, size));
        }
        }
    }
    lua_pushboolean(L, postEvent(std::string(text, length), std::move(values)));
    return 1;
}
void LuaConsole::receiveEvent(const std::string& name, const std::vector<EventValue>& values) {
    if (!state_) {
        return;
    }
    auto invoke = [&](int reference, bool global) {
        const int base = lua_gettop(state_);
        lua_pushcfunction(state_, traceback);
        if (global) {
            lua_getglobal(state_, name.c_str());
        } else {
            lua_rawgeti(state_, LUA_REGISTRYINDEX, reference);
        }
        if (!lua_isfunction(state_, -1)) {
            lua_settop(state_, base);
            return true;
        }
        for (const auto& value : values) {
            pushValue(state_, value);
        }
        beginBudget();
        const int result = lua_pcall(state_, int(values.size()), 0, base + 1);
        endBudget();
        if (result != LUA_OK) {
            const char* error = lua_tostring(state_, -1);
            append(Kind::Error, name + ": " + (error ? error : "Unknown callback error"));
        }
        lua_settop(state_, base);
        return result == LUA_OK;
    };
    if (name == "onLevelLoaded" || name == "onLevelFailedToLoad") {
        if (!invoke(0, true)) {
            lua_pushnil(state_);
            lua_setglobal(state_, name.c_str());
        }
    }
    const auto listeners = listeners_;
    for (const auto& listener : listeners) {
        auto found =
            std::find_if(listeners_.begin(), listeners_.end(), [&](const Listener& current) {
                return current.token == listener.token;
            });
        if (listener.name != name || found == listeners_.end()) {
            continue;
        }
        if (!invoke(listener.reference, false)) {
            found =
                std::find_if(listeners_.begin(), listeners_.end(), [&](const Listener& current) {
                    return current.token == listener.token;
                });
            if (found != listeners_.end()) {
                luaL_unref(state_, LUA_REGISTRYINDEX, found->reference);
                listeners_.erase(found);
            }
        }
    }
}
