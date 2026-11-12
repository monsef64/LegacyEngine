#pragma once
#include "assets.hpp"
#include "lua_console.hpp"

// Each attached script has an isolated Lua state and a generation-safe `self`.
class ScriptComponents {
public:
    void start(World& world,AssetManager& assets,const MeshHandle* meshes) {
        stop();world_=&world;assets_=&assets;for(int i=0;i<4;++i)meshes_[i]=meshes[i];sync();
    }
    void update(float dt) {sync();for(auto& entry:entries_)if(entry.console)entry.console->update(dt);}
    void fixedUpdate(float dt) {sync();for(auto& entry:entries_)if(entry.console)entry.console->fixedUpdate(dt);}
    void stop() {for(auto& e:entries_)if(e.console)e.console->shutdown();entries_.clear();world_=nullptr;}
    std::vector<std::string> messages() const {
        std::vector<std::string> lines;
        for(const auto& e:entries_) {
            if(!e.error.empty())lines.push_back(e.path+": "+e.error);
            if(e.console)for(const auto& line:e.console->lines())lines.push_back(e.path+": "+line.text);
        }
        return lines;
    }
private:
    struct Entry {Entity entity;std::string path,error;std::unique_ptr<LuaConsole> console;};
    std::vector<Entry> entries_;
    World* world_=nullptr;AssetManager* assets_=nullptr;MeshHandle meshes_[4];
    bool valid(const Entry& e) {
        auto* script=world_->tryGet<LuaScript>(e.entity);
        return script && script->enabled && script->path==e.path;
    }
    void sync() {
        if(!world_)return;
        for(auto it=entries_.begin();it!=entries_.end();) {
            if(!valid(*it)){if(it->console)it->console->shutdown();it=entries_.erase(it);}else ++it;
        }
        std::vector<Entity> pending;
        world_->each<LuaScript>([&](Entity e,LuaScript& script) {
            if(!script.enabled || script.path.empty())return;
            for(const auto& entry:entries_)if(entry.entity==e)return;
            if(entries_.size()+pending.size()<128)pending.push_back(e);
        });
        for(Entity e:pending) {
            // A preceding script may have destroyed this object.
            auto* script=world_->tryGet<LuaScript>(e);if(!script || !script->enabled)continue;
            Entry entry{e,script->path,{},{}};
            try {
                auto source=assets_->readScript(entry.path);
                entry.console=std::make_unique<LuaConsole>();
                entry.console->bindWorld(*world_,meshes_[0],meshes_[1],meshes_[2],meshes_[3]);
                if(entry.console->bindObject(e))entry.console->execute(source);
            } catch(const std::exception& error) {entry.error=error.what();}
            entries_.push_back(std::move(entry));
        }
    }
};
