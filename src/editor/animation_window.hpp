#pragma once
#include "animation/animation_clip.hpp"
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
  public:
    // One animated channel of the imported model, ready for a Qt tree/timeline.
    struct SheetRow {
        std::string label, group;
        int indent = 0;
        int track = -1, lane = -1;
        std::vector<float> times;
    };
  private:
    AnimationClip clip_;
    std::string path_, error_;
    std::filesystem::path root_;
    bool dirty_ = false, playing_ = false, loop_ = false, resetPose_ = false;
    float time_ = 0;
    int track_ = -1, key_ = -1, property_ = 0;
    char newPath_[512] = "Animations/NewAnimation.leanim", target_[1024]{};
    Entity owner_, previewOwner_;
    std::unique_ptr<World> base_, preview_;
    SceneSurface surface_;
    OrbitCamera camera_;
    bool previewDirty_ = true, previewRendered_ = false, lastResetPose_ = false,
         previewStreaming_ = false;
    float lastPreviewTime_ = -1;
    std::array<float, 7> lastCamera_{};
    unsigned previewRevision_ = ~0u, renderedRevision_ = 0;
    OrbitCamera& camera() {
        return camera_;
    }
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
        ++renderedRevision_;
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
            previewRevision_ != modelAssetRevision || previewStreaming_ || renderer.streaming()) {
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
            previewStreaming_ = renderer.streaming();
            ++renderedRevision_;
            lastCamera_ = cameraState;
            previewRevision_ = modelAssetRevision;
        }
        (void)width;
    }

    std::vector<SheetRow> importedRows_;
    std::string sheetModel_;
    int sheetAnimation_ = -2;
    std::map<std::string, bool> collapsed_;
    int selectedLane_ = -1;
    int samples_ = 30;
    float zoom_ = 1;
    bool curves_ = false, draggingKey_ = false;
    // Value-owned clipboard survives track edits and switching clips. Copy full
    // property keys (all XYZ/quaternion lanes), never pointers into a clip.
    std::vector<AnimationKey> copiedKeys_;
    std::string copiedProperty_;
    bool canCopyKeys(bool wholeTrack = false) const {
        if (track_ < 0 || track_ >= int(clip_.tracks.size())) {
            return false;
        }
        const auto& keys = clip_.tracks[track_].keys;
        return !keys.empty() && (wholeTrack || (key_ >= 0 && key_ < int(keys.size())));
    }
    bool canPasteKeys() const {
        return !copiedKeys_.empty() && track_ >= 0 && track_ < int(clip_.tracks.size()) &&
               clip_.tracks[track_].property == copiedProperty_;
    }
    void copyKeys(bool wholeTrack = false) {
        if (!canCopyKeys(wholeTrack)) {
            return;
        }
        const auto& track = clip_.tracks[track_];
        copiedKeys_ = wholeTrack ? track.keys : std::vector<AnimationKey>{track.keys[key_]};
        copiedProperty_ = track.property;
        const float origin = copiedKeys_.front().time;
        for (auto& key : copiedKeys_) {
            key.time -= origin;
        }
        error_.clear();
    }
    void pasteKeys() {
        if (!canPasteKeys()) {
            error_ = "Select a destination track with the same property as the copied keys.";
            return;
        }
        const float start = std::clamp(time_, 0.f, clip_.duration);
        const float end = start + copiedKeys_.back().time;
        if (!std::isfinite(end) || end > 86400) {
            error_ = "Pasted keys would exceed the 86400-second clip limit.";
            return;
        }
        // Stage the entire operation so limits cannot leave a partial paste.
        auto keys = clip_.tracks[track_].keys;
        for (auto key : copiedKeys_) {
            key.time += start;
            auto at = std::lower_bound(
                keys.begin(), keys.end(), key.time, [](const auto& existing, float time) {
                    return existing.time < time;
                });
            if (at != keys.end() && std::abs(at->time - key.time) < .00001f) {
                *at = key;
            } else if (at != keys.begin() && std::abs((at - 1)->time - key.time) < .00001f) {
                *(at - 1) = key;
            } else {
                keys.insert(at, key);
            }
        }
        std::size_t total = keys.size();
        for (int i = 0; i < int(clip_.tracks.size()); ++i) {
            if (i != track_) {
                total += clip_.tracks[i].keys.size();
            }
        }
        if (keys.size() > 4096 || total > 16384) {
            error_ = "Paste exceeds the animation key limit (4096 per track, 16384 per clip).";
            return;
        }
        auto& destination = clip_.tracks[track_].keys;
        destination = std::move(keys);
        auto pasted = std::lower_bound(destination.begin(),
                                       destination.end(),
                                       start,
                                       [](const auto& key, float time) { return key.time < time; });
        key_ = int(pasted - destination.begin());
        clip_.duration = std::max(clip_.duration, end);
        playing_ = false;
        draggingKey_ = false;
        resetPose_ = false;
        dirty_ = previewDirty_ = true;
        error_.clear();
    }
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
  public:
    bool visible = false;
    // Rebuilds importedRows_ when the clip's model or animation changed.
    void syncSheet(const AssetManager& assets) {
        if (sheetModel_ == clip_.model && sheetAnimation_ == clip_.modelAnimation) {
            return;
        }
        importedRows_.clear();
        sheetModel_ = clip_.model;
        sheetAnimation_ = clip_.modelAnimation;
        if (clip_.model.empty()) {
            return;
        }
        const auto doc = modelDocument(assets, clip_.model);
        const auto& channels = doc.at("animations").at(clip_.modelAnimation).at("channels");
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
    const std::vector<SheetRow>& sheetRows() const {
        return importedRows_;
    }
    // Starts a brand new clip; the dock calls this only after a dirty check.
    void newClip() {
        clip_ = {};
        previewDirty_ = true;
        path_.clear();
        sheetModel_.clear();
        importedRows_.clear();
        track_ = key_ = -1;
        time_ = 0;
        playing_ = false;
        dirty_ = false;
    }
    // Reloads the clip from disk (Discard edits).
    void discardEdits(const AssetManager& assets) {
        if (path_.empty()) {
            clip_ = {};
            previewDirty_ = true;
        } else {
            clip_ = loadAnimationClip(assets, path_);
            previewDirty_ = true;
        }
        dirty_ = false;
        track_ = key_ = -1;
        time_ = 0;
        playing_ = false;
    }
    void setPath(const std::string& path) {
        path_ = path;
    }
    const char* draftPath() const {
        return newPath_;
    }
    void setDraftPath(const std::string& path) {
        std::snprintf(newPath_, sizeof(newPath_), "%s", path.c_str());
    }
    const char* draftTarget() const {
        return target_;
    }
    void setDraftTarget(const std::string& target) {
        std::snprintf(target_, sizeof(target_), "%s", target.c_str());
    }
    int propertyIndex() const {
        return property_;
    }
    void setPropertyIndex(int index) {
        property_ = index;
    }
    int samples() const {
        return samples_;
    }
    void setSamples(int samples) {
        samples_ = std::clamp(samples, 1, 240);
    }
    OrbitCamera& cameraRef() {
        return camera_;
    }
    // Bumped whenever the preview framebuffer is redrawn, so the dock only
    // copies the picture when it changed.
    unsigned renderedRevision() const {
        return renderedRevision_;
    }
    void setResetPose(bool reset) {
        resetPose_ = reset;
    }
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
    // ---- Data accessors used by the Qt Animation dock -------------------
    const AnimationClip& clip() const {
        return clip_;
    }
    AnimationClip& clip() {
        return clip_;
    }
    const std::string& path() const {
        return path_;
    }
    float time() const {
        return time_;
    }
    void setTime(float seconds) {
        time_ = std::clamp(seconds, 0.0f, clip_.duration);
        previewDirty_ = true;
    }
    bool playing() const {
        return playing_;
    }
    void setPlaying(bool playing) {
        playing_ = playing;
    }
    bool loop() const {
        return loop_;
    }
    void setLoop(bool loop) {
        loop_ = loop;
    }
    int track() const {
        return track_;
    }
    int key() const {
        return key_;
    }
    void selectTrack(int track) {
        track_ = track;
    }
    void selectKey(int key) {
        key_ = key;
    }
    bool dirty() const {
        return dirty_;
    }
    void markDirty() {
        dirty_ = true;
        previewDirty_ = true;
    }
    void advance(float dt) {
        if (!playing_) {
            return;
        }
        time_ += dt;
        if (time_ >= clip_.duration) {
            if (loop_) {
                time_ = std::fmod(time_, std::max(clip_.duration, 0.001f));
            } else {
                time_ = clip_.duration;
                playing_ = false;
            }
        }
        previewDirty_ = true;
    }
    // Copies the current frame into the scene as one undoable edit.
    bool applyCurrentFrame(World& world, Entity selected, EditorHistory& history) {
        if (!world.alive(selected)) {
            return false;
        }
        history.commit(world, selected);
        applyAnimationClip(world, selected, clip_, time_);
        history.commit(world, selected);
        return true;
    }
    bool copyKeysPublic(bool wholeTrack = false) {
        copyKeys(wholeTrack);
        return !copiedKeys_.empty();
    }
    void pasteKeysPublic() {
        pasteKeys();
    }
    bool canPasteKeysPublic() const {
        return !copiedKeys_.empty();
    }
    void addTrack(const AnimationTrack& track) {
        clip_.tracks.push_back(track);
        markDirty();
    }
    void captureKeyAt(World& world, Entity selected, int index) {
        captureKey(world, selected, index);
    }
    void deleteKey(int index) {
        if (index < 0 || track_ < 0 || track_ >= int(clip_.tracks.size())) {
            return;
        }
        auto& keys = clip_.tracks[std::size_t(track_)].keys;
        if (index < int(keys.size())) {
            keys.erase(keys.begin() + index);
            key_ = -1;
            markDirty();
        }
    }
    void setKeyTime(int index, float time) {
        if (track_ < 0 || track_ >= int(clip_.tracks.size())) {
            return;
        }
        auto& keys = clip_.tracks[std::size_t(track_)].keys;
        if (index >= 0 && index < int(keys.size()) && std::isfinite(time)) {
            keys[std::size_t(index)].time = std::clamp(time, 0.0f, clip_.duration);
            markDirty();
        }
    }
    bool setDuration(float seconds) {
        if (!std::isfinite(seconds) || seconds <= 0 || seconds > 86400) {
            return false;
        }
        clip_.duration = seconds;
        markDirty();
        return true;
    }
    void rebuildPreview(World& world, Entity selected, const AssetManager& assets, Renderer& renderer) {
        rebuild(world, selected, assets, renderer);
    }
    // Scrubs the isolated preview; the Qt dock paints sceneImage() afterwards.
    void updatePreview(Renderer& renderer, const AssetManager& assets, float width) {
        render(renderer, assets, width);
    }
    const SceneSurface& surface() const {
        return surface_;
    }
    void resetPreview() {
        previewDirty_ = true;
        previewRendered_ = false;
    }

};
