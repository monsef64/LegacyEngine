#pragma once
#include "core/hierarchy.hpp"
#include <any>

// One owned value snapshot, not a pointer to the source component. Copying a new
// type replaces the previous clipboard; only exactly matching types can paste.
class ComponentClipboard {
  public:
    template <class T> void copy(World& world, Entity entity) {
        if constexpr (std::is_same_v<T, Transform>) {
            value_ = localTransform(world, entity);
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
            world.get<T>(entity) = *value;
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
