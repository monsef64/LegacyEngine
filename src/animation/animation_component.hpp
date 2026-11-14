#pragma once
#include "core/math.hpp"
#include <string>
#include <vector>

enum class AnimationAction { Play, Stop, Pause, Resume, Reset, CrossFade };
struct AnimationCommand {
    AnimationAction action;
    std::string clip;
    float duration = 0;
};
struct AnimationPose {
    Vec3 position{}, scale{1, 1, 1};
    Quat rotation{};
};
struct Animation {
    std::string clip;
    bool loop = false, playOnStart = false;
    // Runtime state is intentionally not serialized.
    bool isPlaying = false, paused = false, started = false;
    float time = 0;
    std::string model;
    int modelAnimation = -1;
    bool poseActive = false;
    std::string error;
    int blendSourceAnimation = -1;
    float blendSourceTime = 0, blendWeight = 1;
    std::vector<AnimationPose> blendFrozenPose;
    std::vector<AnimationCommand> commands;
    bool request(AnimationAction action, const std::string& target = {}, float duration = 0) {
        if (commands.size() >= 64) {
            return false;
        }
        commands.push_back({action, target, duration});
        if (action == AnimationAction::Play || action == AnimationAction::Resume ||
            action == AnimationAction::CrossFade) {
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
