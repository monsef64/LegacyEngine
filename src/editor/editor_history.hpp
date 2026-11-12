#pragma once
#include "core/hierarchy.hpp"
#include <deque>
#include <string>
#include <utility>
#include <functional>

// Value-only signature: never compare struct padding or pointers into pools.
inline std::string sceneSignature(World& world) {
    std::string result;
    auto scalar=[&](auto value) { result.append(reinterpret_cast<const char*>(&value),sizeof(value)); };
    auto vec=[&](Vec3 v) { scalar(v.x);scalar(v.y);scalar(v.z); };
    auto transform=[&](const Transform& t) {
        vec(t.position);scalar(t.rotation.x);scalar(t.rotation.y);scalar(t.rotation.z);scalar(t.rotation.w);vec(t.scale);
    };
    auto component=[&](Entity e, auto value, auto write) {
        using T=decltype(value);
        const auto* c=world.tryGet<T>(e);scalar(c!=nullptr);if(c)write(*c);
    };
    auto fields=[&](const std::vector<ScriptField>& entries){scalar(entries.size());for(const auto& f:entries){scalar(f.name.size());result+=f.name;scalar(f.type.size());result+=f.type;scalar(f.text.size());result+=f.text;scalar(f.number);scalar(f.boolean);scalar(f.entity.index);scalar(f.entity.generation);}};
    for(Entity e:world.entities()) {
        scalar(e.index);scalar(e.generation);
        component(e,ObjectName{},[&](const ObjectName& n){scalar(n.value.size());result+=n.value;});
        component(e,LuaScript{},[&](const LuaScript& script){scalar(script.path.size());result+=script.path;scalar(script.enabled);fields(script.fields);scalar(script.additional.size());for(const auto& entry:script.additional){scalar(entry.path.size());result+=entry.path;scalar(entry.enabled);fields(entry.fields);}});
        component(e,AudioListener{},[&](const AudioListener& listener){scalar(listener.enabled);});
        component(e,AudioSource{},[&](const AudioSource& a){scalar(a.clip.size());result+=a.clip;scalar(a.volume);scalar(a.pitch);scalar(a.pan);scalar(a.distance);scalar(a.blend);scalar(a.looping);scalar(a.muted);scalar(a.playOnStart);});
        component(e,Camera{},[&](const Camera& c){
            scalar(c.active);scalar(c.orthographic);scalar(c.fieldOfView);scalar(c.nearClip);scalar(c.farClip);scalar(c.orthographicSize);
            scalar(c.viewportX);scalar(c.viewportY);scalar(c.viewportWidth);scalar(c.viewportHeight);
            scalar(c.targetEntity.index);scalar(c.targetEntity.generation);vec(c.target);vec(c.followOffset);scalar(c.tracking);scalar(c.following);
        });
        component(e,Transform{},transform);
        component(e,MeshRenderer{},[&](const MeshRenderer& m){scalar(m.mesh.id);vec(m.color);scalar(m.checkerboard);scalar(m.specular);});
        component(e,Collider{},[&](const Collider& c){vec(c.size);vec(c.center);scalar(c.friction);scalar(c.bounciness);scalar(c.shape);scalar(c.radius);scalar(c.height);});
        component(e,RigidBody{},[&](const RigidBody& b){scalar(b.mass);scalar(b.useGravity);vec(b.linearVelocity);vec(b.angularVelocity);});
        component(e,Parent{},[&](const Parent& p){scalar(p.entity.index);scalar(p.entity.generation);transform(p.previous);});
    }
    return result;
}

class EditorHistory {
public:
    explicit EditorHistory(World& world, Entity selection={}) { baseline_=capture(world,selection); }
    // Call after scene UI, but only in Edit mode. Active gestures are committed
    // together when the pointer/text widget is released, not once per frame.
    void observe(World& world, Entity selection, bool gestureActive) {
        if (!gestureActive) commit(world,selection);
    }
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    void commit(World& world, Entity selection) {
        auto signature=sceneSignature(world);
        if(signature==baseline_.signature) { baseline_.selection=selection;return; }
        push(undo_,std::move(baseline_));redo_.clear();
        baseline_=capture(world,selection,std::move(signature));
    }
    std::string error;
    bool perform(World& world,Entity selection,std::function<void(bool)> action) {
        commit(world,selection);
        try {action(true);}catch(const std::exception& e){error=e.what();return false;}
        push(undo_,std::move(baseline_));redo_.clear();
        baseline_=capture(world,selection);baseline_.action=std::move(action);error.clear();return true;
    }
    bool undo(World& world, Entity& selection) { return restore(world,selection,undo_,redo_); }
    bool redo(World& world, Entity& selection) { return restore(world,selection,redo_,undo_); }
private:
    struct Entry { std::unique_ptr<World> scene; Entity selection; std::string signature; std::function<void(bool)> action; };
    Entry baseline_;
    std::deque<Entry> undo_,redo_;
    static constexpr std::size_t limit=64;
    static Entry capture(World& world,Entity selection,std::string signature={}) {
        if(signature.empty())signature=sceneSignature(world);
        return {world.snapshot(),selection,std::move(signature),{}};
    }
    static void push(std::deque<Entry>& stack,Entry entry) {
        if(stack.size()==limit)stack.pop_front();
        stack.push_back(std::move(entry));
    }
    bool restore(World& world,Entity& selection,std::deque<Entry>& from,std::deque<Entry>& to) {
        if(from.empty())return false;
        try {
            if(&from==&undo_) {if(baseline_.action)baseline_.action(false);}
            else if(from.back().action)from.back().action(true);
        }catch(const std::exception& e){error=e.what();return false;}
        error.clear();
        push(to,std::move(baseline_));baseline_=std::move(from.back());from.pop_back();
        world.restore(*baseline_.scene);selection=baseline_.selection;
        if(!world.alive(selection))selection={};
        return true;
    }
};
