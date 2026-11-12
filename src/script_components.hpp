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
        if(script)for(const auto& entry:scriptAttachments(*script))if(entry.enabled && entry.path==e.path)return true;
        return false;
    }
    void sync() {
        if(!world_)return;
        for(auto it=entries_.begin();it!=entries_.end();) {
            if(!valid(*it)){if(it->console)it->console->shutdown();it=entries_.erase(it);}else ++it;
        }
        std::vector<std::pair<Entity,std::string>> pending;
        world_->each<LuaScript>([&](Entity e,LuaScript& scripts) {
            for(const auto& script:scriptAttachments(scripts)) {
                if(!script.enabled || script.path.empty())continue;
                bool found=false;for(const auto& entry:entries_)if(entry.entity==e && entry.path==script.path)found=true;
                if(!found && entries_.size()+pending.size()<128)pending.push_back({e,script.path});
            }
        });
        for(const auto& candidate:pending) {
            Entry entry{candidate.first,candidate.second,{},{}};
            if(!valid(entry))continue;
            bool duplicate=false;for(const auto& existing:entries_)if(existing.entity==entry.entity && existing.path==entry.path)duplicate=true;
            if(duplicate)continue;
            try {
                auto source=assets_->readScript(entry.path);
                entry.console=std::make_unique<LuaConsole>();
                entry.console->bindWorld(*world_,meshes_[0],meshes_[1],meshes_[2],meshes_[3]);
                if(entry.console->bindObject(entry.entity))entry.console->execute(source);
            } catch(const std::exception& error) {entry.error=error.what();}
            entries_.push_back(std::move(entry));
        }
    }
};
