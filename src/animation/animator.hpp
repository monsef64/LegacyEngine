#pragma once
#include "project/json_io.hpp"
#include <map>
#include <string>
struct Animator {
    bool enabled = true;
    std::string sheet;
    // Runtime only; the sheet asset remains immutable during Play.
    std::string currentState, pendingState, loadedSheet, error;
    float elapsed = 0, pendingFade = .2f, stateDuration = 1;
    bool initialized = false;
    unsigned sheetRevision = 0;
    std::map<std::string, Json> parameters;
    std::map<std::string, std::string> parameterTypes;
};
namespace Animators {
inline Json settings(const Animator& a) {
    return {{"enabled", a.enabled}, {"sheet", a.sheet}};
}
inline Animator parse(const Json& j) {
    Animator a;
    a.enabled = j.value("enabled", true);
    a.sheet = j.value("sheet", std::string{});
    if (a.sheet.size() > 4096) {
        throw std::runtime_error("AnimatorSheet path too long");
    }
    return a;
}
} // namespace Animators
