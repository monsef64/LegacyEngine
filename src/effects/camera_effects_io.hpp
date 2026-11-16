#pragma once
#include "effects/camera_effects.hpp"
#include "project/json_io.hpp"
namespace CameraFx {
inline Json settings(const CameraEffects& c) {
    Json j = Json::object();
    for (const auto& f : boolFields) {
        j[f.name] = c.*f.member;
    }
    for (const auto& f : floatFields) {
        j[f.name] = c.*f.member;
    }
    return j;
}
inline CameraEffects parse(const Json& j) {
    if (!j.is_object()) {
        throw std::runtime_error("Invalid CameraEffects");
    }
    CameraEffects c;
    for (const auto& f : boolFields) {
        if (j.contains(f.name)) {
            c.*f.member = j.at(f.name).get<bool>();
        }
    }
    for (const auto& f : floatFields) {
        if (j.contains(f.name)) {
            c.*f.member = j.at(f.name).get<float>();
        }
    }
    if (!valid(c)) {
        throw std::runtime_error("CameraEffects value outside supported range");
    }
    return c;
}
} // namespace CameraFx
