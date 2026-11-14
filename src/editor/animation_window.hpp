#pragma once
#include "animation/animation_clip.hpp"
#include "editor/asset_picker.hpp"
#include "editor/editor_history.hpp"
#include "models/model_asset.hpp"
#include "prefabs/prefab.hpp"
#include "rendering/renderer.hpp"
#include "rendering/scene_surface.hpp"
#include "scene/camera.hpp"
#include <cstdio>

// Preview lives in a cloned world: scrubbing does not dirty the edited level,
// execute scripts, step physics, or overwrite the user's pre-animation pose.
class AnimationWindow {
    AnimationClip clip_;
    std::string path_, error_;
    std::filesystem::path root_;
    bool dirty_ = false, playing_ = false, loop_ = false, resetPose_ = false;
    struct DisabledScope {
        explicit DisabledScope(bool disabled = true) {
            ImGui::BeginDisabled(disabled);
        }
        ~DisabledScope() {
            ImGui::EndDisabled();
        }
    };
    struct TableScope {
        ~TableScope() {
            ImGui::EndTable();
        }
    };
    float time_ = 0;
    int track_ = -1, key_ = -1, property_ = 0;
    char newPath_[512] = "Animations/NewAnimation.leanim", target_[1024]{};
    Entity owner_, previewOwner_;
    std::unique_ptr<World> base_, preview_;
    SceneSurface surface_;
    OrbitCamera camera_;
    void rebuild(World& world, Entity owner, const AssetManager& assets, Renderer& renderer) {
        base_ = std::make_unique<World>();
        previewOwner_ = Prefabs::clone(world, owner, *base_);
        owner_ = owner;
        bool any = false;
        Vec3 low{}, high{};
        auto include = [&](Vec3 p) {
            if (!any) {
                low = high = p;
                any = true;
            }
            low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
            high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
        };
        base_->each<Transform, MeshRenderer>(
            [&](Entity, const Transform& pose, const MeshRenderer& mesh) {
                Vec3 a{}, b{};
                bool valid = false;
                if (!mesh.model.empty()) {
                    try {
                        const auto data = loadModelAsset(assets, mesh.model);
                        bool first = true;
                        for (const auto& v : data.vertices) {
                            if (first) {
                                a = b = v.position;
                                first = false;
                            }
                            a = {std::min(a.x, v.position.x),
                                 std::min(a.y, v.position.y),
                                 std::min(a.z, v.position.z)};
                            b = {std::max(b.x, v.position.x),
                                 std::max(b.y, v.position.y),
                                 std::max(b.z, v.position.z)};
                        }
                        valid = !first;
                    } catch (const std::exception&) {
                    }
                } else {
                    valid = renderer.meshBounds(mesh.mesh, a, b);
                }
                if (valid) {
                    for (int i = 0; i < 8; ++i) {
                        include(pose.position +
                                rotateVector(
                                    pose.rotation,
                                    Vec3{i & 1 ? b.x : a.x, i & 2 ? b.y : a.y, i & 4 ? b.z : a.z} *
                                        pose.scale));
                    }
                }
            });
        camera_ = {};
        if (any) {
            camera_.target = (low + high) * .5f;
            const auto span = high - low;
            camera_.distance = std::max(.1f, std::sqrt(dot(span, span)) * 1.5f);
        } else if (base_->has<Transform>(previewOwner_)) {
            camera_.target = base_->get<Transform>(previewOwner_).position;
        }
        const auto sun = base_->create();
        base_->add(sun, Transform{});
        DirectionalLight light;
        light.shadows = false;
        base_->add(sun, light);
        preview_ = base_->snapshot();
    }
    void render(Renderer& renderer, const AssetManager& assets, float width) {
        if (!preview_ || !base_) {
            return;
        }
        preview_->restore(*base_);
        if (!resetPose_) {
            applyAnimationClip(*preview_, previewOwner_, clip_, time_);
        }
        GLint draw = 0, read = 0, viewport[4];
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
        glGetIntegerv(GL_VIEWPORT, viewport);
        auto restore = [&] {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
            glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        };
        try {
            surface_.resize(480, 270);
            glBindFramebuffer(GL_FRAMEBUFFER, surface_.framebuffer);
            renderer.setAssetRoot(assets.root);
            const auto vp = multiply(
                perspective(
                    camera_.fieldOfView, 480.f / 270, .01f, std::max(200.f, camera_.distance * 10)),
                lookAt(camera_.position(), camera_.target, camera_.up()));
            renderer.draw(*preview_, vp, 480, 270, camera_.position(), 0, 0, false);
            restore();
        } catch (...) {
            restore();
            throw;
        }
        ImGui::Image(static_cast<ImTextureID>(surface_.texture),
                     ImVec2(width, width * 270 / 480),
                     ImVec2(0, 1),
                     ImVec2(1, 0));
        if (ImGui::IsItemHovered()) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                camera_.look(ImGui::GetIO().MouseDelta.x, ImGui::GetIO().MouseDelta.y);
            }
            if (ImGui::GetIO().MouseWheel) {
                camera_.zoom(ImGui::GetIO().MouseWheel);
            }
        }
    }

  public:
    bool visible = false;
    void open(const AssetManager& assets, const std::string& path) {
        if (root_ == assets.root && path_ == path) {
            visible = true;
            return;
        }
        if (dirty_) {
            visible = true;
            error_ = "Save or discard the current clip before opening another.";
            return;
        }
        try {
            clip_ = loadAnimationClip(assets, path);
            path_ = path;
            root_ = assets.root;
            track_ = key_ = -1;
            time_ = 0;
            playing_ = false;
            error_.clear();
            visible = true;
        } catch (const std::exception& e) {
            error_ = e.what();
            visible = true;
        }
    }
    void draw(World& world,
              Entity selected,
              const AssetManager& assets,
              Renderer& renderer,
              EditorHistory& history,
              bool editMode,
              float dt) {
        if (!visible) {
            return;
        }
        if (root_ != assets.root) {
            root_ = assets.root;
            path_.clear();
            clip_ = {};
            base_.reset();
            preview_.reset();
            owner_ = {};
            dirty_ = false;
            playing_ = false;
            track_ = key_ = -1;
        }
        ImGui::SetNextWindowSize(ImVec2(900, 720), ImGuiCond_FirstUseEver);
        bool openWindow = true;
        if (ImGui::Begin(dirty_ ? "Animation*###AnimationEditor" : "Animation###AnimationEditor",
                         &openWindow)) {
            try {
                {
                    DisabledScope editDisabled(!editMode);
                    if (ImGui::Button("New clip")) {
                        if (dirty_) {
                            error_ = "Save or discard the current clip first.";
                        } else {
                            clip_ = {};
                            path_.clear();
                            track_ = key_ = -1;
                            time_ = 0;
                            playing_ = false;
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Discard edits")) {
                        if (!path_.empty()) {
                            clip_ = loadAnimationClip(assets, path_);
                        } else {
                            clip_ = {};
                        }
                        dirty_ = false;
                        track_ = key_ = -1;
                        playing_ = false;
                        time_ = 0;
                        error_.clear();
                    }
                    ImGui::InputText("New asset path", newPath_, sizeof(newPath_));
                    if (ImGui::Button(path_.empty() ? "Create asset" : "Save clip")) {
                        auto path = path_.empty() ? std::string(newPath_) : path_;
                        if (!animationAsset(path)) {
                            throw std::runtime_error("Use a project-relative .leanim asset path");
                        }
                        if (path_.empty() && std::filesystem::exists(assets.resolve(path))) {
                            throw std::runtime_error(
                                "Asset already exists; select it in Assets to edit");
                        }
                        std::filesystem::create_directories(assets.resolve(path).parent_path());
                        saveAnimationClip(assets, path, clip_);
                        path_ = path;
                        dirty_ = false;
                        error_.clear();
                    }
                    ImGui::SameLine();
                    ImGui::TextWrapped("%s", path_.empty() ? "Unsaved clip" : path_.c_str());
                    auto nextPath = path_;
                    if (assetPicker("Open animation",
                                    nextPath,
                                    &assets,
                                    [](const auto& p) { return animationAsset(p); }) &&
                        !nextPath.empty()) {
                        open(assets, nextPath);
                    }
                    {
                        DisabledScope assignDisabled(!world.alive(selected) || path_.empty());
                        if (ImGui::Button("Assign clip to selected object")) {
                            history.commit(world, selected);
                            if (!world.has<Animation>(selected)) {
                                world.add(selected, Animation{});
                            }
                            world.get<Animation>(selected).clip = path_;
                            world.get<Animation>(selected).loop = loop_;
                            history.commit(world, selected);
                        }
                    }
                    float duration = clip_.duration;
                    if (ImGui::InputFloat("Duration (seconds)", &duration, .1f, 1, "%.3f") &&
                        std::isfinite(duration)) {
                        float last = .001f;
                        for (const auto& track : clip_.tracks) {
                            if (!track.keys.empty()) {
                                last = std::max(last, track.keys.back().time);
                            }
                        }
                        clip_.duration = std::clamp(duration, last, 86400.f);
                        time_ = std::min(time_, clip_.duration);
                        dirty_ = true;
                    }
                    ImGui::InputText("Target child path (empty = self)", target_, sizeof(target_));
                    const auto* chosen = animationProperty(animationProperties[property_].name);
                    if (ImGui::BeginCombo("Property", chosen->name)) {
                        for (int i = 0;
                             i < int(sizeof(animationProperties) / sizeof(animationProperties[0]));
                             ++i) {
                            if (ImGui::Selectable(animationProperties[i].name, property_ == i)) {
                                property_ = i;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    {
                        DisabledScope trackDisabled(clip_.tracks.size() >= 64);
                        if (ImGui::Button("Add track")) {
                            const std::string target = target_;
                            const std::string property = animationProperties[property_].name;
                            bool duplicate = false;
                            for (const auto& track : clip_.tracks) {
                                duplicate |= track.target == target && track.property == property;
                            }
                            if (duplicate) {
                                throw std::runtime_error(
                                    "That target/property already has a track");
                            }
                            clip_.tracks.push_back({target, property, {}});
                            track_ = int(clip_.tracks.size()) - 1;
                            key_ = -1;
                            dirty_ = true;
                        }
                    }
                    if (ImGui::BeginTable("Tracks",
                                          2,
                                          ImGuiTableFlags_BordersInnerV |
                                              ImGuiTableFlags_Resizable)) {
                        TableScope tableScope;
                        ImGui::TableNextColumn();
                        ImGui::BeginChild("Track list", ImVec2(0, 160), true);
                        for (int i = 0; i < int(clip_.tracks.size()); ++i) {
                            const auto& track = clip_.tracks[i];
                            ImGui::PushID(i);
                            if (ImGui::Selectable(((track.target.empty() ? "Self" : track.target) +
                                                   " / " + track.property)
                                                      .c_str(),
                                                  track_ == i)) {
                                track_ = i;
                                key_ = -1;
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndChild();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(
                            "Linear timeline - click to scrub, click a key to select");
                        const auto pos = ImGui::GetCursorScreenPos();
                        const float width = std::max(40.f, ImGui::GetContentRegionAvail().x);
                        ImGui::InvisibleButton("Timeline", ImVec2(width, 52));
                        auto* draw = ImGui::GetWindowDrawList();
                        draw->AddRectFilled(
                            pos, ImVec2(pos.x + width, pos.y + 52), IM_COL32(30, 35, 43, 255));
                        for (int i = 0; i <= 10; ++i) {
                            const float x = pos.x + width * i / 10;
                            draw->AddLine(
                                ImVec2(x, pos.y), ImVec2(x, pos.y + 52), IM_COL32(65, 70, 80, 255));
                        }
                        if (ImGui::IsItemActive()) {
                            time_ =
                                std::clamp((ImGui::GetIO().MousePos.x - pos.x) / width, 0.f, 1.f) *
                                clip_.duration;
                            playing_ = false;
                        }
                        if (track_ >= 0 && track_ < int(clip_.tracks.size())) {
                            auto& track = clip_.tracks[track_];
                            for (int i = 0; i < int(track.keys.size()); ++i) {
                                const float x = pos.x + track.keys[i].time / clip_.duration * width;
                                draw->AddCircleFilled(ImVec2(x, pos.y + 26),
                                                      5,
                                                      key_ == i ? IM_COL32(255, 210, 40, 255)
                                                                : IM_COL32(80, 180, 255, 255));
                                if (ImGui::IsItemClicked() &&
                                    std::abs(ImGui::GetIO().MousePos.x - x) < 7) {
                                    key_ = i;
                                    time_ = track.keys[i].time;
                                }
                            }
                            if (ImGui::Button("Capture key from selected object")) {
                                const auto target = animationTarget(world, selected, track.target);
                                AnimationValue value{};
                                const auto* property = animationProperty(track.property);
                                if (!readAnimationValue(world, target, *property, value)) {
                                    throw std::runtime_error(
                                        "Selected object's target/component does not exist");
                                }
                                auto it = std::lower_bound(
                                    track.keys.begin(),
                                    track.keys.end(),
                                    time_,
                                    [](const auto& key, float t) { return key.time < t; });
                                if (it != track.keys.end() &&
                                    std::abs(it->time - time_) < .00001f) {
                                    it->value = value;
                                } else {
                                    if (track.keys.size() >= 4096) {
                                        throw std::runtime_error("Track key limit reached");
                                    }
                                    it = track.keys.insert(it, {time_, value});
                                }
                                key_ = int(it - track.keys.begin());
                                dirty_ = true;
                            }
                            if (key_ >= 0 && key_ < int(track.keys.size())) {
                                auto& key = track.keys[key_];
                                const auto* property = animationProperty(track.property);
                                float t = key.time;
                                if (ImGui::DragFloat(
                                        "Key time", &t, .01f, 0, clip_.duration, "%.3f")) {
                                    const float low = key_ > 0 ? track.keys[key_ - 1].time + .0001f
                                                               : 0,
                                                high = key_ + 1 < int(track.keys.size())
                                                           ? track.keys[key_ + 1].time - .0001f
                                                           : clip_.duration;
                                    if (low <= high) {
                                        key.time = std::clamp(t, low, high);
                                        time_ = key.time;
                                        dirty_ = true;
                                    }
                                }
                                if (property->kind == AnimationValueKind::Boolean) {
                                    bool v = key.value[0] >= .5f;
                                    if (ImGui::Checkbox("Value", &v)) {
                                        key.value[0] = v ? 1.f : 0.f;
                                        dirty_ = true;
                                    }
                                } else if (ImGui::DragScalarN("Key value",
                                                              ImGuiDataType_Float,
                                                              key.value.data(),
                                                              property->lanes(),
                                                              .01f,
                                                              &property->minimum,
                                                              &property->maximum,
                                                              "%.3f",
                                                              ImGuiSliderFlags_AlwaysClamp)) {
                                    dirty_ = true;
                                }
                                if (ImGui::Button("Delete key")) {
                                    track.keys.erase(track.keys.begin() + key_);
                                    key_ = -1;
                                    dirty_ = true;
                                }
                            }
                            if (ImGui::Button("Delete track")) {
                                clip_.tracks.erase(clip_.tracks.begin() + track_);
                                track_ = key_ = -1;
                                dirty_ = true;
                            }
                        }
                    }
                }
                if (ImGui::Button(playing_ ? "Pause" : "Play")) {
                    playing_ = !playing_;
                    resetPose_ = false;
                }
                ImGui::SameLine();
                if (ImGui::Button("Stop")) {
                    playing_ = false;
                    time_ = 0;
                    resetPose_ = false;
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset")) {
                    resetPose_ = true;
                    playing_ = false;
                    time_ = 0;
                    if (base_) {
                        preview_ = base_->snapshot();
                    }
                }
                ImGui::SameLine();
                ImGui::Checkbox("Loop preview", &loop_);
                if (playing_) {
                    time_ += std::isfinite(dt) ? std::clamp(dt, 0.f, .25f) : 0;
                    if (time_ >= clip_.duration) {
                        if (loop_) {
                            time_ = std::fmod(time_, clip_.duration);
                        } else {
                            time_ = clip_.duration;
                            playing_ = false;
                        }
                    }
                }
                if (ImGui::SliderFloat("Time", &time_, 0, clip_.duration, "%.3f s")) {
                    resetPose_ = false;
                }
                if (world.alive(selected) && (!base_ || selected != owner_)) {
                    rebuild(world, selected, assets, renderer);
                }
                if (ImGui::Button("Refresh isolated preview") && world.alive(selected)) {
                    rebuild(world, selected, assets, renderer);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("No scripts/physics. Right-drag look, wheel zoom.");
                {
                    DisabledScope applyDisabled(!editMode || !world.alive(selected));
                    if (ImGui::Button("Apply frame to Scene (undoable)")) {
                        history.commit(world, selected);
                        applyAnimationClip(world, selected, clip_, time_);
                        history.commit(world, selected);
                    }
                }
                render(renderer, assets, std::min(480.f, ImGui::GetContentRegionAvail().x));
                if (!error_.empty()) {
                    ImGui::TextWrapped("%s", error_.c_str());
                }
            } catch (const std::exception& e) {
                error_ = e.what();
                ImGui::TextWrapped("%s", error_.c_str());
            }
        }
        ImGui::End();
        if (!openWindow) {
            if (dirty_) {
                error_ = "Clip has unsaved edits. Save or Discard edits before closing.";
            } else {
                visible = false;
            }
        }
    }
};
