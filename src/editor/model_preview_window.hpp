#pragma once
#include "models/model_asset.hpp"
#include "rendering/renderer.hpp"
#include "rendering/scene_surface.hpp"
#include "scene/camera.hpp"
#include <array>

class ModelPreviewWindow {
    std::unique_ptr<World> world_;
    Entity owner_;
    Json document_;
    std::string path_, error_;
    std::filesystem::path root_;
    SceneSurface surface_;
    OrbitCamera camera_;
    int clip_ = 0;
    float time_ = 0, fadeSeconds_ = .25f, fadeElapsed_ = 0, fadeDuration_ = 0, sourceDuration_ = 1;
    bool blendClips_ = true;
    std::array<float, 12> lastView_{};
    bool rendered_ = false, streaming_ = false;
    unsigned renderedRevision_ = 0;
    unsigned openedRevision_ = ~0u;
    bool playing_ = false, loop_ = true, bind_ = true;

    static const Json& emptyModelClips() {
        static const Json empty = Json::array();
        return empty;
    }

  public:
    bool visible = false;
    void open(const AssetManager& assets, const std::string& path, Renderer& renderer) {
        visible = true;
        if (world_ && root_ == assets.root && path_ == path &&
            openedRevision_ == modelAssetRevision) {
            return;
        }
        error_.clear();
        try {
            auto doc = modelDocument(assets, path);
            const auto& clips =
                doc.contains("animations") ? doc.at("animations") : emptyModelClips();
            if (!clips.is_array() || clips.size() > 256) {
                throw std::runtime_error("Invalid model animation list");
            }
            for (const auto& clip : clips) {
                const auto name = clip.at("name").get<std::string>();
                const auto duration = clip.at("duration").get<float>();
                if (name.size() > 4096 || !std::isfinite(duration) || duration <= 0 ||
                    duration > 86400) {
                    throw std::runtime_error("Invalid model animation metadata");
                }
            }
            if (doc.at("version") == 2 && (!doc.at("parts").is_array() || doc.at("parts").empty() ||
                                           doc.at("parts").size() > 1024)) {
                throw std::runtime_error("Invalid model parts");
            }
            auto world = std::make_unique<World>();
            auto owner = world->create();
            world->add(owner, Transform{});
            world->add(owner, Animation{});
            if (doc.at("version") == 2) {
                for (const auto& p : doc.at("parts")) {
                    auto e = world->create();
                    world->add(e, Transform{});
                    world->add(e, Parent{owner});
                    MeshRenderer mesh;
                    mesh.model = p.at("mesh");
                    mesh.material = p.at("material");
                    world->add(e, mesh);
                }
            } else {
                MeshRenderer mesh;
                mesh.model = path;
                world->add(owner, mesh);
            }
            auto sun = world->create();
            world->add(sun, Transform{});
            DirectionalLight light;
            light.shadows = false;
            light.intensity = 1.6f;
            world->add(sun, light);
            auto fill = world->create();
            Transform fillPose;
            fillPose.rotation = axisAngle({0, 1, 0}, radians(160));
            world->add(fill, fillPose);
            DirectionalLight fillLight;
            fillLight.intensity = .55f;
            fillLight.shadows = false;
            world->add(fill, fillLight);
            Vec3 low{}, high{};
            if (!renderer.modelBounds(assets, path, low, high)) {
                throw std::runtime_error("Model has no geometry");
            }
            camera_ = {};
            camera_.target = (low + high) * .5f;
            camera_.distance = std::max(.1f, std::sqrt(dot(high - low, high - low)) * 1.5f);
            openedRevision_ = modelAssetRevision;
            root_ = assets.root;
            path_ = path;
            document_ = std::move(doc);
            world_ = std::move(world);
            owner_ = owner;
            clip_ = 0;
            time_ = 0;
            playing_ = false;
            bind_ = true;
            fadeDuration_ = 0;
            rendered_ = false;
        } catch (const std::exception& e) {
            error_ = e.what();
            world_.reset();
        }
    }
    // Advances the clip and redraws the 720x405 preview when anything changed.
    // The Qt Model Preview dialog paints surface() afterwards.
    void update(const AssetManager& assets, Renderer& renderer, float dt) {
        if (!visible) {
            return;
        }
        if (root_ != assets.root) {
            world_.reset();
            visible = false;
            return;
        }
        if (world_ && openedRevision_ != modelAssetRevision) {
            open(assets, path_, renderer);
        }
        if (!world_) {
            return;
        }
        static const Json emptyClips = Json::array();
        const auto& clips = document_.contains("animations") ? document_.at("animations") : emptyClips;
        const bool hasClips = !clips.empty();
        const float duration = hasClips ? clips.at(clip_).at("duration").get<float>() : 0;
        if (playing_ && duration > 0) {
            time_ += std::clamp(dt, 0.f, .25f);
            if (time_ >= duration) {
                if (loop_) {
                    time_ = std::fmod(time_, duration);
                } else {
                    time_ = duration;
                    playing_ = false;
                }
            }
        }
        auto& a = world_->get<Animation>(owner_);
        if (playing_ && fadeDuration_ > 0) {
            fadeElapsed_ += std::clamp(dt, 0.f, .25f);
            a.blendWeight = std::min(1.f, fadeElapsed_ / fadeDuration_);
            a.blendSourceTime += std::clamp(dt, 0.f, .25f);
            if (a.blendSourceTime > sourceDuration_) {
                a.blendSourceTime =
                    loop_ ? std::fmod(a.blendSourceTime, sourceDuration_) : sourceDuration_;
            }
            if (a.blendWeight >= 1) {
                fadeDuration_ = 0;
                a.blendFrozenPose.clear();
            }
        }
        a.model = path_;
        a.modelAnimation = clip_;
        a.poseActive = hasClips && !bind_;
        a.time = time_;
        renderFrame(assets, renderer);
    }
    void renderFrame(const AssetManager& assets, Renderer& renderer) {
        if (!world_) {
            return;
        }
        auto& a = world_->get<Animation>(owner_);
        const std::array<float, 12> view{time_,
                                         float(clip_),
                                         a.blendWeight,
                                         camera_.yaw,
                                         camera_.pitch,
                                         camera_.distance,
                                         camera_.target.x,
                                         camera_.target.y,
                                         camera_.target.z,
                                         bind_ ? 1.f : 0.f,
                                         float(modelAssetRevision),
                                         float(fadeDuration_)};
        if (rendered_ && view == lastView_ && !streaming_ && !renderer.streaming()) {
            return;
        }
        GLint draw = 0, read = 0, viewport[4];
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
        glGetIntegerv(GL_VIEWPORT, viewport);
        try {
            surface_.resize(720, 405);
            glBindFramebuffer(GL_FRAMEBUFFER, surface_.framebuffer);
            renderer.setAssetRoot(assets.root);
            renderer.draw(*world_,
                          camera_.viewProjection(720.f / 405),
                          720,
                          405,
                          camera_.position(),
                          0,
                          0,
                          false);
        } catch (const std::exception& e) {
            error_ = e.what();
        }
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        rendered_ = true;
        streaming_ = renderer.streaming();
        lastView_ = view;
        ++renderedRevision_;
    }

    // ---- Qt dialog accessors ---------------------------------------------
    const std::string& path() const {
        return path_;
    }
    const std::string& error() const {
        return error_;
    }
    bool loaded() const {
        return world_ != nullptr;
    }
    const SceneSurface& surface() const {
        return surface_;
    }
    // Bumped every time the preview framebuffer is redrawn, so the Qt dialog
    // only copies the picture when it changed.
    unsigned renderedRevision() const {
        return renderedRevision_;
    }
    OrbitCamera& camera() {
        return camera_;
    }
    int clipCount() const {
        if (!world_ || !document_.contains("animations")) {
            return 0;
        }
        return int(document_.at("animations").size());
    }
    std::string clipName(int index) const {
        if (index < 0 || !document_.contains("animations") ||
            index >= int(document_.at("animations").size())) {
            return {};
        }
        return document_.at("animations").at(index).value("name", std::string{});
    }
    float clipDuration(int index) const {
        if (index < 0 || !document_.contains("animations") ||
            index >= int(document_.at("animations").size())) {
            return 0;
        }
        return document_.at("animations").at(index).value("duration", 0.0f);
    }
    int clip() const {
        return clip_;
    }
    void setClip(int index) {
        if (index < 0 || index >= clipCount() || index == clip_) {
            return;
        }
        auto& a = world_->get<Animation>(owner_);
        const float duration = clipDuration(clip_);
        if (blendClips_ && playing_ && fadeSeconds_ > 0) {
            if (a.blendWeight < 1) {
                auto source = a.blendFrozenPose.empty()
                                  ? modelLocalTRS(document_, a.blendSourceAnimation,
                                                  a.blendSourceTime)
                                  : a.blendFrozenPose;
                auto target = modelLocalTRS(document_, index, time_);
                for (std::size_t n = 0; n < target.size(); ++n) {
                    const float w = a.blendWeight;
                    target[n] = {
                        source[n].position + (target[n].position - source[n].position) * w,
                        source[n].scale + (target[n].scale - source[n].scale) * w,
                        nlerp(source[n].rotation, target[n].rotation, w)};
                }
                a.blendFrozenPose = std::move(target);
            } else {
                a.blendFrozenPose.clear();
            }
            a.blendSourceAnimation = bind_ ? -1 : clip_;
            a.blendSourceTime = time_;
            sourceDuration_ = duration;
            fadeElapsed_ = 0;
            fadeDuration_ = std::min(fadeSeconds_, clipDuration(index));
            a.blendWeight = 0;
        } else {
            a.blendWeight = 1;
            a.blendFrozenPose.clear();
            fadeDuration_ = 0;
        }
        clip_ = index;
        time_ = 0;
        bind_ = false;
    }
    float time() const {
        return time_;
    }
    void setTime(float seconds) {
        time_ = std::clamp(seconds, 0.0f, std::max(clipDuration(clip_), .001f));
        bind_ = false;
        playing_ = false;
        fadeDuration_ = 0;
        autoBlendReset();
    }
    bool playing() const {
        return playing_;
    }
    void play() {
        if (time_ >= clipDuration(clip_)) {
            time_ = 0;
        }
        playing_ = true;
        bind_ = false;
    }
    void pause() {
        playing_ = false;
    }
    void stop() {
        fadeDuration_ = 0;
        autoBlendReset();
        playing_ = false;
        time_ = 0;
        bind_ = false;
    }
    void resetPose() {
        fadeDuration_ = 0;
        autoBlendReset();
        playing_ = false;
        time_ = 0;
        bind_ = true;
    }
    bool loop() const {
        return loop_;
    }
    void setLoop(bool loop) {
        loop_ = loop;
    }
    bool blendClips() const {
        return blendClips_;
    }
    void setBlendClips(bool blend) {
        blendClips_ = blend;
    }
    float fadeSeconds() const {
        return fadeSeconds_;
    }
    void setFadeSeconds(float seconds) {
        fadeSeconds_ = std::clamp(seconds, 0.0f, 5.0f);
    }

  private:
    void autoBlendReset() {
        if (!world_ || !world_->alive(owner_)) {
            return;
        }
        auto& a = world_->get<Animation>(owner_);
        a.blendWeight = 1;
        a.blendFrozenPose.clear();
    }
};
