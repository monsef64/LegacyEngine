#pragma once
#include "assets/assets.hpp"
#include "core/components.hpp"
#include "editor/script_explorer.hpp"
#include "scripting/lua_console.hpp"
#include "scripting/script_exports.hpp"
#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

class ScriptEditor {
  public:
    ScriptEditor();
    std::vector<std::string> componentOutput;
    // Loads an asset script into the editor buffer. Kept UI-free so the Qt
    // Script panel can drive it directly.
    bool loadFile(AssetManager& assets, const std::string& path) {
        if (!AssetManager::script(path)) {
            fileError_ = "Only .ls and .lua scripts can be opened";
            return false;
        }
        try {
            setSource(assets.readScript(path));
        } catch (const std::exception& e) {
            fileError_ = e.what();
            return false;
        }
        assetPath_ = path;
        fileError_.clear();
        fileDirty_ = false;
        return true;
    }
    bool saveFile(AssetManager& assets) {
        if (!AssetManager::script(assetPath_)) {
            return false;
        }
        assets.writeScript(assetPath_, source());
        fileDirty_ = false;
        return true;
    }
    bool executeSource(World& world, AssetManager& assets, const std::string& path) {
        (void)assets;
        bindWorld(world);
        setAssetContext(path, selected_);
        return executeSource();
    }
    const std::string& error() const {
        return fileError_;
    }
    const std::string& path() const {
        return assetPath_;
    }
    Entity target() const {
        return selected_;
    }
    void markDirty() {
        fileDirty_ = true;
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
    void bindWorld(World& world) {
        world_ = &world;
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
    // Binds the consoles' script helpers to the engine meshes; the Qt editor
    // calls this once during setup, exactly where the old editor did.
    void bindConsoleMeshes(World& world,
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
    std::string completionPrefix_;
};
