#include "scripting/game_services.hpp"
#include "assets/assets.hpp"
#include "assets/image_write.hpp"
#include "engine_version.hpp"
#include "project/json_io.hpp"
#include "scripting/lua_console.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <glad/gl.h>
#include <random>
#include <set>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
extern "C" {
#include <lauxlib.h>
#include <lua.h>
}
namespace GameServices {
namespace {
SDL_Window* window = nullptr;
bool editor = true, active = false, focused = true, quitting = false, ready = false, block = false,
     embedded = false;
Uint64 quitTime = 0;
unsigned quitFrames = 0;
int width = 0, height = 0, targetRate = -1;
float brightness = 1;
std::string company, game, screenshot;
std::filesystem::path dataPath, userPath;
constexpr std::size_t maxFile = 16 * 1024 * 1024;
struct Handle {
    FILE* file = nullptr;
    bool append = false;
};
unsigned openFiles = 0;
std::filesystem::path path(const std::string& name) {
    if (userPath.empty()) {
        throw std::runtime_error("Game services have not been initialized");
    }
    if (name.empty() || name.size() > 1024 || name.find('\0') != std::string::npos ||
        name.find(':') != std::string::npos || name.find('#') != std::string::npos ||
        name.find('\\') != std::string::npos || std::filesystem::path(name).is_absolute()) {
        throw std::runtime_error("Expected a relative path inside Game.userDataPath");
    }
    for (const auto& part : std::filesystem::path(name)) {
        if (part == ".." || part == ".") {
            throw std::runtime_error("Path traversal is forbidden");
        }
    }
    AssetManager sandbox;
    sandbox.root = userPath;
    return sandbox.resolve(name);
}
void resolution(lua_State* L, int w, int h, float refresh) {
    lua_createtable(L, 0, 3);
    lua_pushinteger(L, w);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, h);
    lua_setfield(L, -2, "height");
    lua_pushnumber(L, refresh);
    lua_setfield(L, -2, "refreshRate");
}
std::vector<SDL_DisplayMode> modes() {
    std::vector<SDL_DisplayMode> result;
    if (!window) {
        return result;
    }
    int count = 0;
    auto** list = SDL_GetFullscreenDisplayModes(SDL_GetDisplayForWindow(window), &count);
    if (list) {
        for (int i = 0; i < count; ++i) {
            if (list[i] && list[i]->w > 0 && list[i]->h > 0) {
                result.push_back(*list[i]);
            }
        }
        SDL_free(list);
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.w == b.w ? a.h < b.h : a.w < b.w;
    });
    result.erase(std::unique(result.begin(),
                             result.end(),
                             [](const auto& a, const auto& b) { return a.w == b.w && a.h == b.h; }),
                 result.end());
    return result;
}
bool validResolution(int w, int h) {
    for (const auto& m : modes()) {
        if (m.w == w && m.h == h) {
            return true;
        }
    }
    return false;
}
Json encode(lua_State* L, int index, int depth, std::set<const void*>& seen, std::size_t& count) {
    if (depth > 8 || ++count > 4096) {
        throw std::runtime_error("Save table exceeds depth/entry limit");
    }
    switch (lua_type(L, index)) {
    case LUA_TBOOLEAN:
        return bool(lua_toboolean(L, index));
    case LUA_TNUMBER: {
        auto x = lua_tonumber(L, index);
        if (!std::isfinite(x)) {
            throw std::runtime_error("Expected finite number");
        }
        if (lua_isinteger(L, index)) {
            return lua_tointeger(L, index);
        }
        return x;
    }
    case LUA_TSTRING: {
        size_t size = 0;
        const auto* text = lua_tolstring(L, index, &size);
        if (size > 65536) {
            throw std::runtime_error("Save string exceeds 64 KiB");
        }
        return std::string(text, size);
    }
    case LUA_TTABLE: {
        index = lua_absindex(L, index);
        const auto* identity = lua_topointer(L, index);
        if (!seen.insert(identity).second) {
            throw std::runtime_error("Cyclic save table");
        }
        Json result = Json::array();
        lua_pushnil(L);
        while (lua_next(L, index)) {
            if (lua_type(L, -2) != LUA_TSTRING && !lua_isinteger(L, -2)) {
                throw std::runtime_error("Save table keys must be strings or integers");
            }
            size_t keySize = 0;
            const char* keyText =
                lua_type(L, -2) == LUA_TSTRING ? lua_tolstring(L, -2, &keySize) : nullptr;
            if (keySize > 65536) {
                throw std::runtime_error("Table key exceeds 64 KiB");
            }
            Json key = keyText ? Json(std::string(keyText, keySize)) : Json(lua_tointeger(L, -2));
            result.push_back({{"key", key}, {"value", encode(L, -1, depth + 1, seen, count)}});
            lua_pop(L, 1);
        }
        seen.erase(identity);
        return Json{{"entries", result}};
    }
    default:
        throw std::runtime_error(
            "Save values support strings, finite numbers, booleans and tables only");
    }
}
void validateValue(const Json& j, int depth, std::size_t& count) {
    if (depth > 8 || ++count > 4096) {
        throw std::runtime_error("Saved value exceeds limits");
    }
    if (j.is_string()) {
        if (j.get_ref<const std::string&>().size() > 65536) {
            throw std::runtime_error("Saved string exceeds limit");
        }
        return;
    }
    if (j.is_boolean()) {
        return;
    }
    if (j.is_number()) {
        if (!std::isfinite(j.get<double>()) ||
            (j.is_number_unsigned() && j.get<std::uint64_t>() > std::uint64_t(INT64_MAX))) {
            throw std::runtime_error("Invalid saved number");
        }
        return;
    }
    if (!j.is_object() || !j.contains("entries") || !j.at("entries").is_array() ||
        j.at("entries").size() > 4096) {
        throw std::runtime_error("Invalid saved table");
    }
    for (const auto& entry : j.at("entries")) {
        const auto& key = entry.at("key");
        if (!key.is_string() && !key.is_number_integer()) {
            throw std::runtime_error("Invalid saved table key");
        }
        validateValue(key, depth + 1, count);
        validateValue(entry.at("value"), depth + 1, count);
    }
}
void decode(lua_State* L, const Json& j, int depth = 0) {
    if (depth > 8) {
        throw std::runtime_error("Save table too deep");
    }
    if (j.is_boolean()) {
        lua_pushboolean(L, j.get<bool>());
    } else if (j.is_number_integer()) {
        lua_pushinteger(L, j.get<lua_Integer>());
    } else if (j.is_number()) {
        lua_pushnumber(L, j.get<double>());
    } else if (j.is_string()) {
        const auto& s = j.get_ref<const std::string&>();
        lua_pushlstring(L, s.data(), s.size());
    } else if (j.is_object() && j.contains("entries") && j.at("entries").is_array() &&
               j.at("entries").size() <= 4096) {
        lua_newtable(L);
        for (const auto& e : j.at("entries")) {
            decode(L, e.at("key"), depth + 1);
            decode(L, e.at("value"), depth + 1);
            lua_rawset(L, -3);
        }
    } else {
        throw std::runtime_error("Invalid saved value");
    }
}
int fileGC(lua_State* L) {
    auto* h = static_cast<Handle*>(luaL_checkudata(L, 1, "GameFile"));
    if (h->file) {
        std::fclose(h->file);
        h->file = nullptr;
        if (openFiles) {
            --openFiles;
        }
    }
    return 0;
}
int operation(lua_State* L) {
    const int op = int(lua_tointeger(L, lua_upvalueindex(1)));
    // Lua checks happen before owning C++ values; host exceptions become nil,error.
    const char* text = nullptr;
    size_t length = 0;
    if (op == 4 || op == 5 || op == 6 || op == 20 || op == 21 || op == 22 || op == 23 || op == 24 ||
        op == 25 || op == 26 || op == 27 || op == 28 || op == 29 || op == 30 || op == 31 ||
        op == 32 || op == 33 || op == 34 || op == 35 || op == 36 || op == 37 || op == 40 ||
        op == 47 || op == 48) {
        text = luaL_checklstring(L, 1, &length);
    }
    if (length > 65536) {
        return luaL_error(L, "Argument exceeds 64 KiB");
    }
    const char* extra = nullptr;
    if (op == 40 || op == 47) {
        extra = luaL_checkstring(L, 2);
    }
    // Component vector properties are opaque userdata; snapshot their coordinates
    // before constructing C++ values, as their Lua accessors can raise errors.
    if (op == 25 && luaL_testudata(L, 2, "Vector")) {
        lua_newtable(L);
        for (const char* axis : {"x", "y", "z"}) {
            lua_getfield(L, 2, axis);
            lua_setfield(L, -2, axis);
        }
        lua_replace(L, 2);
    }
    bool autoCloseLines = false;
    if (op == 46 && lua_type(L, 1) == LUA_TSTRING) {
        lua_pushinteger(L, 40);
        lua_pushcclosure(L, operation, 1);
        lua_pushvalue(L, 1);
        lua_pushliteral(L, "r");
        lua_call(L, 2, 2);
        if (lua_isnil(L, -2)) {
            return 2;
        }
        lua_pop(L, 1);
        lua_replace(L, 1);
        lua_settop(L, 1);
        autoCloseLines = true;
    }
    Handle* handle = nullptr;
    if (op >= 41 && op <= 46) {
        handle = static_cast<Handle*>(luaL_checkudata(L, 1, "GameFile"));
        if (!handle->file) {
            return luaL_error(L, "File is closed");
        }
    }
    if (op == 42) {
        text = luaL_checklstring(L, 2, &length);
    }
    if (op == 42 && length > maxFile) {
        return luaL_error(L, "Write exceeds 16 MiB");
    }
    char error[1024]{};
    int results = 0;
    try {
        if (op == 0) {
            auto available = modes();
            lua_createtable(L, int(available.size()), 0);
            int i = 0;
            for (const auto& m : available) {
                resolution(L, m.w, m.h, m.refresh_rate);
                lua_rawseti(L, -2, ++i);
            }
            return 1;
        }
        if (op == 1 || op == 2) {
            auto requestedW = luaL_checkinteger(L, 1), requestedH = luaL_checkinteger(L, 2);
            const int w = requestedW > 0 && requestedW <= 32768 ? int(requestedW) : 0;
            const int h = requestedH > 0 && requestedH <= 32768 ? int(requestedH) : 0;
            bool ok = validResolution(w, h);
            if (op == 2 && embedded) {
                ok = false;
            }
            if (op == 2 && ok) {
                if (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) {
                    ok = false;
                    for (const auto& mode : modes()) {
                        if (mode.w == w && mode.h == h) {
                            ok = SDL_SetWindowFullscreenMode(window, &mode);
                            break;
                        }
                    }
                } else {
                    ok = SDL_SetWindowSize(window, w, h);
                }
                if (ok) {
                    SDL_SyncWindow(window);
                }
            }
            lua_pushboolean(L, ok);
            return 1;
        }
        if (op == 3) {
            requestQuit();
            return 0;
        }
        if (op == 4) {
            std::string url(text, length);
            if (url.size() > 4096 || std::any_of(url.begin(), url.end(), [](unsigned char c) {
                    return c < 32 || c == 127;
                })) {
                throw std::runtime_error("Invalid URL");
            }
            if (url.rfind("https://", 0) != 0 && url.rfind("http://", 0) != 0) {
                throw std::runtime_error("Only HTTP/HTTPS URLs are permitted");
            }
            bool ok = SDL_OpenURL(url.c_str());
            lua_pushboolean(L, ok);
            if (ok) {
                LuaConsole::postEvent("OnUrlOpened", {url});
            }
            return 1;
        }
        if (op == 5) {
            if (!screenshot.empty()) {
                throw std::runtime_error("A screenshot is already pending");
            }
            auto destination = path(std::string(text, length));
            if (destination.extension() != ".png") {
                throw std::runtime_error("Screenshot path must end in .png");
            }
            screenshot = std::string(text, length);
            lua_pushboolean(L, true);
            return 1;
        }
        if (op >= 20 && op <= 37) {
            std::string key(text, length);
            if (key.empty() || key.size() > 128) {
                throw std::runtime_error("Save key must be 1..128 bytes");
            }
            auto location = path("Saving.json");
            Json store = std::filesystem::exists(location) ? readJsonFile(location, maxFile)
                                                           : Json::object();
            const bool saving = op < 30;
            const int type = saving ? op - 20 : op - 30;
            static const char* names[]{"String", "Int", "Float", "Bool", "Table", "Vector3"};
            if (type < 0 || type > 5) {
                throw std::runtime_error("Unknown saving operation");
            }
            if (saving) {
                if (type == 0 && lua_type(L, 2) != LUA_TSTRING) {
                    throw std::runtime_error("Expected string");
                }
                if (type == 1 && !lua_isinteger(L, 2)) {
                    throw std::runtime_error("Expected integer");
                }
                if (type == 2 && lua_type(L, 2) != LUA_TNUMBER) {
                    throw std::runtime_error("Expected float");
                }
                if (type == 3 && lua_type(L, 2) != LUA_TBOOLEAN) {
                    throw std::runtime_error("Expected boolean");
                }
                if ((type == 4 || type == 5) && !lua_istable(L, 2)) {
                    throw std::runtime_error("Expected table");
                }
                if (type == 5) {
                    for (const char* axis : {"x", "y", "z"}) {
                        lua_getfield(L, 2, axis);
                        bool ok =
                            lua_type(L, -1) == LUA_TNUMBER && std::isfinite(lua_tonumber(L, -1));
                        lua_pop(L, 1);
                        if (!ok) {
                            throw std::runtime_error("Vector3 requires finite x,y,z");
                        }
                    }
                }
                std::set<const void*> seen;
                std::size_t count = 0;
                auto value = encode(L, 2, 0, seen, count);
                store[key] = {{"type", names[type]}, {"value", value}};
                if (store.dump().size() > maxFile) {
                    throw std::runtime_error("Save store exceeds 16 MiB");
                }
                auto temporary = path("Saving.json.writing");
                try {
                    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
                    out << store.dump();
                    out.close();
                    if (!out) {
                        throw std::runtime_error("Cannot write save store");
                    }
#ifdef _WIN32
                    if (!MoveFileExW(temporary.c_str(),
                                     location.c_str(),
                                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
                        throw std::runtime_error("Cannot replace save store");
                    }
#else
                    std::filesystem::rename(temporary, location);
#endif
                } catch (...) {
                    std::error_code ec;
                    std::filesystem::remove(temporary, ec);
                    throw;
                }
                lua_pushboolean(L, true);
                return 1;
            }
            if (!store.contains(key) || store[key].value("type", "") != names[type]) {
                lua_pushvalue(L, 2);
                return 1;
            }
            const auto& loaded = store[key].at("value");
            if ((type == 0 && !loaded.is_string()) || (type == 1 && !loaded.is_number_integer()) ||
                (type == 2 && !loaded.is_number()) || (type == 3 && !loaded.is_boolean()) ||
                ((type == 4 || type == 5) && !loaded.is_object())) {
                throw std::runtime_error("Saved type mismatch");
            }
            std::size_t count = 0;
            validateValue(loaded, 0, count);
            decode(L, loaded);
            return 1;
        }
        if (op == 40) {
            if (std::strcmp(extra, "r") && std::strcmp(extra, "rb") && std::strcmp(extra, "w") &&
                std::strcmp(extra, "wb") && std::strcmp(extra, "a") && std::strcmp(extra, "ab")) {
                throw std::runtime_error("File modes: r, rb, w, wb, a, ab");
            }
            if (openFiles >= 64) {
                throw std::runtime_error("At most 64 game files may be open");
            }
            auto location = path(std::string(text, length));
            if (*extra != 'r') {
                std::filesystem::create_directories(location.parent_path());
            }
            if (std::filesystem::exists(location) &&
                std::filesystem::file_size(location) > maxFile) {
                throw std::runtime_error("File exceeds 16 MiB");
            }
            auto* h = static_cast<Handle*>(lua_newuserdatauv(L, sizeof(Handle), 0));
            h->file = nullptr;
            h->append = *extra == 'a';
            luaL_setmetatable(L, "GameFile");
#ifdef _WIN32
            std::wstring mode;
            for (const char* p = extra; *p; ++p) {
                mode.push_back(wchar_t(*p));
            }
            h->file = _wfopen(location.c_str(), mode.c_str());
#else
            h->file = std::fopen(location.c_str(), extra);
#endif
            if (!h->file) {
                throw std::runtime_error("Cannot open file");
            }
            ++openFiles;
            return 1;
        }
        if (op == 41) {
            if (lua_type(L, 2) == LUA_TNUMBER) {
                auto n = lua_tointeger(L, 2);
                if (n < 0 || n > lua_Integer(maxFile)) {
                    throw std::runtime_error("Invalid read size");
                }
                std::string data(std::size_t(n), '\0');
                data.resize(std::fread(data.data(), 1, data.size(), handle->file));
                lua_pushlstring(L, data.data(), data.size());
                return 1;
            }
            const char* format = lua_isnoneornil(L, 2) ? "*l" : lua_tostring(L, 2);
            if (!format) {
                throw std::runtime_error("Read expects byte count, *a or *l");
            }
            bool line = !std::strcmp(format, "*l");
            if (!line && std::strcmp(format, "*a")) {
                throw std::runtime_error("Unknown read format");
            }
            std::string data;
            int c = EOF;
            while (data.size() < maxFile && (c = std::fgetc(handle->file)) != EOF) {
                if (line && c == '\n') {
                    break;
                }
                data.push_back(char(c));
            }
            if (data.size() == maxFile) {
                throw std::runtime_error("Read limit reached");
            }
            if (line && data.empty() && c == EOF) {
                lua_pushnil(L);
            } else {
                lua_pushlstring(L, data.data(), data.size());
            }
            return 1;
        }
        if (op == 42) {
            if (handle->append) {
                std::fseek(handle->file, 0, SEEK_END);
            }
            long pos = std::ftell(handle->file);
            if (pos < 0 || std::uint64_t(pos) + length > maxFile) {
                throw std::runtime_error("File exceeds 16 MiB");
            }
            lua_pushboolean(L, std::fwrite(text, 1, length, handle->file) == length);
            return 1;
        }
        if (op == 43) {
            fileGC(L);
            return 0;
        }
        if (op == 44) {
            lua_pushboolean(L, std::fflush(handle->file) == 0);
            return 1;
        }
        if (op == 45) {
            const char* mode = luaL_optstring(L, 2, "cur");
            long offset = long(luaL_optinteger(L, 3, 0));
            int whence = !std::strcmp(mode, "set")   ? SEEK_SET
                         : !std::strcmp(mode, "cur") ? SEEK_CUR
                         : !std::strcmp(mode, "end") ? SEEK_END
                                                     : -1;
            if (whence < 0 || offset < -long(maxFile) || offset > long(maxFile)) {
                throw std::runtime_error("Invalid seek");
            }
            if (std::fseek(handle->file, offset, whence)) {
                throw std::runtime_error("Seek failed");
            }
            auto pos = std::ftell(handle->file);
            if (pos < 0 || pos > long(maxFile)) {
                throw std::runtime_error("Seek exceeds limit");
            }
            lua_pushinteger(L, pos);
            return 1;
        }
        if (op == 46) {
            lua_pushvalue(L, 1);
            lua_pushboolean(L, autoCloseLines);
            lua_pushcclosure(
                L,
                +[](lua_State* L) -> int {
                    lua_pushinteger(L, 41);
                    lua_pushcclosure(L, operation, 1);
                    lua_pushvalue(L, lua_upvalueindex(1));
                    lua_pushliteral(L, "*l");
                    lua_call(L, 2, 1);
                    if (lua_isnil(L, -1) && lua_toboolean(L, lua_upvalueindex(2))) {
                        auto* h = static_cast<Handle*>(lua_touserdata(L, lua_upvalueindex(1)));
                        if (h->file) {
                            std::fclose(h->file);
                            h->file = nullptr;
                            if (openFiles) {
                                --openFiles;
                            }
                        }
                    }
                    return 1;
                },
                2);
            return 1;
        }
        if (op == 47) {
            auto a = path(std::string(text, length)), b = path(extra);
            if (std::filesystem::exists(b)) {
                throw std::runtime_error("Destination exists");
            }
            std::filesystem::rename(a, b);
            lua_pushboolean(L, true);
            return 1;
        }
        if (op == 48) {
            auto a = path(std::string(text, length));
            if (std::filesystem::is_directory(a)) {
                throw std::runtime_error("File.remove does not remove directories");
            }
            lua_pushboolean(L, std::filesystem::remove(a));
            return 1;
        }
    } catch (const std::exception& e) {
        std::snprintf(error, sizeof(error), "%s", e.what());
    }
    lua_pushnil(L);
    lua_pushstring(L, error[0] ? error : "Unsupported operation");
    results = 2;
    return results;
}
void function(lua_State* L, const char* name, int op) {
    lua_pushinteger(L, op);
    lua_pushcclosure(L, operation, 1);
    lua_setfield(L, -2, name);
}
int screenIndex(lua_State* L) {
    const char* key = luaL_checkstring(L, 2);
    if (!std::strcmp(key, "width")) {
        lua_pushinteger(L, width);
    } else if (!std::strcmp(key, "height")) {
        lua_pushinteger(L, height);
    } else if (!std::strcmp(key, "brightness")) {
        lua_pushnumber(L, brightness);
    } else if (!std::strcmp(key, "fullScreen")) {
        lua_pushboolean(L, window && (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN));
    } else if (!std::strcmp(key, "currentResolution")) {
        const auto* m =
            window ? SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window)) : nullptr;
        resolution(L, m ? m->w : width, m ? m->h : height, m ? m->refresh_rate : 0);
    } else {
        int op = !std::strcmp(key, "getResolutions")   ? 0
                 : !std::strcmp(key, "getResolution")  ? 1
                 : !std::strcmp(key, "setResolution")  ? 2
                 : !std::strcmp(key, "takeScreenshot") ? 5
                                                       : -1;
        if (op < 0) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushinteger(L, op);
        lua_pushcclosure(L, operation, 1);
    }
    return 1;
}
int screenWrite(lua_State* L) {
    const char* key = luaL_checkstring(L, 2);
    if (!std::strcmp(key, "brightness")) {
        float value = float(luaL_checknumber(L, 3));
        if (!std::isfinite(value) || value < 0 || value > 1) {
            return luaL_error(L, "Brightness must be 0..1");
        }
        brightness = value;
    } else if (!std::strcmp(key, "fullScreen")) {
        luaL_checktype(L, 3, LUA_TBOOLEAN);
        if (embedded || !window || !SDL_SetWindowFullscreen(window, lua_toboolean(L, 3))) {
            return luaL_error(L, "Cannot change fullscreen mode");
        }
    } else {
        return luaL_error(L, "Read-only or unknown Screen property");
    }
    return 0;
}
int gameIndex(lua_State* L) {
    const char* key = luaL_checkstring(L, 2);
    if (!std::strcmp(key, "companyName")) {
        lua_pushlstring(L, company.data(), company.size());
    } else if (!std::strcmp(key, "getDataPath")) {
        auto p = dataPath.generic_string();
        lua_pushlstring(L, p.data(), p.size());
    } else if (!std::strcmp(key, "userDataPath")) {
        auto p = userPath.generic_string();
        lua_pushlstring(L, p.data(), p.size());
    } else if (!std::strcmp(key, "isEditor")) {
        lua_pushboolean(L, editor);
    } else if (!std::strcmp(key, "isFocused")) {
        lua_pushboolean(L, focused);
    } else if (!std::strcmp(key, "targetFrameRate")) {
        lua_pushinteger(L, targetRate);
    } else if (!std::strcmp(key, "engineVersion")) {
        lua_pushstring(L, EngineVersion::version);
    } else if (!std::strcmp(key, "blockGameKilling")) {
        lua_pushboolean(L, block);
    } else {
        int op = !std::strcmp(key, "quit") ? 3 : !std::strcmp(key, "openURL") ? 4 : -1;
        const char* saves[]{
            "saveString", "saveInt", "saveFloat", "saveBool", "saveTable", "saveVector3"};
        const char* loads[]{
            "loadString", "loadInt", "loadFloat", "loadBool", "loadTable", "loadVector3"};
        for (int i = 0; i < 6; ++i) {
            if (!std::strcmp(key, saves[i])) {
                op = 20 + i;
            }
            if (!std::strcmp(key, loads[i])) {
                op = 30 + i;
            }
        }
        if (op < 0) {
            lua_pushnil(L);
            return 1;
        }
        lua_pushinteger(L, op);
        lua_pushcclosure(L, operation, 1);
    }
    return 1;
}
int gameWrite(lua_State* L) {
    const char* key = luaL_checkstring(L, 2);
    if (!std::strcmp(key, "targetFrameRate")) {
        auto n = luaL_checkinteger(L, 3);
        if (n != -1 && (n < 1 || n > 1000)) {
            return luaL_error(L, "Frame rate must be -1 or 1..1000");
        }
        targetRate = int(n);
    } else if (!std::strcmp(key, "blockGameKilling")) {
        luaL_checktype(L, 3, LUA_TBOOLEAN);
        block = lua_toboolean(L, 3);
    } else {
        return luaL_error(L, "Read-only or unknown Game property");
    }
    return 0;
}
int randomFunction(lua_State* L) {
    const int op = int(lua_tointeger(L, lua_upvalueindex(1)));
    lua_Number lo = 0, hi = 0;
    lua_Integer a = 0, b = 0, length = 0;
    if (op == 0) {
        lo = luaL_checknumber(L, 1);
        hi = luaL_checknumber(L, 2);
        if (!std::isfinite(lo) || !std::isfinite(hi) || lo > hi) {
            return luaL_error(L, "Expected finite min <= max");
        }
    }
    if (op == 1) {
        a = luaL_checkinteger(L, 1);
        b = luaL_checkinteger(L, 2);
        if (a > b) {
            return luaL_error(L, "Expected min <= max");
        }
    }
    if (op == 3) {
        length = luaL_checkinteger(L, 1);
        if (length < 0 || length > 4096) {
            return luaL_error(L, "Random string length must be 0..4096");
        }
    }
    char error[256]{};
    try {
        static thread_local std::mt19937_64 generator{std::random_device{}()};
        if (op == 0) {
            std::uniform_real_distribution<long double> distribution(lo, hi);
            lua_pushnumber(L, std::clamp(lua_Number(distribution(generator)), lo, hi));
            return 1;
        }
        if (op == 1) {
            lua_pushinteger(L, std::uniform_int_distribution<lua_Integer>(a, b)(generator));
            return 1;
        }
        if (op == 2) {
            lua_pushboolean(L, std::bernoulli_distribution(.5)(generator));
            return 1;
        }
        constexpr char alphabet[] =
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
        std::string value(std::size_t(length), ' ');
        for (auto& c : value) {
            c = alphabet[std::uniform_int_distribution<unsigned>(0,
                                                                 sizeof(alphabet) - 2)(generator)];
        }
        lua_pushlstring(L, value.data(), value.size());
        return 1;
    } catch (const std::exception& e) {
        std::snprintf(error, sizeof(error), "%s", e.what());
    }
    lua_pushnil(L);
    lua_pushstring(L, error);
    return 2;
}
void proxy(lua_State* L, const char* name, lua_CFunction index, lua_CFunction write) {
    lua_newuserdatauv(L, 1, 0);
    lua_newtable(L);
    lua_pushcfunction(L, index);
    lua_setfield(L, -2, "__index");
    lua_pushcfunction(L, write);
    lua_setfield(L, -2, "__newindex");
    lua_pushliteral(L, "locked");
    lua_setfield(L, -2, "__metatable");
    lua_setmetatable(L, -2);
    lua_setglobal(L, name);
}
} // namespace
void install(lua_State* L) {
    proxy(L, "Screen", screenIndex, screenWrite);
    proxy(L, "Game", gameIndex, gameWrite);
    lua_newtable(L);
    const char* randomNames[]{
        "randomFloat", "randomInt", "randomBool", "randomString"};
    for (int i = 0; i < 4; ++i) {
        lua_pushinteger(L, i);
        lua_pushcclosure(L, randomFunction, 1);
        lua_pushvalue(L, -1);
        lua_setglobal(L, randomNames[i]);
        lua_setfield(L, -2, randomNames[i]);
    }
    lua_setglobal(L, "Random");
    luaL_newmetatable(L, "GameFile");
    lua_pushcfunction(L, fileGC);
    lua_setfield(L, -2, "__gc");
    lua_pushliteral(L, "locked");
    lua_setfield(L, -2, "__metatable");
    lua_pop(L, 1);
    lua_newtable(L);
    const char* names[]{
        "open", "read", "write", "close", "flush", "seek", "lines", "rename", "remove"};
    for (int i = 0; i < 9; ++i) {
        function(L, names[i], 40 + i);
    }
    lua_setglobal(L, "File");
}
void configure(SDL_Window* w,
               bool inEditor,
               const std::string& c,
               const std::string& g,
               const std::filesystem::path& data,
               bool embeddedGame) {
    window = w;
    editor = inEditor;
    embedded = embeddedGame;
    company = c;
    game = g;
    dataPath = data;
    cancelQuit();
    brightness = 1;
    targetRate = -1;
    std::string id;
    for (unsigned char ch : g) {
        id += std::isalnum(ch) ? char(ch) : '_';
    }
    if (id.empty()) {
        id = "Game";
    }
    if (char* p = SDL_GetPrefPath("LegacyEngine", id.c_str())) {
        userPath = p;
        SDL_free(p);
        std::filesystem::create_directories(userPath);
    } else {
        throw std::runtime_error("Cannot locate game user-data folder");
    }
    if (window) {
        SDL_GetWindowSizeInPixels(window, &width, &height);
    }
    focused = window && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS);
}
void requestQuit() {
    if (!active) {
        ready = true;
        return;
    }
    if (!quitting) {
        quitting = true;
        quitTime = SDL_GetTicksNS();
        quitFrames = 0;
        LuaConsole::postEvent("OnGamePreQuit");
    }
}
void cancelQuit() {
    quitting = ready = block = false;
    quitFrames = 0;
    screenshot.clear();
}
bool quitReady() {
    return ready;
}
bool pendingQuit() {
    return quitting;
}
void update(SDL_Window* w, bool enabled) {
    window = w;
    active = enabled;
    if (!active) {
        cancelQuit();
        return;
    }
    if (!window) {
        return;
    }
    int nw = 0, nh = 0;
    if (embedded) {
        nw = width;
        nh = height;
    } else {
        SDL_GetWindowSizeInPixels(window, &nw, &nh);
    }
    if (nw != width || nh != height) {
        width = nw;
        height = nh;
        LuaConsole::postEvent("OnResolutionChanged", {LuaConsole::Resolution{width, height, 0}});
    }
    bool now = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    if (now != focused) {
        focused = now;
        LuaConsole::postEvent(now ? "OnFocus" : "OnFocusLost");
    }
    if (quitting && ++quitFrames > 1 &&
        ((!block && !LuaConsole::hasTasks()) || SDL_GetTicksNS() - quitTime > 10000000000ull)) {
        ready = true;
    }
}
namespace {
void applyBrightness(unsigned framebuffer, int w, int h) {
    if (brightness >= 1) {
        return;
    }
    static GLuint program = 0, vao = 0;
    if (!program) {
        auto compile = [](GLenum type, const char* text) {
            GLuint s = glCreateShader(type);
            glShaderSource(s, 1, &text, nullptr);
            glCompileShader(s);
            return s;
        };
        GLuint v = compile(
            GL_VERTEX_SHADER,
            "#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);}");
        GLuint f = compile(
            GL_FRAGMENT_SHADER,
            "#version 330 core\nuniform float amount;out vec4 color;void main(){color=vec4(0,0,0,amount);}");
        program = glCreateProgram();
        glAttachShader(program, v);
        glAttachShader(program, f);
        glLinkProgram(program);
        glDeleteShader(v);
        glDeleteShader(f);
        glGenVertexArrays(1, &vao);
    }
    GLint oldProgram, oldVao, draw, viewport[4], src, dst, srcA, dstA;
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    glGetIntegerv(GL_VIEWPORT, viewport);
    glGetIntegerv(GL_BLEND_SRC_RGB, &src);
    glGetIntegerv(GL_BLEND_DST_RGB, &dst);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcA);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &dstA);
    bool blend = glIsEnabled(GL_BLEND), depth = glIsEnabled(GL_DEPTH_TEST),
         scissor = glIsEnabled(GL_SCISSOR_TEST), cull = glIsEnabled(GL_CULL_FACE);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_ZERO, GL_SRC_ALPHA, GL_ZERO, GL_ONE);
    glUseProgram(program);
    glBindVertexArray(vao);
    glUniform1f(glGetUniformLocation(program, "amount"), brightness);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBlendFuncSeparate(src, dst, srcA, dstA);
    if (!blend) {
        glDisable(GL_BLEND);
    }
    if (depth) {
        glEnable(GL_DEPTH_TEST);
    }
    if (scissor) {
        glEnable(GL_SCISSOR_TEST);
    }
    if (cull) {
        glEnable(GL_CULL_FACE);
    }
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    glBindVertexArray(oldVao);
    glUseProgram(oldProgram);
}
} // namespace
void finishFrame(unsigned framebuffer, int w, int h) {
    if (embedded && (w != width || h != height)) {
        width = w;
        height = h;
        LuaConsole::postEvent("OnResolutionChanged", {LuaConsole::Resolution{w, h, 0}});
    }
    applyBrightness(framebuffer, w, h);
    if (!screenshot.empty()) {
        try {
            auto destination = path(screenshot);
            std::filesystem::create_directories(destination.parent_path());
            if (w < 1 || h < 1 || w > 8192 || h > 8192) {
                throw std::runtime_error("Invalid screenshot dimensions");
            }
            std::vector<unsigned char> pixels(std::size_t(w) * h * 3);
            GLint read, pack, alignment;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
            glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &pack);
            glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
            glReadPixels(0, 0, w, h, GL_BGR, GL_UNSIGNED_BYTE, pixels.data());
            glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, pack);
            glPixelStorei(GL_PACK_ALIGNMENT, alignment);
            writeBgrPng(destination, w, h, pixels);
        } catch (const std::exception& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Screenshot: %s", e.what());
        }
        screenshot.clear();
    }
}
void paceFrame(Uint64 started) {
    if (targetRate > 0) {
        const Uint64 budget = 1000000000ull / unsigned(targetRate);
        const Uint64 elapsed = SDL_GetTicksNS() - started;
        if (elapsed < budget) {
            SDL_DelayNS(budget - elapsed);
        }
    }
}
} // namespace GameServices
