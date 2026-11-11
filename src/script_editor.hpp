#pragma once
#include "lua_console.hpp"
#include <array>

class ScriptEditor {
public:
    ScriptEditor();
    void draw();
    const LuaConsole& console() const { return console_; }
private:
    LuaConsole console_;
    std::array<char,LuaConsole::maxSourceBytes+1> source_{};
    bool autoScroll_=true;
    bool scrollToBottom_=false;
};
