#pragma once
#include "animation/animation_clip.hpp"
#include <map>

class AnimationSystem {
    enum class Phase { Stopped, Playing, Paused, Finished };
    struct Original {
        Entity target;
        std::string property;
        AnimationValue value{};
    };
    struct State {
        std::string asset;
        AnimationClip clip;
        std::vector<Original> originals, fadeValues;
        Phase phase = Phase::Stopped;
        bool loaded = false, recapture = false;
        float fadeTime = 0, fadeDuration = 0, sourceDuration = 1;
        bool sourceLoop = false;
    };
    std::map<std::pair<unsigned, unsigned>, State> states_;
    std::filesystem::path root_;
    static void restore(World& world, State& state) {
        for (const auto& o : state.originals) {
            if (const auto* p = animationProperty(o.property)) {
                writeAnimationValue(world, o.target, *p, o.value);
            }
        }
        updateHierarchy(world);
    }
    static std::vector<Original> capture(World& world, Entity owner, const AnimationClip& clip) {
        std::vector<Original> result;
        for (const auto& track : clip.tracks) {
            auto target = animationTarget(world, owner, track.target);
            AnimationValue value{};
            if (!readAnimationValue(world, target, *animationProperty(track.property), value)) {
                throw std::runtime_error("Animation target/component missing: " + track.target +
                                         " / " + track.property);
            }
            result.push_back({target, track.property, value});
        }
        return result;
    }
    static void remember(State& state, const std::vector<Original>& values) {
        for (const auto& v : values) {
            bool found = false;
            for (const auto& o : state.originals) {
                found |= o.target == v.target && o.property == v.property;
            }
            if (!found) {
                state.originals.push_back(v);
            }
        }
    }
    static void cancelFade(Animation& animation, State& state) {
        state.fadeDuration = 0;
        state.fadeTime = 0;
        state.fadeValues.clear();
        animation.blendWeight = 1;
        animation.blendFrozenPose.clear();
    }
    static void apply(World& world, Entity owner, Animation& animation, State& state) {
        applyAnimationClip(world, owner, state.clip, animation.time);
        if (state.fadeDuration <= 0) {
            return;
        }
        const float weight = animation.blendWeight;
        for (const auto& from : state.fadeValues) {
            const auto* property = animationProperty(from.property);
            AnimationValue to{};
            if (!readAnimationValue(world, from.target, *property, to)) {
                continue;
            }
            if (property->kind == AnimationValueKind::Quaternion) {
                auto q = nlerp(Quat{from.value[0], from.value[1], from.value[2], from.value[3]},
                               Quat{to[0], to[1], to[2], to[3]},
                               weight);
                to = {q.x, q.y, q.z, q.w};
            } else if (property->kind == AnimationValueKind::Boolean) {
                if (weight < .5f) {
                    to = from.value;
                }
            } else {
                for (int i = 0; i < property->lanes(); ++i) {
                    to[i] = from.value[i] + (to[i] - from.value[i]) * weight;
                }
            }
            writeAnimationValue(world, from.target, *property, to);
        }
        updateHierarchy(world);
    }

  public:
    struct Event {
        Entity owner;
        const char* name;
    };
    std::vector<Event> events;
    void clear() {
        states_.clear();
        events.clear();
        root_.clear();
    }
    void update(World& world, const AssetManager& assets, float seconds) {
        events.clear();
        if (root_ != assets.root) {
            states_.clear();
            root_ = assets.root;
        }
        seconds = std::isfinite(seconds) ? std::clamp(seconds, 0.f, .25f) : 0;
        world.each<Animation>([&](Entity owner, Animation& animation) {
            auto& state = states_[{owner.index, owner.generation}];
            if (!animation.started) {
                animation.started = true;
                if (animation.playOnStart && animation.commands.empty()) {
                    animation.request(AnimationAction::Play);
                }
            }
            if (state.asset != animation.clip) {
                if (state.loaded) {
                    restore(world, state);
                }
                state = State{};
                state.asset = animation.clip;
                animation.time = 0;
                animation.poseActive = false;
                animation.blendWeight = 1;
                animation.blendFrozenPose.clear();
            }
            auto commands = std::move(animation.commands);
            animation.commands.clear();
            bool began = false;
            auto emit = [&](const char* name) { events.push_back({owner, name}); };
            for (const auto& command : commands) {
                try {
                    if (command.action == AnimationAction::CrossFade) {
                        auto next = loadAnimationClip(assets, command.clip);
                        if (!std::isfinite(command.duration) || command.duration < 0 ||
                            command.duration > 3600) {
                            throw std::runtime_error("Cross-fade duration must be 0..3600 seconds");
                        }
                        if (state.loaded && state.clip.model != next.model) {
                            throw std::runtime_error(
                                "Cross-fade requires clips for the same model (or two property clips)");
                        }
                        auto values = capture(world, owner, next);
                        const bool fading =
                            state.loaded && animation.poseActive && animation.blendWeight < 1;
                        std::vector<AnimationPose> frozen;
                        if (fading && !next.model.empty()) {
                            const auto doc = modelDocument(assets, next.model);
                            auto from = animation.blendFrozenPose.empty()
                                            ? modelLocalTRS(doc,
                                                            animation.blendSourceAnimation,
                                                            animation.blendSourceTime)
                                            : animation.blendFrozenPose;
                            auto to = modelLocalTRS(doc, animation.modelAnimation, animation.time);
                            for (std::size_t i = 0; i < to.size(); ++i) {
                                const float t = animation.blendWeight;
                                to[i] = {from[i].position + (to[i].position - from[i].position) * t,
                                         from[i].scale + (to[i].scale - from[i].scale) * t,
                                         nlerp(from[i].rotation, to[i].rotation, t)};
                            }
                            frozen = std::move(to);
                        }
                        animation.blendSourceAnimation =
                            animation.poseActive ? animation.modelAnimation : -1;
                        animation.blendSourceTime = animation.time;
                        animation.blendFrozenPose = std::move(frozen);
                        state.sourceDuration = state.loaded ? state.clip.duration : 1;
                        state.sourceLoop = animation.loop;
                        if (state.recapture) {
                            state.originals.clear();
                        }
                        remember(state, values);
                        state.fadeValues = std::move(values);
                        state.fadeTime = 0;
                        state.fadeDuration = std::min(command.duration, next.duration);
                        animation.blendWeight = state.fadeDuration > 0 ? 0 : 1;
                        state.clip = std::move(next);
                        state.loaded = true;
                        state.recapture = false;
                        state.asset = animation.clip = command.clip;
                        state.phase = Phase::Playing;
                        animation.time = 0;
                        animation.error.clear();
                        apply(world, owner, animation, state);
                        emit("onAnimationStart");
                        began = true;
                        continue;
                    }
                    if (!state.loaded && (command.action == AnimationAction::Play ||
                                          command.action == AnimationAction::Stop)) {
                        auto clip = loadAnimationClip(assets, animation.clip);
                        auto originals = capture(world, owner, clip);
                        state.clip = std::move(clip);
                        state.originals = std::move(originals);
                        state.loaded = true;
                    }
                    switch (command.action) {
                    case AnimationAction::Play:
                        if (state.recapture) {
                            state.originals = capture(world, owner, state.clip);
                            state.recapture = false;
                        }
                        cancelFade(animation, state);
                        state.phase = Phase::Playing;
                        animation.time = 0;
                        apply(world, owner, animation, state);
                        emit("onAnimationStart");
                        began = true;
                        break;
                    case AnimationAction::Pause:
                        if (state.phase == Phase::Playing) {
                            state.phase = Phase::Paused;
                            emit("onAnimationPause");
                        }
                        break;
                    case AnimationAction::Resume:
                        if (state.phase == Phase::Paused) {
                            state.phase = Phase::Playing;
                            emit("onAnimationResume");
                        }
                        break;
                    case AnimationAction::Stop: {
                        bool active = state.phase == Phase::Playing || state.phase == Phase::Paused;
                        cancelFade(animation, state);
                        state.phase = Phase::Stopped;
                        animation.time = 0;
                        if (state.loaded) {
                            apply(world, owner, animation, state);
                        }
                        if (active) {
                            emit("onAnimationStopped");
                        }
                        break;
                    }
                    case AnimationAction::Reset:
                        cancelFade(animation, state);
                        state.phase = Phase::Stopped;
                        animation.time = 0;
                        restore(world, state);
                        animation.poseActive = false;
                        state.recapture = true;
                        break;
                    case AnimationAction::CrossFade:
                        break;
                    }
                    animation.error.clear();
                } catch (const std::exception& e) {
                    animation.error = e.what();
                }
            }
            if (state.loaded && state.phase == Phase::Playing && !began) {
                animation.time += seconds;
                if (state.fadeDuration > 0) {
                    state.fadeTime += seconds;
                    animation.blendWeight = std::min(1.f, state.fadeTime / state.fadeDuration);
                    animation.blendSourceTime += seconds;
                    if (animation.blendSourceTime > state.sourceDuration) {
                        animation.blendSourceTime =
                            state.sourceLoop
                                ? std::fmod(animation.blendSourceTime, state.sourceDuration)
                                : state.sourceDuration;
                    }
                }
                if (animation.time >= state.clip.duration) {
                    if (animation.loop) {
                        animation.time = std::fmod(animation.time, state.clip.duration);
                    } else {
                        animation.time = state.clip.duration;
                        state.phase = Phase::Finished;
                        emit("onAnimationFinish");
                    }
                }
                apply(world, owner, animation, state);
                if (animation.blendWeight >= 1) {
                    cancelFade(animation, state);
                }
            }
            animation.isPlaying = state.phase == Phase::Playing;
            animation.paused = state.phase == Phase::Paused;
        });
        for (auto it = states_.begin(); it != states_.end();) {
            Entity owner{it->first.first, it->first.second};
            if (!world.alive(owner) || !world.has<Animation>(owner)) {
                it = states_.erase(it);
            } else {
                ++it;
            }
        }
    }
};
