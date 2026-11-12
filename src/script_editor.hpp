#pragma once
#include "lua_console.hpp"
#include "editor_layout.hpp"
#include <array>
#include <string>
#include <algorithm>
struct ImGuiInputTextCallbackData;

class ScriptEditor {
public:
    ScriptEditor();
    void draw(const EditorLayout* layout=nullptr, bool executionEnabled=true);
    void startPlay() { console_.reset(); console_.execute(source_.data()); }
    void resetRuntime() { console_.reset(); }
    void bindWorld(World& world,MeshHandle cube,MeshHandle sphere,MeshHandle capsule,MeshHandle cone={}) { console_.bindWorld(world,cube,sphere,capsule,cone); }
    void update(float dt) { console_.update(dt); }
    void fixedUpdate(float dt) { console_.fixedUpdate(dt); }
    void shutdown() { console_.shutdown(); }
    void setSource(std::string_view text) {
        source_.fill(0);
        std::copy_n(text.data(),std::min(text.size(),source_.size()-1),source_.data());
        completionPrefix_.clear();
    }
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
