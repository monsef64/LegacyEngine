#pragma once
#include "animation/animator_controller.hpp"
#include "core/hierarchy.hpp"

namespace Animators {
inline void update(World& world, const AssetManager& assets, float /*dt*/) {
    world.each<Animator>([&](Entity owner, Animator& a) {
        auto* backend = world.tryGet<Animation>(owner);
        if (!a.enabled || !isActive(world, owner)) {
            if (backend && a.playing && !a.paused && !a.suspended) {
                backend->request(AnimationAction::Pause);
                a.suspended = true;
            }
            return;
        }
        try {
            prepare(a, assets);
            if (!backend) {
                if (a.playing && !a.state.empty() && a.commands.empty()) {
                    a.request(AnimatorAction::Play, a.state);
                }
                a.initialized = false;
                backend = &world.add(owner, Animation{});
            }
            auto& animation = *backend;
            if (a.restartBackend) {
                animation.request(AnimationAction::Stop);
                a.restartBackend = false;
            }
            if (a.suspended) {
                if (a.playing && !a.paused) {
                    animation.request(AnimationAction::Resume);
                }
                a.suspended = false;
            }
            auto enter = [&](const std::string& name, float fade) {
                const auto* state = findState(*a.definition, name);
                if (!state) {
                    throw std::runtime_error("Unknown Animator state: " + name);
                }
                if (!std::isfinite(fade) || fade < 0 || fade > 3600) {
                    throw std::runtime_error("Animator fade must be 0..3600 seconds");
                }
                // A new state supersedes older asynchronous load requests. At most
                // one state entry is issued per frame; scripts cannot flood the backend.
                animation.commands.clear();
                if (!animation.request(fade > 0 ? AnimationAction::CrossFade
                                                : AnimationAction::PlayClip,
                                       state->clip,
                                       fade,
                                       state->loop ? 1 : 0)) {
                    throw std::runtime_error("Animation command queue is full");
                }
                a.playbackStarted = false;
                a.state = state->id;
                a.stateTime = 0;
                a.transitionRemaining = fade;
                a.playing = true;
                a.paused = false;
            };
            bool entered = false;
            if (!a.initialized) {
                a.initialized = true;
                const bool explicitEntry =
                    std::any_of(a.commands.begin(), a.commands.end(), [](const auto& c) {
                        return c.action == AnimatorAction::Play ||
                               c.action == AnimatorAction::CrossFade ||
                               c.action == AnimatorAction::Stop;
                    });
                if (a.playOnStart && !explicitEntry && !a.definition->entry.empty()) {
                    enter(a.definition->entry, 0);
                    entered = true;
                }
            }
            auto commands = std::move(a.commands);
            a.commands.clear();
            // Preserve stop/pause ordering while taking only the last explicit
            // state request; issuing several Play commands cannot queue old clips.
            for (std::size_t i = 0; i < commands.size(); ++i) {
                const auto& command = commands[i];
                if (command.action == AnimatorAction::Play ||
                    command.action == AnimatorAction::CrossFade) {
                    bool later = false;
                    for (auto j = i + 1; j < commands.size(); ++j) {
                        if (commands[j].action == AnimatorAction::Play ||
                            commands[j].action == AnimatorAction::CrossFade) {
                            later = true;
                            break;
                        }
                    }
                    if (!later) {
                        enter(command.state,
                              command.action == AnimatorAction::Play ? 0 : command.fade);
                        entered = true;
                    }
                } else if (command.action == AnimatorAction::Stop) {
                    animation.request(AnimationAction::Stop);
                    a.playing = a.paused = false;
                    a.stateTime = a.transitionRemaining = 0;
                } else if (command.action == AnimatorAction::Pause) {
                    if (a.playing) {
                        animation.request(AnimationAction::Pause);
                        a.paused = true;
                    }
                } else if (a.playing && a.paused) {
                    animation.request(AnimationAction::Resume);
                    a.paused = false;
                }
            }
            if (!entered && a.playing && !a.paused && !a.state.empty()) {
                const auto* state = findState(*a.definition, a.state);
                const bool ready = state && animation.clip == state->clip &&
                                   animation.commands.empty() && animation.error.empty() &&
                                   animation.duration > 0;
                if (ready) {
                    // Read the backend's completed playback sample, not this
                    // frame's proposed delta: exit times must not fire early.
                    if (!a.playbackStarted) {
                        a.playbackStarted = true;
                        a.loopSequence = animation.eventSequence[1];
                        a.stateTime = animation.time;
                    } else {
                        const auto loops =
                            std::min(256u, animation.eventSequence[1] - a.loopSequence);
                        const float elapsed =
                            animation.time - a.lastPlaybackTime + loops * animation.duration;
                        a.stateTime = std::min(1e9f, a.stateTime + std::max(0.f, elapsed));
                        a.loopSequence = animation.eventSequence[1];
                    }
                    a.lastPlaybackTime = animation.time;
                    if (animation.blendWeight >= 1) {
                        a.transitionRemaining = 0;
                        for (const auto& t : a.definition->transitions) {
                            if (t.from != a.state && t.from != "*") {
                                continue;
                            }
                            // Any State does not continually restart its destination.
                            if (t.from == "*" && t.to == a.state) {
                                continue;
                            }
                            if (t.hasExitTime &&
                                a.stateTime + .00001f < animation.duration * t.exitTime) {
                                continue;
                            }
                            if (!std::all_of(t.conditions.begin(),
                                             t.conditions.end(),
                                             [&](const auto& c) { return conditionMet(a, c); })) {
                                continue;
                            }
                            enter(t.to, t.fade);
                            break;
                        }
                    }
                }
            }
            if (!animation.error.empty()) {
                a.error = animation.error;
            } else {
                a.error.clear();
            }
        } catch (const std::exception& e) {
            a.error = e.what();
            if (backend && (a.playing || backend->isPlaying || backend->paused)) {
                backend->request(AnimationAction::Stop);
            }
            a.playing = a.paused = false;
        }
    });
}
} // namespace Animators
