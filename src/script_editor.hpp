#pragma once
#include "lua_console.hpp"
#include <array>
#include <string>
struct ImGuiInputTextCallbackData;

class ScriptEditor {
public:
    ScriptEditor();
    void draw();
    void bindWorld(World& world,MeshHandle cube,MeshHandle sphere,MeshHandle capsule,MeshHandle cone={}) { console_.bindWorld(world,cube,sphere,capsule,cone); }
    void update(float dt) { console_.update(dt); }
    void fixedUpdate(float dt) { console_.fixedUpdate(dt); }
    void shutdown() { console_.shutdown(); }
    std::string_view source() const { return source_.data(); }
    const LuaConsole& console() const { return console_; }
private:
    LuaConsole console_;
    std::array<char,LuaConsole::maxSourceBytes+1> source_{};
    static int completion(ImGuiInputTextCallbackData* data);
    std::string completionPrefix_;
    bool autoScroll_=true;
    bool scrollToBottom_=false;
};
