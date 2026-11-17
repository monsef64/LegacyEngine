#pragma once
#include "core/worker_pool.hpp"
#include "editor/editor_history.hpp"
#include "lighting/lightmap_baker.hpp"

class LightmappingWindow {
    Lightmapping::Settings settings_;
    std::shared_ptr<Lightmapping::Progress> progress_;
    std::future<std::shared_ptr<Lightmapping::Result>> future_;
    std::string signature_, status_, level_, bakingState_;
    std::filesystem::path root_;
    std::chrono::steady_clock::time_point started_;
    int quality_ = 1;
    unsigned revision_ = 0;
    Lightmapping::ValidationCache validation_;
    static bool eligible(World& world,
                         Entity e,
                         const Transform& t,
                         const MeshRenderer& r,
                         const ProjectIO::Meshes& meshes) {
        if (!t.isStatic || !isActive(world, e) || world.has<Animation>(e) ||
            world.has<CharacterController>(e) || world.has<Camera>(e)) {
            return false;
        }
        if (auto* body = world.tryGet<RigidBody>(e); body && body->mass > 0) {
            return false;
        }
        if (std::abs(t.scale.x * t.scale.y * t.scale.z) < 1e-12f) {
            return false;
        }
        Entity parent = getParent(world, e);
        for (unsigned depth = 0; world.alive(parent); ++depth, parent = getParent(world, parent)) {
            if (depth >= 128 || world.has<Animation>(parent)) {
                return false;
            }
            if (auto* body = world.tryGet<RigidBody>(parent); body && body->mass > 0) {
                return false;
            }
        }
        if (!r.model.empty()) {
            return true;
        }
        for (int i = 1; i < 6; ++i) {
            if (r.mesh.id == meshes[i].id) {
                return true;
            }
        }
        return false;
    }

  public:
    bool visible = false;
    void cancelBake() {
        if (progress_) {
            progress_->cancel = true;
        }
    }
    ~LightmappingWindow() {
        if (progress_) {
            progress_->cancel = true;
        }
    }
    // Pump the background bake without drawing anything. The Qt Lighting dock
    // calls this every frame and then reads the state below.
    void update(World& world,
                Entity selection,
                const AssetManager& assets,
                const ProjectIO::Meshes& meshes,
                EditorHistory& history,
                bool editAllowed,
                const std::string& level) {
        if (progress_ && (assets.root != root_ || level != level_)) {
            progress_->cancel = true;
        }
        if (future_.valid() &&
            future_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            try {
                auto result = future_.get();
                if (progress_->cancel || !editAllowed || assets.root != root_ ||
                    modelAssetRevision != revision_ || level != level_ ||
                    sceneSignature(world) != signature_ ||
                    Lightmapping::staticState(world, assets) != bakingState_) {
                    status_ =
                        "Bake discarded: scene/project changed or bake cancelled. Bake again in Edit mode.";
                } else {
                    history.commit(world, selection);
                    for (const auto& assignment : result->assignments) {
                        if (auto* terrain = world.tryGet<Terrain>(assignment.entity)) {
                            if (!terrain->data.unique()) {
                                terrain->data = std::make_shared<Terrains::Data>(*terrain->data);
                            }
                            terrain->data->lightmap = assignment.path;
                            terrain->data->revision = Terrains::nextRevision();
                        } else {
                            auto& r = world.get<MeshRenderer>(assignment.entity);
                            r.lightmap = assignment.path;
                            r.lightmapGrid = assignment.grid;
                        }
                    }
                    const auto manifest =
                        (result->directory / "bake.json")
                            .lexically_relative(std::filesystem::weakly_canonical(assets.root))
                            .generic_string();
                    for (const auto& [entity, type] : result->lights) {
                        if (type == 0) {
                            world.get<DirectionalLight>(entity).bake = manifest;
                        }
                        if (type == 1) {
                            world.get<PointLight>(entity).bake = manifest;
                        }
                        if (type == 2) {
                            world.get<SpotLight>(entity).bake = manifest;
                        }
                    }
                    result->keep = true;
                    history.commit(world, selection);
                    status_ = "Bake complete. Save the level to persist its lightmap assignments.";
                }
            } catch (const std::exception& error) {
                status_ = error.what();
            }
            progress_.reset();
        }
    }

    struct Snapshot {
        int totalObjects = 0, baked = 0, assigned = 0;
        std::size_t eligibleObjects = 0, staticLights = 0;
        bool busy = false;
        float progress = 0.0f;
        std::size_t texelsDone = 0, texelsTotal = 0;
        double elapsed = 0.0;
        int phase = 0;
        std::string status;
        std::string level;
        std::vector<std::pair<std::string, bool>> objects;
    };

    Snapshot snapshot(World& world,
                      const AssetManager& assets,
                      const ProjectIO::Meshes& meshes,
                      const std::string& level) {
        Snapshot out;
        out.level = level.empty() ? std::string("Unsaved level") : level;
        out.status = status_;
        const auto staticState = Lightmapping::staticState(world, assets);
        std::vector<Lightmapping::Object> objects;
        world.eachReadOnly<Transform, MeshRenderer>(
            [&](Entity e, const Transform& t, const MeshRenderer& r) {
                if (world.has<Terrain>(e)) {
                    return;
                }
                ++out.totalObjects;
                if (eligible(world, e, t, r, meshes)) {
                    objects.push_back({e, t, r});
                    if (!r.lightmap.empty()) {
                        ++out.assigned;
                    }
                    if (validation_.valid(assets, r, staticState)) {
                        ++out.baked;
                    }
                }
            });
        world.eachReadOnly<Transform, Terrain>(
            [&](Entity e, const Transform& t, const Terrain& terrain) {
                ++out.totalObjects;
                MeshRenderer eligibility;
                eligibility.mesh = meshes[1];
                if (!terrain.data || !eligible(world, e, t, eligibility, meshes)) {
                    return;
                }
                MeshRenderer r;
                r.lightmap = terrain.data->lightmap;
                r.lightmapGrid = 1;
                out.assigned += r.lightmap.empty() ? 0 : 1;
                out.baked += validation_.valid(assets, r, staticState) ? 1 : 0;
            });
        std::vector<Lightmapping::Light> lights;
        auto collect = [&](int type, Entity e, const LightSettings& l, const Transform& t) {
            if (l.mode == 1 && isActive(world, e) && l.enabled && l.intensity > 0 &&
                validLight(l)) {
                Lightmapping::Light light{
                    l, t.position, rotateVector(normalize(t.rotation), {0, 0, -1}), type};
                light.entity = e;
                light.right = rotateVector(normalize(t.rotation), {1, 0, 0});
                light.up = rotateVector(normalize(t.rotation), {0, 1, 0});
                lights.push_back(std::move(light));
            }
        };
        world.eachReadOnly<DirectionalLight, Transform>(
            [&](Entity e, const DirectionalLight& l, const Transform& t) { collect(0, e, l, t); });
        world.eachReadOnly<PointLight, Transform>(
            [&](Entity e, const PointLight& l, const Transform& t) { collect(1, e, l, t); });
        world.eachReadOnly<SpotLight, Transform>(
            [&](Entity e, const SpotLight& l, const Transform& t) { collect(2, e, l, t); });
        out.eligibleObjects = objects.size();
        out.staticLights = lights.size();
        for (const auto& object : objects) {
            const auto* name = world.tryGet<ObjectName>(object.entity);
            out.objects.emplace_back(name ? name->value : std::string("Object"),
                                     validation_.valid(assets, object.renderer, staticState));
        }
        if (future_.valid() && progress_) {
            out.busy = true;
            out.progress = progress_->total.load() ? float(progress_->done.load()) /
                                                         float(progress_->total.load())
                                                   : 0.0f;
            out.texelsDone = progress_->done.load();
            out.texelsTotal = progress_->total.load();
            out.phase = progress_->phase;
            out.elapsed = std::chrono::duration<float>(std::chrono::steady_clock::now() - started_)
                              .count();
        }
        return out;
    }

    void setQuality(int quality) {
        quality_ = std::clamp(quality, 0, 2);
        settings_.tile = 8 << quality_;
    }
    int quality() const {
        return quality_;
    }
    Lightmapping::Settings& settings() {
        return settings_;
    }

    bool bake(World& world,
              const AssetManager& assets,
              const ProjectIO::Meshes& meshes,
              const std::string& level) {
        const auto staticState = Lightmapping::staticState(world, assets);
        std::vector<Lightmapping::Object> objects;
        world.eachReadOnly<Transform, MeshRenderer>(
            [&](Entity e, const Transform& t, const MeshRenderer& r) {
                if (!world.has<Terrain>(e) && eligible(world, e, t, r, meshes)) {
                    objects.push_back({e, t, r});
                }
            });
        world.eachReadOnly<Transform, Terrain>(
            [&](Entity e, const Transform& t, const Terrain& terrain) {
                MeshRenderer eligibility;
                eligibility.mesh = meshes[1];
                if (!terrain.data || !eligible(world, e, t, eligibility, meshes)) {
                    return;
                }
                MeshRenderer r;
                r.lightmap = terrain.data->lightmap;
                r.lightmapGrid = 1;
                objects.push_back(Lightmapping::Object{e, t, r, terrain.data});
            });
        std::vector<Lightmapping::Light> lights;
        auto collect = [&](int type, Entity e, const LightSettings& l, const Transform& t) {
            if (l.mode == 1 && isActive(world, e) && l.enabled && l.intensity > 0 &&
                validLight(l)) {
                Lightmapping::Light light{
                    l, t.position, rotateVector(normalize(t.rotation), {0, 0, -1}), type};
                light.entity = e;
                light.right = rotateVector(normalize(t.rotation), {1, 0, 0});
                light.up = rotateVector(normalize(t.rotation), {0, 1, 0});
                lights.push_back(std::move(light));
            }
        };
        world.eachReadOnly<DirectionalLight, Transform>(
            [&](Entity e, const DirectionalLight& l, const Transform& t) { collect(0, e, l, t); });
        world.eachReadOnly<PointLight, Transform>(
            [&](Entity e, const PointLight& l, const Transform& t) { collect(1, e, l, t); });
        world.eachReadOnly<SpotLight, Transform>(
            [&](Entity e, const SpotLight& l, const Transform& t) { collect(2, e, l, t); });
        settings_.bounces = 0;
        if (objects.empty() || lights.empty() || assets.root.empty()) {
            status_ = "Mark at least one mesh and light Static to bake.";
            return false;
        }
        try {
            auto progress = std::make_shared<Lightmapping::Progress>();
            const auto signature = sceneSignature(world);
            auto request = WorkerPool::shared().submit(
                [objects = std::move(objects),
                 lights = std::move(lights),
                 assets,
                 meshes,
                 settings = settings_,
                 progress,
                 staticState] {
                    return Lightmapping::bake(
                        objects, lights, assets, meshes, settings, progress, staticState);
                });
            if (!request) {
                status_ = "Background queue full; try again.";
                return false;
            }
            progress_ = std::move(progress);
            future_ = std::move(*request);
            signature_ = signature;
            bakingState_ = staticState;
            root_ = assets.root;
            level_ = level;
            revision_ = modelAssetRevision;
            started_ = std::chrono::steady_clock::now();
            status_.clear();
            return true;
        } catch (const std::exception& error) {
            status_ = error.what();
            return false;
        }
    }

    void clearAssignments(World& world, Entity selection, EditorHistory& history) {
        history.commit(world, selection);
        world.each<MeshRenderer>([](Entity, MeshRenderer& r) {
            r.lightmap.clear();
            r.lightmapGrid = 0;
        });
        world.each<Terrain>([](Entity, Terrain& t) {
            if (t.data && !t.data->lightmap.empty()) {
                if (!t.data.unique()) {
                    t.data = std::make_shared<Terrains::Data>(*t.data);
                }
                t.data->lightmap.clear();
                t.data->revision = Terrains::nextRevision();
            }
        });
        auto clear = [](Entity, auto& l) { l.bake.clear(); };
        world.each<DirectionalLight>(clear);
        world.each<PointLight>(clear);
        world.each<SpotLight>(clear);
        history.commit(world, selection);
        status_ = "Assignments cleared. Existing atlas files remain for undo and other saved levels.";
    }

};
