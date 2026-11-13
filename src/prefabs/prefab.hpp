#pragma once
#include "project/project_io.hpp"
#include <map>
namespace Prefabs {
inline void updateDestroyTimers(World& world, float dt) {
    if (!std::isfinite(dt) || dt < 0) {
        return;
    }
    std::vector<Entity> expired;
    world.each<DestroyAfter>([&](Entity e, DestroyAfter& timer) {
        timer.seconds -= dt;
        if (timer.seconds <= 0) {
            expired.push_back(e);
        }
    });
    for (auto e : expired) {
        if (world.alive(e)) {
            // Destruction is observed by script runtimes on their next update.
            destroyHierarchy(world, e);
        }
    }
}

inline bool asset(const std::string& path) {
    return materialPathValid(path) && AssetManager::extension(path) == ".prefab";
}
inline std::vector<Entity> tree(World& world, Entity root) {
    if (!world.alive(root)) {
        throw std::runtime_error("Prefab root no longer exists");
    }
    std::vector<Entity> result{root};
    const auto all = world.entities();
    for (std::size_t i = 0; i < result.size(); ++i) {
        for (auto e : all) {
            if (getParent(world, e) == result[i]) {
                if (std::find(result.begin(), result.end(), e) != result.end()) {
                    throw std::runtime_error("Cyclic prefab hierarchy");
                }
                result.push_back(e);
            }
        }
    }
    return result;
}
inline void eraseTree(World& world, Entity root, bool keepRoot = false) {
    auto nodes = tree(world, root);
    for (auto it = nodes.rbegin(); it != nodes.rend(); ++it) {
        if (!keepRoot || *it != root) {
            world.destroy(*it);
        }
    }
}
inline Entity clone(World& source, Entity root, World& destination, Entity existing = {}) {
    const auto nodes = tree(source, root);
    if (destination.entities().size() + nodes.size() > ProjectIO::maxObjects) {
        throw std::runtime_error("Prefab exceeds scene object limit");
    }
    std::map<std::pair<unsigned, unsigned>, Entity> mapping;
    auto key = [](Entity e) { return std::make_pair(e.index, e.generation); };
    for (auto e : nodes) {
        mapping[key(e)] =
            (e == root && destination.alive(existing)) ? existing : destination.create();
    }
    auto remap = [&](Entity e) {
        auto found = mapping.find(key(e));
        return found == mapping.end() ? Entity{} : found->second;
    };
    for (auto e : nodes) {
        auto target = remap(e);
        destination.copyEntityFrom(source, e, target);
        destination.remove<PrefabInstance>(target);
        destination.remove<DestroyAfter>(target);
        if (auto* p = destination.tryGet<Parent>(target)) {
            p->entity = remap(p->entity);
            if (p->entity == Entity{}) {
                destination.remove<Parent>(target);
            }
        }
        if (auto* c = destination.tryGet<Camera>(target)) {
            c->targetEntity = remap(c->targetEntity);
        }
        if (auto* a = destination.tryGet<AudioSource>(target)) {
            a->stop();
            a->pendingEvents.clear();
            a->eventSequence = {};
        }
        if (auto* scripts = destination.tryGet<LuaScript>(target)) {
            auto fields = [&](auto& fs) {
                for (auto& f : fs) {
                    if (f.entity != Entity{}) {
                        f.entity = remap(f.entity);
                    }
                }
            };
            fields(scripts->fields);
            for (auto& extra : scripts->additional) {
                fields(extra.fields);
            }
        }
    }
    return remap(root);
}
inline Entity root(World& world) {
    Entity result;
    unsigned count = 0;
    for (auto e : world.entities()) {
        if (getParent(world, e) == Entity{}) {
            result = e;
            ++count;
        }
    }
    if (count != 1 || !world.has<Transform>(result)) {
        throw std::runtime_error("Prefab must have exactly one Transform root");
    }
    return result;
}
inline std::unique_ptr<World>
load(AssetManager& assets, const std::string& path, const ProjectIO::Meshes& meshes) {
    if (!asset(path)) {
        throw std::runtime_error("Expected project-relative .prefab asset");
    }
    ProjectIO::Loaded result;
    const auto data = readJsonFile(assets.resolve(path), ProjectIO::maxBytes);
    if (data.at("format") != "LegacyEnginePrefab" || data.at("version") != 1) {
        throw std::runtime_error("Invalid prefab asset");
    }
    ProjectIO::decode(data.at("scene").dump(), result, meshes);
    root(result.world);
    return result.world.snapshot();
}
inline void save(AssetManager& assets,
                 const std::string& path,
                 World& world,
                 Entity entity,
                 const ProjectIO::Meshes& meshes) {
    if (!asset(path)) {
        throw std::runtime_error("Expected project-relative .prefab asset");
    }
    World isolated;
    clone(world, entity, isolated);
    root(isolated);
    auto scene = parseJson(ProjectIO::encode(isolated, "", OrbitCamera{}, meshes));
    writeJsonFile(assets.resolve(path),
                  Json{{"format", "LegacyEnginePrefab"}, {"version", 1}, {"scene", scene}});
}
inline Entity spawn(AssetManager& assets,
                    const std::string& path,
                    World& world,
                    const ProjectIO::Meshes& meshes,
                    Vec3 position,
                    Quat rotation) {
    auto loaded = load(assets, path, meshes);
    auto source = root(*loaded);
    auto& transform = loaded->get<Transform>(source);
    transform.position = position;
    transform.rotation = rotation;
    updateHierarchy(*loaded);
    auto result = clone(*loaded, source, world);
    world.add(result, PrefabInstance{path});
    return result;
}
inline void refresh(AssetManager& assets,
                    const std::string& path,
                    World& world,
                    const ProjectIO::Meshes& meshes) {
    auto loaded = load(assets, path, meshes);
    const auto source = root(*loaded);
    std::vector<Entity> instances;
    world.each<PrefabInstance>([&](Entity e, const PrefabInstance& link) {
        if (link.asset == path) {
            instances.push_back(e);
        }
    });
    for (auto instance : instances) {
        if (!world.alive(instance)) {
            continue;
        }
        const auto pose = world.get<Transform>(instance);
        const auto parent = getParent(world, instance);
        auto prepared = loaded->snapshot();
        auto& transform = prepared->get<Transform>(source);
        transform = pose;
        updateHierarchy(*prepared);
        eraseTree(world, instance, true);
        clone(*prepared, source, world, instance);
        if (parent != Entity{}) {
            setParent(world, instance, parent);
        }
        world.add(instance, PrefabInstance{path});
    }
}
inline void
refreshSavedLevels(AssetManager& assets, const std::string& path, const ProjectIO::Meshes& meshes) {
    // Parse/validate every affected document before touching any saved level.
    std::vector<std::pair<std::filesystem::path, Json>> changed;
    for (const auto& file : assets.files()) {
        if (AssetManager::extension(file) == ".level") {
            ProjectIO::Loaded loaded;
            ProjectIO::decode(
                readJsonFile(assets.resolve(file), ProjectIO::maxBytes).dump(), loaded, meshes);
            bool linked = false;
            loaded.world.each<PrefabInstance>(
                [&](Entity, const PrefabInstance& link) { linked |= link.asset == path; });
            if (linked) {
                refresh(assets, path, loaded.world, meshes);
                changed.emplace_back(assets.resolve(file),
                                     parseJson(ProjectIO::encode(
                                         loaded.world, loaded.source, loaded.camera, meshes)));
            }
        }
    }
    for (const auto& [file, document] : changed) {
        writeJsonFile(file, document);
    }
}
inline void refreshAll(AssetManager& assets, World& world, const ProjectIO::Meshes& meshes) {
    std::vector<std::string> paths;
    world.each<PrefabInstance>([&](Entity, const PrefabInstance& link) {
        if (std::find(paths.begin(), paths.end(), link.asset) == paths.end()) {
            paths.push_back(link.asset);
        }
    });
    for (const auto& path : paths) {
        refresh(assets, path, world, meshes);
    }
}

} // namespace Prefabs
