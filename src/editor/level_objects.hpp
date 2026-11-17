#pragma once

#include "assets/assets.hpp"
#include "core/components.hpp"
#include "core/ecs.hpp"
#include "core/hierarchy.hpp"
#include "editor/editor_geometry.hpp"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

struct LevelObject {
    Entity entity;
    std::string name;
    bool hasCollider;
};

inline std::vector<LevelObject> levelObjects(World& world) {
    std::vector<LevelObject> objects;
    world.each<Transform>([&](Entity e, Transform&) {
        const auto* name = world.tryGet<ObjectName>(e);
        objects.push_back(
            {e, name ? name->value : "Object " + std::to_string(e.index), world.has<Collider>(e)});
    });
    std::sort(objects.begin(), objects.end(), [](const LevelObject& a, const LevelObject& b) {
        return a.entity.index < b.entity.index;
    });
    return objects;
}

inline std::string copiedObjectName(World& world, const std::string& original) {
    std::string base = original;
    std::uint64_t next = 2;
    const auto start = original.rfind(" (");
    if (start != std::string::npos && original.back() == ')') {
        std::uint64_t number = 0;
        const char* first = original.data() + start + 2;
        const char* last = original.data() + original.size() - 1;
        const auto parsed = std::from_chars(first, last, number);
        if (parsed.ec == std::errc{} && parsed.ptr == last && number >= 1 &&
            number < std::numeric_limits<std::uint64_t>::max()) {
            base = original.substr(0, start);
            next = number + 1;
        }
    }
    std::unordered_set<std::string> used;
    world.each<ObjectName>([&](Entity, ObjectName& name) { used.insert(name.value); });
    for (;;) {
        std::string candidate = base + " (" + std::to_string(next) + ")";
        if (!used.count(candidate)) {
            return candidate;
        }
        if (next == std::numeric_limits<std::uint64_t>::max()) {
            base = original;
            next = 2;
        } else {
            ++next;
        }
    }
}

// Copy values before adding components: pool growth can invalidate references.
inline Entity
copyLevelObject(World& sourceWorld, Entity source, World& world, bool preserveParent = false) {
    if (!sourceWorld.alive(source)) {
        return {};
    }
    const Entity copy = world.create();
    auto component = [&](auto value) {
        using T = decltype(value);
        if (auto* original = sourceWorld.tryGet<T>(source)) {
            T data = *original;
            if constexpr (std::is_base_of_v<LightSettings, T>) {
                data.bake.clear();
            }
            world.add(copy, data);
        }
    };
    component(Transform{});
    component(MeshRenderer{});
    component(Collider{});
    component(Skybox{});
    component(ObjectActivity{});
    component(PrefabInstance{});
    component(DirectionalLight{});
    component(PointLight{});
    component(SpotLight{});
    component(AudioListener{});
    component(AudioSource{});
    component(RigidBody{});
    component(ObjectName{});
    component(LuaScript{});
    component(Camera{});
    component(CharacterController{});
    component(FPSCamera{});
    component(Raycast{});
    component(Animation{});
    component(ParticleSystem{});
    component(LightShafts{});
    component(CameraEffects{});
    if (const auto* terrain = sourceWorld.tryGet<Terrain>(source)) {
        world.add(copy, *terrain);
    }
    if (const auto* original = sourceWorld.tryGet<Laser>(source)) {
        Laser laser;
        static_cast<LaserSettings&>(laser) = *original;
        world.add(copy, laser);
    }
    component(ObjectLayer{});
    component(InspectorOrder{});
    if (auto* animation = world.tryGet<Animation>(copy)) {
        *animation = Animation{animation->clip, animation->loop, animation->playOnStart};
    }
    if (auto* particles = world.tryGet<ParticleSystem>(copy)) {
        particles->stop();
        particles->started = false;
        particles->time = 0;
    }
    if (auto* audio = world.tryGet<AudioSource>(copy)) {
        audio->stop();
    }
    if (auto* camera = world.tryGet<Camera>(copy)) {
        camera->active = false;
    }
    if (preserveParent) {
        component(Parent{});
    }
    if (auto* name = world.tryGet<ObjectName>(copy)) {
        name->value = copiedObjectName(world, name->value);
    }
    return copy;
}
// Snapshot descendants before creating anything (source and destination may be identical).
inline Entity
copyLevelHierarchy(World& sourceWorld, Entity source, World& world, bool preserveParent = false) {
    if (!sourceWorld.alive(source)) {
        return {};
    }
    std::vector<Entity> originals{source}, copies;
    const auto entities = sourceWorld.entities();
    for (std::size_t i = 0; i < originals.size(); ++i) {
        for (auto e : entities) {
            if (getParent(sourceWorld, e) == originals[i] &&
                std::find(originals.begin(), originals.end(), e) == originals.end()) {
                originals.push_back(e);
            }
        }
    }
    for (auto e : originals) {
        copies.push_back(copyLevelObject(sourceWorld, e, world));
    }
    auto remap = [&](Entity e) {
        auto it = std::find(originals.begin(), originals.end(), e);
        return it == originals.end() ? (&sourceWorld == &world ? e : Entity{})
                                     : copies[std::size_t(it - originals.begin())];
    };
    for (std::size_t i = 0; i < originals.size(); ++i) {
        if (i > 0) {
            if (const auto* name = sourceWorld.tryGet<ObjectName>(originals[i])) {
                const auto value = *name;
                world.add(copies[i], value);
            }
        }
        const auto parent = getParent(sourceWorld, originals[i]);
        if (i || preserveParent) {
            setParent(world, copies[i], remap(parent));
        }
        if (auto* camera = world.tryGet<Camera>(copies[i])) {
            camera->targetEntity = remap(camera->targetEntity);
        }
        if (auto* scripts = world.tryGet<LuaScript>(copies[i])) {
            auto fields = [&](auto& entries) {
                for (auto& f : entries) {
                    f.entity = remap(f.entity);
                }
            };
            fields(scripts->fields);
            for (auto& extra : scripts->additional) {
                fields(extra.fields);
            }
        }
    }
    return copies.front();
}
inline Entity duplicateLevelObject(World& world, Entity source) {
    return copyLevelHierarchy(world, source, world, true);
}
// Clipboard owns a detached copy, not a live reference to the source entity.
class ObjectClipboard {
  public:
    bool available() const {
        return data_ != nullptr;
    }
    void copy(World& world, Entity source) {
        if (!world.alive(source)) {
            return;
        }
        data_ = std::make_unique<World>();
        entity_ = copyLevelHierarchy(world, source, *data_);
        // Copy suffix is applied when pasting, not while storing the clipboard.
        if (auto* name = world.tryGet<ObjectName>(source)) {
            data_->add(entity_, *name);
        }
    }
    Entity paste(World& world) {
        return data_ ? copyLevelHierarchy(*data_, entity_, world) : Entity{};
    }

  private:
    std::unique_ptr<World> data_;
    Entity entity_;
};
inline void deleteLevelObject(World& world, Entity object) {
    destroyHierarchy(world, object);
}

// Primary selection drives the Inspector; the remaining rows are a batch selection.
inline std::vector<Entity> hierarchySelection;
inline Entity hierarchyPrimary;
inline void syncHierarchySelection(World& world, Entity primary) {
    hierarchySelection.erase(std::remove_if(hierarchySelection.begin(),
                                            hierarchySelection.end(),
                                            [&](Entity e) { return !world.alive(e); }),
                             hierarchySelection.end());
    if (primary != hierarchyPrimary) {
        hierarchySelection.clear();
        if (world.alive(primary)) {
            hierarchySelection.push_back(primary);
        }
    }
    hierarchyPrimary = primary;
}
inline std::vector<Entity> hierarchyBatch(World& world, Entity fallback) {
    if (std::find(hierarchySelection.begin(), hierarchySelection.end(), fallback) ==
        hierarchySelection.end()) {
        return {fallback};
    }
    std::vector<Entity> roots;
    for (auto e : hierarchySelection) {
        bool nested = false;
        for (auto parent = getParent(world, e); parent != Entity{};
             parent = getParent(world, parent)) {
            if (std::find(hierarchySelection.begin(), hierarchySelection.end(), parent) !=
                hierarchySelection.end()) {
                nested = true;
                break;
            }
        }
        if (!nested && world.alive(e)) {
            roots.push_back(e);
        }
    }
    return roots;
}
inline void deleteHierarchySelection(World& world, Entity primary) {
    for (auto e : hierarchyBatch(world, primary)) {
        deleteLevelObject(world, e);
    }
    hierarchySelection.clear();
    hierarchyPrimary = {};
}

// Search includes ancestors but never mutates the ECS hierarchy.
inline std::unordered_set<std::uint64_t>
hierarchyMatches(World& world, const std::vector<LevelObject>& objects, std::string query) {
    auto lower = [](std::string text) {
        for (char& c : text) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return text;
    };
    query = lower(query);
    std::unordered_set<std::uint64_t> visible;
    for (const auto& object : objects) {
        if (query.empty() || lower(object.name).find(query) != std::string::npos) {
            Entity e = object.entity;
            for (std::size_t depth = 0; depth <= objects.size() && world.alive(e); ++depth) {
                const auto key = (std::uint64_t(e.generation) << 32) | e.index;
                if (!visible.insert(key).second) {
                    break;
                }
                e = getParent(world, e);
            }
        }
    }
    return visible;
}
inline std::uint64_t hierarchyKey(Entity e) {
    return (std::uint64_t(e.generation) << 32) | e.index;
}
