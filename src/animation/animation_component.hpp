#pragma once
#include "core/math.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

enum class AnimationAction { Play, Stop, Pause, Resume, Reset, CrossFade, PlayClip };
struct AnimationCommand {
    AnimationAction action;
    std::string clip;
    float duration = 0;
    int loop = -1; // Optional destination loop override; preserve source loop while fading.
};
struct AnimationPose {
    Vec3 position{}, scale{1, 1, 1};
    Quat rotation{};
};
struct Animation {
    inline static constexpr const char* eventNames[]{
        "onFinished", "onRestarted", "onStopped", "onStarted", "onPaused", "onResumed"};
    std::string clip;
    bool loop = false, playOnStart = false;
    // Runtime state is intentionally not serialized.
    bool isPlaying = false, paused = false, started = false;
    float time = 0, duration = 0;
    std::string model;
    int modelAnimation = -1;
    bool poseActive = false;
    std::string error;
    int blendSourceAnimation = -1;
    float blendSourceTime = 0, blendWeight = 1;
    std::vector<AnimationPose> blendFrozenPose;
    std::vector<AnimationCommand> commands;
    std::array<unsigned, 6> eventSequence{};
    inline static std::atomic<std::uint64_t> nextEventInstance{1};
    std::uint64_t eventInstance = nextEventInstance.fetch_add(1, std::memory_order_relaxed);
    bool request(AnimationAction action,
                 const std::string& target = {},
                 float duration = 0,
                 int loop = -1) {
        if (action == AnimationAction::Stop || action == AnimationAction::Reset) {
            commands.clear();
        }
        if (commands.size() >= 64) {
            return false;
        }
        commands.push_back({action, target, duration, loop});
        if (action == AnimationAction::Play || action == AnimationAction::Resume ||
            (action == AnimationAction::CrossFade || action == AnimationAction::PlayClip)) {
            isPlaying = true;
            paused = false;
        } else if (action == AnimationAction::Pause) {
            isPlaying = false;
            paused = true;
        } else {
            isPlaying = false;
            paused = false;
        }
        return true;
    }
};
