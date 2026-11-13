#pragma once
#include "scripting/lua_console.hpp"
#include "core/components.hpp"
#include "scripting/script_exports.hpp"
#include "editor/editor_layout.hpp"
#include <array>
#include <string>
#include <vector>
#include <algorithm>
struct ImGuiInputTextCallbackData;

class ScriptEditor {
public:
    ScriptEditor();
    std::vector<std::string> componentOutput;
    void draw(const EditorLayout* layout=nullptr, bool executionEnabled=true);
    void startPlay() {
        console_.reset();
        // Asset scripts are executed by ScriptComponents, once per attachment.
        if(assetPath_.empty())console_.execute(source_.data());
    }
    void setAssetContext(std::string_view path,Entity selected) { assetPath_=path;selected_=selected; }
    bool executeSource() {
        Entity owner{};int count=0;
        if(world_ && !assetPath_.empty()) {
            world_->each<LuaScript>([&](Entity e,LuaScript& scripts) {
                for(const auto& script:scriptAttachments(scripts))if(script.path==assetPath_) {
                    ++count;owner=e;break;
                }
            });
            if(world_->alive(selected_))if(auto* scripts=world_->tryGet<LuaScript>(selected_))
                for(const auto& script:scriptAttachments(*scripts))if(script.path==assetPath_){owner=selected_;count=1;break;}
            if(count!=1)return false;
        }
        console_.reset();
        if(!assetPath_.empty() && !console_.bindObject(owner))return false;
        if(!assetPath_.empty()){
            auto& scripts=world_->get<LuaScript>(owner);auto* fields=&scripts.fields;
            if(scripts.path!=assetPath_)for(auto& extra:scripts.additional)if(extra.path==assetPath_){fields=&extra.fields;break;}
            syncScriptExports(source_.data(),*fields);if(!console_.bindExports(*fields))return false;
        }
        console_.execute(source_.data());return true;
    }
    void resetRuntime() { console_.reset(); }
    void bindWorld(World& world,MeshHandle cube,MeshHandle sphere,MeshHandle capsule,MeshHandle cone={},MeshHandle plane={}) { world_=&world;console_.bindWorld(world,cube,sphere,capsule,cone,plane); }
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
    World* world_=nullptr;
    std::string assetPath_;
    Entity selected_{};
    bool ownerError_=false;
    LuaConsole console_;
    std::array<char,LuaConsole::maxSourceBytes+1> source_{};
    static int completion(ImGuiInputTextCallbackData* data);
    std::string completionPrefix_;
    bool autoScroll_=true;
    bool scrollToBottom_=false;
};
