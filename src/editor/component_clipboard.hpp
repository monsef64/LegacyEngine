#pragma once
#include "core/hierarchy.hpp"
#include <any>

// A baked atlas combines multiple lights, so a mode change must detach that
// atlas as a whole rather than leave stale light plus realtime light doubled.
inline void detachLightBake(World& world, const LightSettings& light) {
    if (light.bake.empty()) {
        return;
    }
    world.each<MeshRenderer>([&](Entity, MeshRenderer& mesh) {
        const auto manifest =
            (std::filesystem::path(mesh.lightmap).parent_path() / "bake.json").generic_string();
        if (!mesh.lightmap.empty() && (light.bake == "@legacy" || manifest == light.bake)) {
            mesh.lightmap.clear();
            mesh.lightmapGrid = 0;
        }
    });
}

// One owned value snapshot, not a pointer to the source component. Copying a new
// type replaces the previous clipboard; only exactly matching types can paste.
class ComponentClipboard {
  public:
    template <class T> void copy(World& world, Entity entity) {
        if constexpr (std::is_same_v<T, Transform>) {
            value_ = localTransform(world, entity);
        } else if constexpr (std::is_same_v<T, Animator>) {
            const auto& source = world.get<Animator>(entity);
            Animator copy;
            copy.controller = source.controller;
            copy.enabled = source.enabled;
            copy.playOnStart = source.playOnStart;
            value_ = std::move(copy);
        } else if constexpr (std::is_same_v<T, Laser>) {
            Laser copy;
            static_cast<LaserSettings&>(copy) = world.get<T>(entity);
            value_ = copy;
        } else if constexpr (std::is_same_v<T, ParticleSystem>) {
            ParticleSystem copy;
            static_cast<ParticleSettings&>(copy) = world.get<T>(entity);
            value_ = std::move(copy);
        } else {
            value_ = world.get<T>(entity);
        }
    }
    template <class T> bool matches() const {
        return std::any_cast<T>(&value_) != nullptr;
    }
    template <class T> bool paste(World& world, Entity entity) const {
        const auto* value = std::any_cast<T>(&value_);
        if (!value || !world.alive(entity) || !world.has<T>(entity)) {
            return false;
        }
        if constexpr (std::is_same_v<T, Transform>) {
            setLocalTransform(world, entity, *value);
        } else {
            if constexpr (std::is_base_of_v<LightSettings, T>) {
                detachLightBake(world, world.get<T>(entity));
            }
            world.get<T>(entity) = *value;
            if constexpr (std::is_base_of_v<LightSettings, T>) {
                world.get<T>(entity).bake.clear();
            }
            if constexpr (std::is_same_v<T, Laser>) {
                auto& l = world.get<T>(entity);
                l.ready = false;
                l.hit = false;
                l.end = {};
                l.hitNormal = {0, 1, 0};
            }
            if constexpr (std::is_same_v<T, ParticleSystem>) {
                auto& p = world.get<T>(entity);
                p.stop();
                p.started = false;
                p.time = 0;
            }
        }
        return true;
    }

  private:
    std::any value_;
};

inline ComponentClipboard componentClipboard;
