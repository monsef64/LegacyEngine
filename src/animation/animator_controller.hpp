#pragma once
#include "animation/animator_component.hpp"
#include "assets/assets.hpp"
#include "project/json_io.hpp"
#include <cmath>
#include <mutex>
#include <set>

namespace Animators {
inline bool controllerAsset(const std::string& path) {
    if (path.empty() || path.size() > 4096 || std::filesystem::path(path).is_absolute() ||
        path.find('\0') != path.npos) {
        return false;
    }
    for (const auto& part : std::filesystem::path(path)) {
        if (part == "..") {
            return false;
        }
    }
    return AssetManager::extension(path) == ".controller";
}
inline bool nameValid(const std::string& name) {
    return !name.empty() && name.size() <= 128 && name.find('\0') == name.npos;
}
inline const AnimatorState* findState(const AnimatorController& c, const std::string& name) {
    for (const auto& s : c.states) {
        if (s.id == name || s.name == name) {
            return &s;
        }
    }
    return nullptr;
}
inline Json settings(const Animator& a) {
    return {{"controller", a.controller}, {"enabled", a.enabled}, {"playOnStart", a.playOnStart}};
}
inline Animator parse(const Json& j) {
    Animator a;
    a.controller = j.value("controller", std::string{});
    a.enabled = j.value("enabled", true);
    a.playOnStart = j.value("playOnStart", true);
    if (!a.controller.empty() && !controllerAsset(a.controller)) {
        throw std::runtime_error("Expected a project-relative .controller asset");
    }
    return a;
}
inline Json document(const AnimatorController& c) {
    Json j{{"format", "LegacyAnimator"},
           {"version", 1},
           {"entry", c.entry},
           {"parameters", Json::array()},
           {"states", Json::array()},
           {"transitions", Json::array()}};
    for (const auto& p : c.parameters) {
        j["parameters"].push_back({{"name", p.name},
                                   {"type",
                                    p.value.type == AnimatorParameterType::Bool  ? "Bool"
                                    : p.value.type == AnimatorParameterType::Int ? "Int"
                                                                                 : "Float"},
                                   {"default", p.value.number}});
    }
    for (const auto& s : c.states) {
        j["states"].push_back({{"id", s.id},
                               {"name", s.name},
                               {"clip", s.clip},
                               {"loop", s.loop},
                               {"x", s.x},
                               {"y", s.y}});
    }
    for (const auto& t : c.transitions) {
        Json conditions = Json::array();
        for (const auto& q : t.conditions) {
            conditions.push_back(
                {{"parameter", q.parameter}, {"comparison", q.comparison}, {"value", q.value}});
        }
        j["transitions"].push_back({{"from", t.from},
                                    {"to", t.to},
                                    {"fade", t.fade},
                                    {"hasExitTime", t.hasExitTime},
                                    {"exitTime", t.exitTime},
                                    {"conditions", conditions}});
    }
    return j;
}
inline AnimatorController parseController(const Json& j) {
    if (j.at("format") != "LegacyAnimator" || j.at("version") != 1) {
        throw std::runtime_error("Unsupported Animator controller format");
    }
    if (!j.at("states").is_array() || !j.at("parameters").is_array() ||
        !j.at("transitions").is_array() || j.at("states").size() > 256 ||
        j.at("parameters").size() > 64 || j.at("transitions").size() > 1024) {
        throw std::runtime_error("Animator controller exceeds graph limits");
    }
    AnimatorController c;
    c.entry = j.value("entry", std::string{});
    std::set<std::string> names, ids;
    for (const auto& p : j.at("parameters")) {
        AnimatorParameter parameter;
        parameter.name = p.at("name").get<std::string>();
        const auto type = p.at("type").get<std::string>();
        if (type != "Bool" && type != "Int" && type != "Float") {
            throw std::runtime_error("Unknown Animator parameter type");
        }
        parameter.value.type = type == "Bool"  ? AnimatorParameterType::Bool
                               : type == "Int" ? AnimatorParameterType::Int
                                               : AnimatorParameterType::Float;
        parameter.value.number = p.value("default", 0.0);
        const auto v = parameter.value.number;
        if (!nameValid(parameter.name) || !names.insert(parameter.name).second ||
            !std::isfinite(v) || std::abs(v) > 1e6 || (type == "Bool" && v != 0 && v != 1) ||
            (type == "Int" && std::floor(v) != v)) {
            throw std::runtime_error("Invalid or duplicate Animator parameter");
        }
        c.parameters.push_back(std::move(parameter));
    }
    names.clear();
    for (const auto& s : j.at("states")) {
        AnimatorState state;
        state.id = s.at("id").get<std::string>();
        state.name = s.at("name").get<std::string>();
        state.clip = s.at("clip").get<std::string>();
        state.loop = s.value("loop", true);
        state.x = s.value("x", 220.f);
        state.y = s.value("y", 0.f);
        if (!nameValid(state.id) || state.id == "*" || !nameValid(state.name) ||
            state.name == "*" || !ids.insert(state.id).second || !names.insert(state.name).second ||
            !std::isfinite(state.x) || !std::isfinite(state.y) || std::abs(state.x) > 100000 ||
            std::abs(state.y) > 100000) {
            throw std::runtime_error("Invalid or duplicate Animator state");
        }
        // Validate clip paths without touching disk. Empty drafts cannot be saved.
        if (state.clip.empty() || std::filesystem::path(state.clip).is_absolute() ||
            state.clip.size() > 4096 || state.clip.find('\0') != state.clip.npos ||
            AssetManager::extension(state.clip) != ".leanim") {
            throw std::runtime_error("Each Animator state needs a project-relative .leanim clip");
        }
        for (const auto& part : std::filesystem::path(state.clip)) {
            if (part == "..") {
                throw std::runtime_error("Animation clip escapes project assets");
            }
        }
        c.states.push_back(std::move(state));
    }
    // Avoid an ID accidentally resolving to a different state's display name.
    for (const auto& a : c.states) {
        for (const auto& b : c.states) {
            if (a.id != b.id && a.id == b.name) {
                throw std::runtime_error("State names cannot shadow another state ID");
            }
        }
    }
    if ((!c.states.empty() && !ids.count(c.entry)) || (c.states.empty() && !c.entry.empty())) {
        throw std::runtime_error("Animator entry must refer to a state");
    }
    for (const auto& t : j.at("transitions")) {
        AnimatorTransition transition;
        transition.from = t.at("from").get<std::string>();
        transition.to = t.at("to").get<std::string>();
        transition.fade = t.value("fade", .2f);
        transition.hasExitTime = t.value("hasExitTime", true);
        transition.exitTime = t.value("exitTime", 1.f);
        if ((!ids.count(transition.from) && transition.from != "*") || !ids.count(transition.to) ||
            !std::isfinite(transition.fade) || transition.fade < 0 || transition.fade > 3600 ||
            !std::isfinite(transition.exitTime) || transition.exitTime < 0 ||
            transition.exitTime > 1) {
            throw std::runtime_error("Invalid Animator transition");
        }
        const auto conditions = t.value("conditions", Json::array());
        if (!conditions.is_array() || conditions.size() > 32) {
            throw std::runtime_error("Too many transition conditions");
        }
        for (const auto& q : conditions) {
            AnimatorCondition condition;
            condition.parameter = q.at("parameter").get<std::string>();
            condition.comparison = q.value("comparison", std::string("=="));
            condition.value = q.value("value", 0.0);
            const auto parameter =
                std::find_if(c.parameters.begin(), c.parameters.end(), [&](const auto& p) {
                    return p.name == condition.parameter;
                });
            const auto& op = condition.comparison;
            if (parameter == c.parameters.end() ||
                (op != "==" && op != "!=" && op != "<" && op != ">" && op != "<=" && op != ">=") ||
                !std::isfinite(condition.value) || std::abs(condition.value) > 1e6) {
                throw std::runtime_error("Invalid Animator condition");
            }
            if ((parameter->value.type == AnimatorParameterType::Bool &&
                 (op != "==" && op != "!=")) ||
                (parameter->value.type == AnimatorParameterType::Bool && condition.value != 0 &&
                 condition.value != 1) ||
                (parameter->value.type == AnimatorParameterType::Int &&
                 std::floor(condition.value) != condition.value)) {
                throw std::runtime_error("Condition does not match parameter type");
            }
            transition.conditions.push_back(std::move(condition));
        }
        c.transitions.push_back(std::move(transition));
    }
    return c;
}
inline std::shared_ptr<const AnimatorController> loadController(const AssetManager& assets,
                                                                const std::string& path) {
    struct Cached {
        std::filesystem::file_time_type stamp;
        std::uintmax_t size;
        std::shared_ptr<const AnimatorController> definition;
    };
    static std::mutex mutex;
    static std::map<std::string, Cached> cache;
    const auto file = assets.resolve(path);
    const auto key = file.generic_string();
    const auto stamp = std::filesystem::last_write_time(file);
    const auto bytes = std::filesystem::file_size(file);
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto found = cache.find(key);
        if (found != cache.end() && found->second.stamp == stamp && found->second.size == bytes) {
            return found->second.definition;
        }
    }
    auto definition =
        std::make_shared<AnimatorController>(parseController(readJsonFile(file, 1024 * 1024)));
    std::lock_guard<std::mutex> lock(mutex);
    if (cache.size() >= 64) {
        cache.erase(cache.begin());
    }
    cache.insert_or_assign(key, Cached{stamp, bytes, definition});
    return definition;
}
inline void prepare(Animator& a, const AssetManager& assets) {
    const auto key = assets.root.generic_string() + "|" + a.controller;
    if (a.loadedKey == key && a.definition) {
        return;
    }
    if (!controllerAsset(a.controller)) {
        throw std::runtime_error("Assign an Animator .controller asset");
    }
    a.definition = loadController(assets, a.controller);
    a.loadedKey = key;
    a.values.clear();
    for (const auto& p : a.definition->parameters) {
        a.values.emplace(p.name, p.value);
    }
    a.restartBackend = true;
    a.playing = a.paused = false;
    a.initialized = false;
    a.state.clear();
    a.stateTime = a.transitionRemaining = 0;
    a.error.clear();
}
inline void
setParameter(Animator& a, const std::string& name, AnimatorParameterType type, double value) {
    auto found = a.values.find(name);
    if (found == a.values.end() || found->second.type != type) {
        throw std::runtime_error("Animator parameter is missing or has a different type: " + name);
    }
    if (!std::isfinite(value) || std::abs(value) > 1e6 ||
        (type == AnimatorParameterType::Int && std::floor(value) != value)) {
        throw std::runtime_error("Invalid Animator parameter value");
    }
    found->second.number = value;
}
inline bool conditionMet(const Animator& a, const AnimatorCondition& c) {
    const auto found = a.values.find(c.parameter);
    if (found == a.values.end()) {
        return false;
    }
    const auto v = found->second.number;
    return c.comparison == "=="   ? v == c.value
           : c.comparison == "!=" ? v != c.value
           : c.comparison == "<"  ? v < c.value
           : c.comparison == ">"  ? v > c.value
           : c.comparison == "<=" ? v <= c.value
                                  : v >= c.value;
}
} // namespace Animators
