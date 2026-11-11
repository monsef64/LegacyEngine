#pragma once

#include <cstddef>
#include <deque>
#include <string>
#include <string_view>

struct lua_State;
struct lua_Debug;

// Persistent, restricted Lua state and bounded console transcript; no UI dependency.
class LuaConsole {
public:
    enum class Kind { Info, Output, Error };
    struct Line { Kind kind; std::string text; };
    static constexpr std::size_t maxSourceBytes = 64 * 1024;
    static constexpr std::size_t maxLines = 1000;
    static constexpr std::size_t maxLineBytes = 8192;
    static constexpr std::size_t memoryLimit = 32 * 1024 * 1024;

    LuaConsole();
    ~LuaConsole();
    LuaConsole(const LuaConsole&) = delete;
    LuaConsole& operator=(const LuaConsole&) = delete;
    bool execute(std::string_view source);
    void reset();
    void clear() { lines_.clear(); }
    const std::deque<Line>& lines() const { return lines_; }
    bool ready() const { return state_ != nullptr; }

private:
    lua_State* state_ = nullptr;
    std::size_t allocated_ = 0;
    int remainingInstructions_ = 0;
    std::deque<Line> lines_;
    void append(Kind kind, std::string_view text);
    static void* allocate(void* user, void* pointer, std::size_t oldSize, std::size_t newSize);
    static int print(lua_State* state);
    static int traceback(lua_State* state);
    static void instructionHook(lua_State* state, lua_Debug* debug);
};
