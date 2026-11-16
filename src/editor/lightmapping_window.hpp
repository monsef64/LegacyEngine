#pragma once
#include "core/worker_pool.hpp"
#include "editor/editor_history.hpp"
#include "lighting/lightmap_baker.hpp"
#include <imgui.h>

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
    void draw(World& world,
              Entity selection,
              const AssetManager& assets,
              const ProjectIO::Meshes& meshes,
              EditorHistory& history,
              bool editAllowed,
              const std::string& level) {
        if (progress_ && (assets.root != root_ || level != level_)) {
            progress_->cancel = true;
        }
        // Pump even while hidden. Never publish a result into a changed scene/project.
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
                        auto& r = world.get<MeshRenderer>(assignment.entity);
                        r.lightmap = assignment.path;
                        r.lightmapGrid = assignment.grid;
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
        if (!visible) {
            return;
        }
        ImGui::SetNextWindowSize(ImVec2(520, 430), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Lighting", &visible)) {
            ImGui::End();
            return;
        }
        ImGui::TextWrapped("Level: %s", level.empty() ? "Unsaved level" : level.c_str());
        const auto staticState = Lightmapping::staticState(world, assets);
        std::vector<Lightmapping::Object> objects;
        int total = 0, baked = 0, assigned = 0;
        world.eachReadOnly<Transform, MeshRenderer>(
            [&](Entity e, const Transform& t, const MeshRenderer& r) {
                ++total;
                if (eligible(world, e, t, r, meshes)) {
                    objects.push_back({e, t, r});
                    if (!r.lightmap.empty()) {
                        ++assigned;
                    }
                    if (validation_.valid(assets, r, staticState)) {
                        ++baked;
                    }
                }
            });
        std::vector<Lightmapping::Light> lights;
        auto collect = [&](int type, Entity e, const LightSettings& l, const Transform& t) {
            if (t.isStatic && isActive(world, e) && l.enabled && l.intensity > 0 && validLight(l)) {
                Lightmapping::Light light{
                    l, t.position, rotateVector(normalize(t.rotation), {0, 0, -1}), type};
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
        ImGui::Text("Objects: %d   Eligible: %zu   Baked: %d", total, objects.size(), baked);
        ImGui::Text("Static lights: %zu", lights.size());
        if (assigned > baked) {
            ImGui::TextWrapped("%d stale lightmaps — rebake required.", assigned - baked);
        }
        if (ImGui::TreeNode("Eligible objects")) {
            for (const auto& object : objects) {
                const auto* name = world.tryGet<ObjectName>(object.entity);
                ImGui::Text("%s: %s",
                            name ? name->value.c_str() : "Object",
                            validation_.valid(assets, object.renderer, staticState)
                                ? "Valid bake"
                                : "Not baked / stale: rebake");
            }
            ImGui::TreePop();
        }
        ImGui::Separator();
        const bool busy = future_.valid();
        ImGui::BeginDisabled(busy || !editAllowed);
        ImGui::Combo("Lightmap quality",
                     &quality_,
                     "Draft (8 texels/metre)\0Medium (16 texels/metre)\0High (32 texels/metre)\0");
        settings_.tile = 8 << quality_;
        ImGui::DragFloat("Shadow ray bias",
                         &settings_.bias,
                         .0001f,
                         .0001f,
                         .1f,
                         "%.4f",
                         ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderInt("Indirect bounces", &settings_.bounces, 0, 4);
        ImGui::SliderInt("GI samples / texel", &settings_.samples, 4, 256);
        ImGui::DragFloat("GI ray distance",
                         &settings_.maxDistance,
                         1.f,
                         1.f,
                         1000.f,
                         "%.1f m",
                         ImGuiSliderFlags_AlwaysClamp);
        ImGui::Checkbox("Denoise indirect lighting", &settings_.denoise);
        if (ImGui::Button("Bake level") && !objects.empty() && !lights.empty() &&
            !assets.root.empty()) {
            try {
                auto progress = std::make_shared<Lightmapping::Progress>();
                const auto signature = sceneSignature(world);
                auto request = WorkerPool::shared().submit([objects = std::move(objects),
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
                } else {
                    progress_ = std::move(progress);
                    future_ = std::move(*request);
                    signature_ = signature;
                    bakingState_ = staticState;
                    root_ = assets.root;
                    level_ = level;
                    revision_ = modelAssetRevision;
                    started_ = std::chrono::steady_clock::now();
                    status_.clear();
                }
            } catch (const std::exception& error) {
                status_ = error.what();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear level assignments")) {
            history.commit(world, selection);
            world.each<MeshRenderer>([](Entity, MeshRenderer& r) {
                r.lightmap.clear();
                r.lightmapGrid = 0;
            });
            history.commit(world, selection);
            status_ =
                "Assignments cleared. Existing atlas files remain for undo and other saved levels.";
        }
        ImGui::EndDisabled();
        if (busy && progress_) {
            const auto count = progress_->total.load(), done = progress_->done.load();
            ImGui::TextUnformatted(progress_->phase == 0 ? "Preparing bake..."
                                                         : "Baking lighting...");
            ImGui::ProgressBar(count ? float(done) / float(count) : 0.f, ImVec2(-1, 0));
            ImGui::Text(
                "Texels: %zu / %zu   Elapsed: %.1f s",
                done,
                count,
                std::chrono::duration<float>(std::chrono::steady_clock::now() - started_).count());
            if (ImGui::Button("Cancel bake")) {
                progress_->cancel = true;
            }
        }
        if (objects.empty() || lights.empty()) {
            ImGui::TextWrapped("Mark at least one mesh and light Static to bake.");
        }
        if (!status_.empty()) {
            ImGui::TextWrapped("%s", status_.c_str());
        }
        ImGui::End();
    }
};
