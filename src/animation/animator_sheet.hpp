#pragma once
#include "animation/animation_clip.hpp"
#include "animation/animator.hpp"

namespace Animators {
inline bool asset(const std::string& path) {
    return materialPathValid(path) && AssetManager::extension(path) == ".animatorsheet";
}
inline Json emptySheet() {
    return {{"format", "LegacyEngineAnimatorSheet"},
            {"version", 1},
            {"entry", "Idle"},
            {"parameters", Json::array()},
            {"states", Json::array({{{"name", "Idle"}, {"clip", ""}, {"loop", true}}})},
            {"transitions", Json::array()}};
}
inline void validate(const Json& doc) {
    if (doc.at("format") != "LegacyEngineAnimatorSheet" || doc.at("version") != 1 ||
        !doc.at("states").is_array() || doc.at("states").empty() || doc.at("states").size() > 128 ||
        !doc.at("parameters").is_array() || doc.at("parameters").size() > 64 ||
        !doc.at("transitions").is_array() || doc.at("transitions").size() > 256) {
        throw std::runtime_error("Invalid AnimatorSheet structure");
    }
    std::set<std::string> states;
    std::map<std::string, std::string> parameters;
    auto name = [](const std::string& n) { return !n.empty() && n.size() <= 64 && n != "*"; };
    for (const auto& p : doc.at("parameters")) {
        const auto n = p.at("name").get<std::string>(), type = p.at("type").get<std::string>();
        if (!name(n) || parameters.count(n) ||
            (type != "float" && type != "bool" && type != "trigger")) {
            throw std::runtime_error("Invalid animator parameter");
        }
        const auto value = p.at("default");
        if (type == "float" ? (!value.is_number() || !std::isfinite(value.get<double>()) ||
                               std::abs(value.get<double>()) > 1e6)
                            : !value.is_boolean()) {
            throw std::runtime_error("Invalid parameter default");
        }
        parameters[n] = type;
    }
    for (const auto& s : doc.at("states")) {
        const auto n = s.at("name").get<std::string>(), clip = s.at("clip").get<std::string>();
        if (!name(n) || !states.insert(n).second || (!clip.empty() && !animationAsset(clip)) ||
            !s.at("loop").is_boolean()) {
            throw std::runtime_error("Invalid animator state");
        }
    }
    if (!states.count(doc.at("entry").get<std::string>())) {
        throw std::runtime_error("Animator entry state not found");
    }
    for (const auto& t : doc.at("transitions")) {
        const auto from = t.at("from").get<std::string>(), to = t.at("to").get<std::string>();
        const float duration = t.value("duration", .2f), exit = t.value("exitTime", -1.f);
        if ((from != "*" && !states.count(from)) || !states.count(to) || !std::isfinite(duration) ||
            duration < 0 || duration > 30 || !std::isfinite(exit) || exit < -1 || exit > 1 ||
            !t.at("conditions").is_array() || t.at("conditions").size() > 8) {
            throw std::runtime_error("Invalid animator transition");
        }
        for (const auto& c : t.at("conditions")) {
            const auto p = c.at("parameter").get<std::string>(), op = c.at("op").get<std::string>();
            if (!parameters.count(p) ||
                (op != "==" && op != "!=" && op != ">" && op != "<" && op != "trigger")) {
                throw std::runtime_error("Invalid transition condition");
            }
            if (op == "trigger") {
                if (parameters.at(p) != "trigger") {
                    throw std::runtime_error("Trigger condition requires trigger parameter");
                }
            } else if (parameters.at(p) == "float") {
                if (!c.at("value").is_number() || !std::isfinite(c.at("value").get<double>())) {
                    throw std::runtime_error("Condition requires number");
                }
            } else if (!c.at("value").is_boolean() || (op != "==" && op != "!=")) {
                throw std::runtime_error("Boolean condition requires == or !=");
            }
        }
    }
}
inline std::string create(const AssetManager& assets, const std::string& folder) {
    auto base = (std::filesystem::path(folder) / "AnimatorSheet").generic_string();
    auto path = base + ".animatorsheet";
    for (unsigned n = 2; std::filesystem::exists(assets.resolve(path)); ++n) {
        path = base + "_" + std::to_string(n) + ".animatorsheet";
    }
    writeJsonFile(assets.resolve(path), emptySheet());
    return path;
}
} // namespace Animators
