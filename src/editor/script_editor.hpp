#pragma once
#include "assets/assets.hpp"
#include "core/components.hpp"
#include "editor/editor_layout.hpp"
#include "editor/script_explorer.hpp"
#include "scripting/lua_console.hpp"
#include "scripting/script_exports.hpp"
#include <algorithm>
#include <array>
#include <string>
#include <vector>
struct ImGuiInputTextCallbackData;

class ScriptEditor {
  public:
    ScriptEditor();
    std::vector<std::string> componentOutput;
    void draw(const EditorLayout* layout = nullptr,
              bool executionEnabled = true,
              AssetManager* assets = nullptr,
              std::string* openedScript = nullptr);
    void saveFile(AssetManager& assets) {
        if (fileDirty_ && AssetManager::script(assetPath_)) {
            assets.writeScript(assetPath_, source());
            fileDirty_ = false;
        }
    }
    bool hasUnsavedChanges() const {
        return fileDirty_;
    }
    bool fileOpen() const {
        return !assetPath_.empty() && AssetManager::script(assetPath_);
    }
    void startPlay() {
        console_.reset();
        // Asset scripts are executed by ScriptComponents, once per attachment.
    }
    void setAssetContext(std::string_view path, Entity selected) {
        assetPath_ = path;
        selected_ = selected;
    }
    bool executeSource() {
        if (!fileOpen()) {
            return false;
        }
        Entity owner{};
        int count = 0;
        if (world_ && !assetPath_.empty()) {
            world_->each<LuaScript>([&](Entity e, LuaScript& scripts) {
                for (const auto& script : scriptAttachments(scripts)) {
                    if (script.path == assetPath_) {
                        ++count;
                        owner = e;
                        break;
                    }
                }
            });
            if (world_->alive(selected_)) {
                if (auto* scripts = world_->tryGet<LuaScript>(selected_)) {
                    for (const auto& script : scriptAttachments(*scripts)) {
                        if (script.path == assetPath_) {
                            owner = selected_;
                            count = 1;
                            break;
                        }
                    }
                }
            }
            if (count != 1) {
                return false;
            }
        }
        console_.reset();
        if (!assetPath_.empty() && !console_.bindObject(owner)) {
            return false;
        }
        if (!assetPath_.empty()) {
            auto& scripts = world_->get<LuaScript>(owner);
            auto* fields = &scripts.fields;
            if (scripts.path != assetPath_) {
                for (auto& extra : scripts.additional) {
                    if (extra.path == assetPath_) {
                        fields = &extra.fields;
                        break;
                    }
                }
            }
            syncScriptExports(source_.data(), *fields);
            if (!console_.bindExports(*fields)) {
                return false;
            }
        }
        return console_.execute(source_.data(), assetPath_);
    }
    void resetRuntime() {
        console_.reset();
    }
    void bindWorld(World& world,
                   MeshHandle cube,
                   MeshHandle sphere,
                   MeshHandle capsule,
                   MeshHandle cone = {},
                   MeshHandle plane = {}) {
        world_ = &world;
        console_.bindWorld(world, cube, sphere, capsule, cone, plane);
    }
    void update(float dt) {
        console_.update(dt);
    }
    void fixedUpdate(float dt) {
        console_.fixedUpdate(dt);
    }
    void shutdown() {
        console_.shutdown();
    }
    void setSource(std::string_view text) {
        source_.fill(0);
        std::copy_n(text.data(), std::min(text.size(), source_.size() - 1), source_.data());
        completionPrefix_.clear();
        fileDirty_ = false;
    }
    std::string_view source() const {
        return source_.data();
    }
    LuaConsole& console() {
        return console_;
    }
    const LuaConsole& console() const {
        return console_;
    }

  private:
    World* world_ = nullptr;
    std::string assetPath_;
    Entity selected_{};
    bool ownerError_ = false, fileDirty_ = false;
    std::string fileError_;
    ScriptTree scriptTree_;
    std::filesystem::path treeRoot_;
    double treeRefresh_ = 0;
    LuaConsole console_;
    std::array<char, LuaConsole::maxSourceBytes + 1> source_{};
    static int completion(ImGuiInputTextCallbackData* data);
    std::string completionPrefix_;
    bool autoScroll_ = true;
    bool scrollToBottom_ = false;
};
