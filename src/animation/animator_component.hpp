#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

enum class AnimatorParameterType { Bool, Int, Float };
struct AnimatorValue {
    AnimatorParameterType type = AnimatorParameterType::Bool;
    double number = 0;
};
struct AnimatorParameter {
    std::string name;
    AnimatorValue value;
};
struct AnimatorState {
    std::string id, name, clip;
    bool loop = true;
    float x = 220, y = 0;
};
struct AnimatorCondition {
    std::string parameter, comparison = "==";
    double value = 0;
};
struct AnimatorTransition {
    std::string from, to;
    float fade = .2f;
    bool hasExitTime = true;
    float exitTime = 1;
    std::vector<AnimatorCondition> conditions;
};
struct AnimatorController {
    std::string entry;
    std::vector<AnimatorParameter> parameters;
    std::vector<AnimatorState> states;
    std::vector<AnimatorTransition> transitions;
};
enum class AnimatorAction { Play, CrossFade, Stop, Pause, Resume };
struct AnimatorCommand {
    AnimatorAction action;
    std::string state;
    float fade = 0;
};
struct Animator {
    bool enabled = true, playOnStart = true;
    std::string controller;
    // Runtime-only. Controller definitions are immutable; parameters are per instance.
    std::shared_ptr<const AnimatorController> definition;
    std::string loadedKey, state, error;
    std::map<std::string, AnimatorValue> values;
    std::vector<AnimatorCommand> commands;
    bool initialized = false, playing = false, paused = false, suspended = false;
    float stateTime = 0, transitionRemaining = 0, lastPlaybackTime = 0;
    unsigned loopSequence = 0;
    bool playbackStarted = false, restartBackend = false;
    bool request(AnimatorAction action, std::string target = {}, float fade = 0) {
        if (commands.size() >= 64) {
            return false;
        }
        commands.push_back({action, std::move(target), fade});
        return true;
    }
};
