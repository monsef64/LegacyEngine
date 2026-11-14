#pragma once
#include "animation/animation_clip.hpp"
#include "editor/asset_picker.hpp"
#include "editor/editor_history.hpp"
#include "models/model_asset.hpp"
#include "prefabs/prefab.hpp"
#include "rendering/renderer.hpp"
#include "rendering/scene_surface.hpp"
#include "scene/camera.hpp"
#include <array>
#include <cstdio>
#include <map>

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
    struct PopupScope {
        ~PopupScope() {
            ImGui::EndPopup();
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
    bool previewDirty_ = true, previewRendered_ = false, lastResetPose_ = false;
    float lastPreviewTime_ = -1;
    std::array<float, 7> lastCamera_{};
    unsigned previewRevision_ = ~0u;
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
                        valid = renderer.modelBounds(assets, mesh.model, a, b);
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
        light.intensity = 1.6f;
        base_->add(sun, light);
        preview_ = base_->snapshot();
        previewDirty_ = true;
        previewRendered_ = false;
    }
    void render(Renderer& renderer, const AssetManager& assets, float width) {
        if (!preview_ || !base_) {
            return;
        }
        const bool poseChanged =
            previewDirty_ || time_ != lastPreviewTime_ || resetPose_ != lastResetPose_;
        if (poseChanged) {
            preview_->restore(*base_);
            if (!resetPose_) {
                applyAnimationClip(*preview_, previewOwner_, clip_, time_);
            }
            previewDirty_ = false;
            lastPreviewTime_ = time_;
            lastResetPose_ = resetPose_;
        }
        const std::array<float, 7> cameraState{camera_.yaw,
                                               camera_.pitch,
                                               camera_.distance,
                                               camera_.target.x,
                                               camera_.target.y,
                                               camera_.target.z,
                                               camera_.fieldOfView};
        if (poseChanged || !previewRendered_ || cameraState != lastCamera_ ||
            previewRevision_ != modelAssetRevision) {
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
                const auto vp = multiply(perspective(camera_.fieldOfView,
                                                     480.f / 270,
                                                     .01f,
                                                     std::max(200.f, camera_.distance * 10)),
                                         lookAt(camera_.position(), camera_.target, camera_.up()));
                renderer.draw(*preview_, vp, 480, 270, camera_.position(), 0, 0, false);
                restore();
            } catch (...) {
                restore();
                throw;
            }

            previewRendered_ = true;
            lastCamera_ = cameraState;
            previewRevision_ = modelAssetRevision;
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

    struct SheetRow {
        std::string label, group;
        int indent = 0;
        int track = -1, lane = -1;
        std::vector<float> times;
    };
    std::vector<SheetRow> importedRows_;
    std::string sheetModel_;
    int sheetAnimation_ = -2;
    std::map<std::string, bool> collapsed_;
    int selectedLane_ = -1;
    int samples_ = 30;
    float zoom_ = 1;
    bool curves_ = false, draggingKey_ = false;
    void captureKey(World& world, Entity selected, int index) {
        auto& track = clip_.tracks[index];
        const auto target = animationTarget(world, selected, track.target);
        AnimationValue value{};
        if (!readAnimationValue(world, target, *animationProperty(track.property), value)) {
            error_ = "Selected target/component does not exist.";
            return;
        }
        auto key = std::lower_bound(track.keys.begin(),
                                    track.keys.end(),
                                    time_,
                                    [](const auto& k, float t) { return k.time < t; });
        if (key != track.keys.end() && std::abs(key->time - time_) < .00001f) {
            key->value = value;
        } else {
            if (track.keys.size() >= 4096) {
                error_ = "Track key limit reached.";
                return;
            }
            key = track.keys.insert(key, {time_, value});
        }
        track_ = index;
        key_ = int(key - track.keys.begin());
        dirty_ = true;
        previewDirty_ = true;
    }
    void drawCurves(float height) {
        auto pos = ImGui::GetCursorScreenPos();
        float width = std::max(1.f, ImGui::GetContentRegionAvail().x);
        ImGui::InvisibleButton("Curves plot", ImVec2(width, height));
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), IM_COL32(30, 33, 39, 255));
        if (track_ < 0 || track_ >= int(clip_.tracks.size())) {
            draw->AddText(ImVec2(pos.x + 12, pos.y + 12),
                          IM_COL32(180, 185, 195, 255),
                          "Select a property track in Dope Sheet to view its curves.");
            return;
        }
        const auto& track = clip_.tracks[track_];
        const auto* property = animationProperty(track.property);
        float low = 0, high = 1;
        for (const auto& k : track.keys) {
            for (int lane = 0; lane < property->lanes(); ++lane) {
                low = std::min(low, k.value[lane]);
                high = std::max(high, k.value[lane]);
            }
        }
        ImU32 colors[]{IM_COL32(235, 100, 100, 255),
                       IM_COL32(115, 215, 145, 255),
                       IM_COL32(100, 155, 245, 255),
                       IM_COL32(215, 170, 240, 255)};
        for (int lane = 0; lane < property->lanes(); ++lane) {
            if (selectedLane_ >= 0 && lane != selectedLane_) {
                continue;
            }
            for (std::size_t k = 1; k < track.keys.size(); ++k) {
                const auto& a = track.keys[k - 1];
                const auto& b = track.keys[k];
                ImVec2 p{pos.x + a.time / clip_.duration * width,
                         pos.y + height - 12 -
                             (a.value[lane] - low) / (high - low) * (height - 24)},
                    q{pos.x + b.time / clip_.duration * width,
                      pos.y + height - 12 - (b.value[lane] - low) / (high - low) * (height - 24)};
                if (property->kind == AnimationValueKind::Boolean) {
                    draw->AddLine(p, ImVec2(q.x, p.y), colors[lane]);
                    draw->AddLine(ImVec2(q.x, p.y), q, colors[lane]);
                } else {
                    draw->AddLine(p, q, colors[lane], 1.5f);
                }
            }
        }
        float x = pos.x + time_ / clip_.duration * width;
        draw->AddLine(ImVec2(x, pos.y), ImVec2(x, pos.y + height), IM_COL32(245, 190, 70, 255));
        if (ImGui::IsItemActive()) {
            time_ =
                std::clamp((ImGui::GetIO().MousePos.x - pos.x) / width, 0.f, 1.f) * clip_.duration;
            playing_ = false;
            resetPose_ = false;
        }
    }

  public:
    bool visible = false;
    void remapAssetPaths(const std::string& from, const std::string& to) {
        auto remap = [&](std::string& p) {
            if (p == from || (p.size() > from.size() && p.compare(0, from.size(), from) == 0 &&
                              p[from.size()] == '/')) {
                p = to + p.substr(from.size());
            }
        };
        remap(path_);
        remap(clip_.model);
        std::string draft = newPath_;
        remap(draft);
        std::snprintf(newPath_, sizeof(newPath_), "%s", draft.c_str());
        sheetModel_.clear();
        previewDirty_ = true;
    }
    bool hasUnsavedChanges() const {
        return dirty_;
    }
    const std::string& error() const {
        return error_;
    }
    bool saveClip(const AssetManager& assets) {
        try {
            if (root_ != assets.root) {
                throw std::runtime_error("Animation belongs to another project");
            }
            const auto path = path_.empty() ? std::string(newPath_) : path_;
            if (!animationAsset(path)) {
                throw std::runtime_error("Use a project-relative .leanim asset path");
            }
            if (path_.empty() && std::filesystem::exists(assets.resolve(path))) {
                throw std::runtime_error("Asset already exists; open it instead");
            }
            std::filesystem::create_directories(assets.resolve(path).parent_path());
            saveAnimationClip(assets, path, clip_);
            path_ = path;
            dirty_ = false;
            error_.clear();
            return true;
        } catch (const std::exception& e) {
            error_ = e.what();
            return false;
        }
    }
    bool saveIfDirty(const AssetManager& assets) {
        return !dirty_ || saveClip(assets);
    }
    void open(const AssetManager& assets, const std::string& path) {
        sheetModel_.clear();
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
            previewDirty_ = true;
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
            sheetModel_.clear();
        }
        ImGui::SetNextWindowSize(ImVec2(1100, 570), ImGuiCond_FirstUseEver);
        bool openWindow = true;
        if (ImGui::Begin(dirty_ ? "Animation*###AnimationEditor" : "Animation###AnimationEditor",
                         &openWindow)) {
            try {
                if (editMode &&
                    ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteFocused)) {
                    saveClip(assets);
                }
                {
                    DisabledScope disabled(!editMode);
                    if (ImGui::Button("Save animation")) {
                        saveClip(assets);
                    }
                }
                ImGui::SameLine();
                ImGui::TextDisabled("Ctrl+S | all tracks and X/Y/Z values");
                // Compact transport bar, with the sheet always in front rather than
                // buried below a long stack of property controls.
                if (ImGui::Button("|<")) {
                    time_ = 0;
                    playing_ = false;
                    resetPose_ = false;
                }
                ImGui::SameLine();
                if (ImGui::Button("<")) {
                    time_ = std::max(0.f, time_ - 1.f / samples_);
                    playing_ = false;
                    resetPose_ = false;
                }
                ImGui::SameLine();
                if (ImGui::Button(playing_ ? "Pause" : "Play")) {
                    playing_ = !playing_;
                    resetPose_ = false;
                }
                ImGui::SameLine();
                if (ImGui::Button(">")) {
                    time_ = std::min(clip_.duration, time_ + 1.f / samples_);
                    playing_ = false;
                    resetPose_ = false;
                }
                ImGui::SameLine();
                if (ImGui::Button(">|")) {
                    time_ = clip_.duration;
                    playing_ = false;
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
                    playing_ = false;
                    time_ = 0;
                    resetPose_ = true;
                }
                ImGui::SameLine();
                ImGui::Checkbox("Loop", &loop_);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(75);
                int frame = int(std::round(time_ * samples_));
                if (ImGui::InputInt("Frame", &frame, 0)) {
                    time_ = std::clamp(frame / float(samples_), 0.f, clip_.duration);
                    playing_ = false;
                    resetPose_ = false;
                }
                ImGui::SameLine();
                ImGui::TextDisabled(" / %.2fs", clip_.duration);
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
                ImGui::Separator();
                if (ImGui::Button("Clip / Settings")) {
                    ImGui::OpenPopup("Clip settings");
                }
                ImGui::SameLine();
                ImGui::TextUnformatted(path_.empty() ? "Unsaved animation" : path_.c_str());
                ImGui::SameLine();
                ImGui::SetNextItemWidth(65);
                if (ImGui::InputInt("Samples", &samples_, 0)) {
                    samples_ = std::clamp(samples_, 1, 240);
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(95);
                ImGui::SliderFloat("Zoom", &zoom_, 1, 12, "%.1fx");
                if (ImGui::BeginPopup("Clip settings")) {
                    PopupScope popup;
                    auto next = path_;
                    if (assetPicker("Open clip",
                                    next,
                                    &assets,
                                    [](const auto& p) { return animationAsset(p); }) &&
                        !next.empty()) {
                        open(assets, next);
                    }
                    {
                        DisabledScope disabled(!editMode);
                        if (ImGui::Button("New clip")) {
                            if (dirty_) {
                                error_ = "Save or discard edits first.";
                            } else {
                                clip_ = {};
                                previewDirty_ = true;
                                path_.clear();
                                sheetModel_.clear();
                                track_ = key_ = -1;
                                time_ = 0;
                            }
                        }
                        ImGui::InputText("Asset path", newPath_, sizeof(newPath_));
                        if (ImGui::Button("Save clip")) {
                            saveClip(assets);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Discard edits")) {
                            if (path_.empty()) {
                                clip_ = {};
                                previewDirty_ = true;
                            } else {
                                clip_ = loadAnimationClip(assets, path_);
                            }
                            dirty_ = false;
                            track_ = key_ = -1;
                            time_ = 0;
                        }
                        float duration = clip_.duration;
                        {
                            DisabledScope imported(!clip_.model.empty());
                            if (ImGui::InputFloat("Duration", &duration, .1f, 1) &&
                                std::isfinite(duration)) {
                                float last = .001f;
                                for (const auto& t : clip_.tracks) {
                                    if (!t.keys.empty()) {
                                        last = std::max(last, t.keys.back().time);
                                    }
                                }
                                clip_.duration = std::clamp(duration, last, 86400.f);
                                time_ = std::min(time_, clip_.duration);
                                dirty_ = true;
                                previewDirty_ = true;
                            }
                        }
                        if (world.alive(selected) && !path_.empty() &&
                            ImGui::Button("Assign to selected object")) {
                            history.commit(world, selected);
                            if (!world.has<Animation>(selected)) {
                                world.add(selected, Animation{});
                            }
                            auto& a = world.get<Animation>(selected);
                            a.clip = path_;
                            a.loop = loop_;
                            history.commit(world, selected);
                        }
                    }
                }
                if (ImGui::Button("+ Add Property")) {
                    ImGui::OpenPopup("Add property");
                }
                if (ImGui::BeginPopup("Add property")) {
                    PopupScope popup;
                    DisabledScope disabled(!editMode || clip_.tracks.size() >= 64);
                    ImGui::InputText("Child path (empty = self)", target_, sizeof(target_));
                    if (ImGui::BeginCombo("Property", animationProperties[property_].name)) {
                        for (int i = 0;
                             i < int(sizeof(animationProperties) / sizeof(animationProperties[0]));
                             ++i) {
                            if (ImGui::Selectable(animationProperties[i].name, property_ == i)) {
                                property_ = i;
                            }
                        }
                        ImGui::EndCombo();
                    }
                    if (ImGui::Button("Add track")) {
                        std::string target = target_,
                                    property = animationProperties[property_].name;
                        bool duplicate = false;
                        for (const auto& t : clip_.tracks) {
                            duplicate |= t.target == target && t.property == property;
                        }
                        if (duplicate) {
                            error_ = "That property already has a track.";
                        } else {
                            clip_.tracks.push_back({target, property, {}});
                            track_ = int(clip_.tracks.size()) - 1;
                            key_ = -1;
                            dirty_ = true;
                            previewDirty_ = true;
                        }
                        ImGui::CloseCurrentPopup();
                    }
                }
                ImGui::SameLine();
                ImGui::TextDisabled(
                    "Click ruler to scrub | drag diamonds to retime | double-click a lane to capture");
                if (sheetModel_ != clip_.model || sheetAnimation_ != clip_.modelAnimation) {
                    importedRows_.clear();
                    sheetModel_ = clip_.model;
                    sheetAnimation_ = clip_.modelAnimation;
                    if (!clip_.model.empty()) {
                        const auto doc = modelDocument(assets, clip_.model);
                        const auto& channels =
                            doc.at("animations").at(clip_.modelAnimation).at("channels");
                        for (const auto& c : channels) {
                            int node = c.at("node");
                            std::string name = doc.at("nodes").at(node).value(
                                "name", std::string("Bone ") + std::to_string(node));
                            for (const auto* type : {"positions", "rotations", "scales"}) {
                                const auto& keys = c.at(type);
                                if (keys.empty()) {
                                    continue;
                                }
                                SheetRow row;
                                row.label = name + " : " + type;
                                row.indent = 1;
                                for (int parent = doc.at("nodes").at(node).at("parent");
                                     parent >= 0 && row.indent < 32;
                                     parent = doc.at("nodes").at(parent).at("parent")) {
                                    ++row.indent;
                                }
                                for (const auto& k : keys) {
                                    row.times.push_back(k.at(0));
                                }
                                importedRows_.push_back(std::move(row));
                            }
                        }
                    }
                }
                std::vector<SheetRow> propertyRows;
                propertyRows.reserve(clip_.tracks.size() * 5);
                std::map<std::string, std::vector<int>> groups;
                for (int i = 0; i < int(clip_.tracks.size()); ++i) {
                    groups[clip_.tracks[i].target].push_back(i);
                }
                for (const auto& group : groups) {
                    SheetRow header;
                    header.track = -2;
                    header.group = group.first;
                    header.label = group.first.empty() ? "Self" : group.first;
                    header.indent = int(std::count(group.first.begin(), group.first.end(), '/'));
                    propertyRows.push_back(std::move(header));
                    if (collapsed_[group.first]) {
                        continue;
                    }
                    for (int index : group.second) {
                        const auto& t = clip_.tracks[index];
                        SheetRow row;
                        row.label = t.property;
                        row.track = index;
                        row.indent =
                            int(std::count(group.first.begin(), group.first.end(), '/')) + 1;
                        for (const auto& k : t.keys) {
                            row.times.push_back(k.time);
                        }
                        propertyRows.push_back(std::move(row));
                        if (t.property == "Transform.position") {
                            static const char* axes[]{"X", "Y", "Z"};
                            for (int lane = 0; lane < 3; ++lane) {
                                SheetRow axis;
                                axis.label = axes[lane];
                                axis.track = index;
                                axis.lane = lane;
                                axis.indent =
                                    int(std::count(group.first.begin(), group.first.end(), '/')) +
                                    2;
                                for (const auto& key : t.keys) {
                                    axis.times.push_back(key.time);
                                }
                                propertyRows.push_back(std::move(axis));
                            }
                        }
                    }
                }
                std::vector<const SheetRow*> rows;
                rows.reserve(propertyRows.size() + importedRows_.size());
                for (const auto& row : propertyRows) {
                    rows.push_back(&row);
                }
                for (const auto& row : importedRows_) {
                    rows.push_back(&row);
                }
                const float sheetHeight = std::max(170.f, ImGui::GetContentRegionAvail().y - 130.f);
                if (!curves_) {
                    const float laneWidth =
                        std::max(240.f, (ImGui::GetContentRegionAvail().x - 280.f) * zoom_);
                    if (ImGui::BeginTable("Dope sheet",
                                          2,
                                          ImGuiTableFlags_BordersInnerV |
                                              ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollX |
                                              ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg,
                                          ImVec2(0, sheetHeight))) {
                        TableScope table;
                        ImGui::TableSetupColumn(
                            "Hierarchy / Property", ImGuiTableColumnFlags_WidthFixed, 260);
                        ImGui::TableSetupColumn(
                            "Timeline", ImGuiTableColumnFlags_WidthFixed, laneWidth);
                        ImGui::TableSetupScrollFreeze(1, 1);
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted("Hierarchy / Property");
                        ImGui::TableNextColumn();
                        auto ruler = ImGui::GetCursorScreenPos();
                        ImGui::InvisibleButton("Ruler", ImVec2(laneWidth, 30));
                        auto* draw = ImGui::GetWindowDrawList();
                        if (ImGui::IsItemActive()) {
                            time_ = std::clamp((ImGui::GetIO().MousePos.x - ruler.x) / laneWidth,
                                               0.f,
                                               1.f) *
                                    clip_.duration;
                            playing_ = false;
                            resetPose_ = false;
                        }
                        const int ticks = std::clamp(int(laneWidth / 80), 2, 100);
                        for (int i = 0; i <= ticks; ++i) {
                            float x = ruler.x + laneWidth * i / ticks;
                            char label[32];
                            float t = clip_.duration * i / ticks;
                            std::snprintf(label,
                                          sizeof(label),
                                          "%d:%02d",
                                          int(t),
                                          int(std::round((t - int(t)) * samples_)));
                            draw->AddText(
                                ImVec2(x + 3, ruler.y), IM_COL32(185, 190, 200, 255), label);
                            draw->AddLine(ImVec2(x, ruler.y + 18),
                                          ImVec2(x, ruler.y + 30),
                                          IM_COL32(110, 115, 125, 255));
                        }
                        const float rulerHead = ruler.x + laneWidth * time_ / clip_.duration;
                        draw->AddTriangleFilled(ImVec2(rulerHead - 5, ruler.y + 18),
                                                ImVec2(rulerHead + 5, ruler.y + 18),
                                                ImVec2(rulerHead, ruler.y + 25),
                                                IM_COL32(235, 90, 75, 255));
                        ImGuiListClipper clipper;
                        clipper.Begin(int(rows.size()), 25.f);
                        while (clipper.Step()) {
                            for (int rowIndex = clipper.DisplayStart; rowIndex < clipper.DisplayEnd;
                                 ++rowIndex) {
                                const auto& row = *rows[rowIndex];
                                ImGui::PushID(rowIndex);
                                ImGui::TableNextRow(0, 25);
                                ImGui::TableNextColumn();
                                ImGui::Indent(std::min(row.indent, 12) * 12.f);
                                auto label = row.track == -2
                                                 ? (collapsed_[row.group] ? "> " : "v ") + row.label
                                                 : row.label;
                                if (row.lane >= 0) {
                                    const auto& t = clip_.tracks[row.track];
                                    char value[48];
                                    if (t.keys.empty()) {
                                        std::snprintf(value, sizeof(value), "   --");
                                    } else {
                                        std::snprintf(value,
                                                      sizeof(value),
                                                      "   %.3f",
                                                      sampleAnimationTrack(t, time_)[row.lane]);
                                    }
                                    label += value;
                                }
                                if (ImGui::Selectable(label.c_str(),
                                                      row.track >= 0 && track_ == row.track &&
                                                          selectedLane_ == row.lane,
                                                      0,
                                                      ImVec2(0, 21))) {
                                    if (row.track == -2) {
                                        collapsed_[row.group] = !collapsed_[row.group];
                                    } else {
                                        track_ = row.track;
                                        selectedLane_ = row.lane;
                                        key_ = -1;
                                    }
                                }
                                ImGui::Unindent(std::min(row.indent, 12) * 12.f);
                                if (ImGui::IsItemHovered()) {
                                    ImGui::SetTooltip("%s%s",
                                                      row.label.c_str(),
                                                      row.track < 0 ? " (imported, read-only)"
                                                                    : "");
                                }
                                ImGui::TableNextColumn();
                                auto pos = ImGui::GetCursorScreenPos();
                                ImGui::InvisibleButton("Lane", ImVec2(laneWidth, 21));
                                draw = ImGui::GetWindowDrawList();
                                for (int i = 0; i <= ticks; ++i) {
                                    float x = pos.x + laneWidth * i / ticks;
                                    draw->AddLine(ImVec2(x, pos.y),
                                                  ImVec2(x, pos.y + 25),
                                                  IM_COL32(70, 74, 82, 100));
                                }
                                if (ImGui::IsItemClicked()) {
                                    draggingKey_ = false;
                                    float nearest = 8;
                                    for (int k = 0; k < int(row.times.size()); ++k) {
                                        float distance = std::abs(
                                            ImGui::GetIO().MousePos.x -
                                            (pos.x + laneWidth * row.times[k] / clip_.duration));
                                        if (distance < nearest) {
                                            nearest = distance;
                                            track_ = row.track;
                                            selectedLane_ = row.lane;
                                            key_ = k;
                                            time_ = row.times[k];
                                            draggingKey_ = row.track >= 0 && editMode;
                                        }
                                    }
                                    if (nearest == 8) {
                                        time_ = std::clamp((ImGui::GetIO().MousePos.x - pos.x) /
                                                               laneWidth,
                                                           0.f,
                                                           1.f) *
                                                clip_.duration;
                                        track_ = row.track;
                                        selectedLane_ = row.lane;
                                        key_ = -1;
                                    }
                                    playing_ = false;
                                    resetPose_ = false;
                                }
                                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0)) {
                                    float t =
                                        std::round(std::clamp((ImGui::GetIO().MousePos.x - pos.x) /
                                                                  laneWidth,
                                                              0.f,
                                                              1.f) *
                                                   clip_.duration * samples_) /
                                        samples_;
                                    if (draggingKey_ && row.track >= 0 && key_ >= 0) {
                                        auto& keys = clip_.tracks[row.track].keys;
                                        float low = key_ > 0 ? keys[key_ - 1].time + .0001f : 0,
                                              high = key_ + 1 < int(keys.size())
                                                         ? keys[key_ + 1].time - .0001f
                                                         : clip_.duration;
                                        if (low <= high) {
                                            keys[key_].time = std::clamp(t, low, high);
                                            time_ = keys[key_].time;
                                            dirty_ = true;
                                            previewDirty_ = true;
                                        }
                                    } else {
                                        time_ = t;
                                    }
                                }
                                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) &&
                                    row.track >= 0 && editMode) {
                                    captureKey(world, selected, row.track);
                                }
                                const float firstTime =
                                    std::max(0.f,
                                             (draw->GetClipRectMin().x - pos.x - 5) / laneWidth *
                                                 clip_.duration);
                                const float lastTime =
                                    std::min(clip_.duration,
                                             (draw->GetClipRectMax().x - pos.x + 5) / laneWidth *
                                                 clip_.duration);
                                auto marker =
                                    std::lower_bound(row.times.begin(), row.times.end(), firstTime);
                                while (marker != row.times.end() && *marker <= lastTime) {
                                    const int k = int(marker - row.times.begin());
                                    const float x = pos.x + laneWidth * (*marker) / clip_.duration,
                                                y = pos.y + 10;
                                    const ImU32 color = row.track == track_ && k == key_
                                                            ? IM_COL32(255, 197, 70, 255)
                                                            : IM_COL32(175, 190, 210, 255);
                                    draw->AddQuadFilled(ImVec2(x, y - 4),
                                                        ImVec2(x + 4, y),
                                                        ImVec2(x, y + 4),
                                                        ImVec2(x - 4, y),
                                                        color);
                                    // Dense imported clips draw at most one diamond per two
                                    // pixels, rather than generating a million hidden quads.
                                    const float next = (*marker) + 2.f / laneWidth * clip_.duration;
                                    marker = std::lower_bound(marker + 1, row.times.end(), next);
                                }
                                float playhead = pos.x + laneWidth * time_ / clip_.duration;
                                draw->AddLine(ImVec2(playhead, pos.y),
                                              ImVec2(playhead, pos.y + 25),
                                              IM_COL32(235, 90, 75, 255),
                                              1.5f);
                                ImGui::PopID();
                            }
                        }
                        if (rows.empty()) {
                            ImGui::TableNextRow();
                            ImGui::TableNextColumn();
                            ImGui::TextDisabled("Add a property to begin.");
                        }
                    }
                } else {
                    drawCurves(sheetHeight);
                }
                if (ImGui::Button("Dope Sheet")) {
                    curves_ = false;
                }
                ImGui::SameLine();
                if (ImGui::Button("Curves")) {
                    curves_ = true;
                }
                ImGui::SameLine();
                ImGui::TextDisabled(
                    "%zu tracks | frame %d", rows.size(), int(std::round(time_ * samples_)));
                if (track_ >= 0 && track_ < int(clip_.tracks.size())) {
                    DisabledScope disabled(!editMode);
                    auto& track = clip_.tracks[track_];
                    if (ImGui::Button("Capture key")) {
                        captureKey(world, selected, track_);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Delete track")) {
                        clip_.tracks.erase(clip_.tracks.begin() + track_);
                        track_ = key_ = -1;
                        dirty_ = true;
                        previewDirty_ = true;
                    } else if (key_ >= 0 && key_ < int(track.keys.size())) {
                        auto& key = track.keys[key_];
                        const auto* property = animationProperty(track.property);
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(90);
                        float t = key.time;
                        if (ImGui::DragFloat(
                                "Key time", &t, 1.f / samples_, 0, clip_.duration, "%.3f")) {
                            float low = key_ ? track.keys[key_ - 1].time + .0001f : 0,
                                  high = key_ + 1 < int(track.keys.size())
                                             ? track.keys[key_ + 1].time - .0001f
                                             : clip_.duration;
                            if (low <= high) {
                                key.time = std::clamp(t, low, high);
                                time_ = key.time;
                                dirty_ = true;
                                previewDirty_ = true;
                            }
                        }
                        if (property->kind == AnimationValueKind::Boolean) {
                            bool v = key.value[0] >= .5f;
                            if (ImGui::Checkbox("Value", &v)) {
                                key.value[0] = v ? 1.f : 0.f;
                                dirty_ = true;
                                previewDirty_ = true;
                            }
                        } else if (property->kind == AnimationValueKind::Vector) {
                            const char* axes[]{"X", "Y", "Z"};
                            for (int lane = 0; lane < 3; ++lane) {
                                if (lane) {
                                    ImGui::SameLine();
                                }
                                ImGui::PushID(lane);
                                ImGui::SetNextItemWidth(std::max(
                                    65.f, (ImGui::GetContentRegionAvail().x - 90.f) / (3 - lane)));
                                if (ImGui::DragFloat(axes[lane],
                                                     &key.value[lane],
                                                     .01f,
                                                     property->minimum,
                                                     property->maximum,
                                                     "%.3f",
                                                     ImGuiSliderFlags_AlwaysClamp)) {
                                    dirty_ = true;
                                    previewDirty_ = true;
                                    selectedLane_ = lane;
                                }
                                ImGui::PopID();
                            }
                        } else if (ImGui::DragScalarN("Value",
                                                      ImGuiDataType_Float,
                                                      key.value.data(),
                                                      property->lanes(),
                                                      .01f,
                                                      &property->minimum,
                                                      &property->maximum,
                                                      "%.3f",
                                                      ImGuiSliderFlags_AlwaysClamp)) {
                            dirty_ = true;
                            previewDirty_ = true;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Delete key")) {
                            track.keys.erase(track.keys.begin() + key_);
                            key_ = -1;
                            dirty_ = true;
                            previewDirty_ = true;
                        }
                    }
                }
                if (ImGui::CollapsingHeader("Isolated preview")) {
                    if (world.alive(selected) && (!base_ || selected != owner_)) {
                        rebuild(world, selected, assets, renderer);
                    }
                    if (ImGui::Button("Refresh preview") && world.alive(selected)) {
                        rebuild(world, selected, assets, renderer);
                    }
                    ImGui::SameLine();
                    {
                        DisabledScope disabled(!editMode || !world.alive(selected));
                        if (ImGui::Button("Apply frame to Scene (undoable)")) {
                            history.commit(world, selected);
                            applyAnimationClip(world, selected, clip_, time_);
                            history.commit(world, selected);
                        }
                    }
                    render(renderer, assets, std::min(400.f, ImGui::GetContentRegionAvail().x));
                }
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
                error_ = "Save or discard clip edits before closing.";
            } else {
                visible = false;
            }
        }
    }
};
