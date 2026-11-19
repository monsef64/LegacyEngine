#pragma once
#include "core/math.hpp"
#include "project/json_io.hpp"
#include <array>

// Four-wheel raycast vehicle, local -Z forward, +Y up. Commands are not saved.
#define VEHICLE_SETTINGS(X)                                                                        \
    X(wheelBase, 2.4f, .2f, 20.f)                                                                  \
    X(trackWidth, 1.5f, .2f, 10.f)                                                                 \
    X(mountHeight, -.2f, -10.f, 10.f)                                                              \
    X(wheelRadius, .35f, .05f, 3.f)                                                                \
    X(suspensionLength, .45f, .05f, 3.f)                                                           \
    X(springStrength, 35000.f, 100.f, 500000.f)                                                    \
    X(suspensionDamping, 4500.f, 0.f, 50000.f)                                                     \
    X(engineForce, 8000.f, 0.f, 200000.f)                                                          \
    X(brakeForce, 12000.f, 0.f, 300000.f)                                                          \
    X(tireGrip, 1.5f, .05f, 5.f)                                                                   \
    X(maxSteerAngle, 30.f, 0.f, 70.f)                                                              \
    X(maxSpeed, 45.f, 1.f, 150.f)
struct Vehicle {
    bool enabled = true;
#define FIELD(name, value, lo, hi) float name = value;
    VEHICLE_SETTINGS(FIELD)
#undef FIELD
    float throttle = 0, steering = 0, brake = 0;
    bool handbrake = false;
    float speed = 0;
    float wheelRoll = 0; // Runtime visual rotation, never serialized.
    int groundedWheels = 0;
    std::array<float, 4> compression{}; // front-left, front-right, rear-left, rear-right
};
namespace Vehicles {
inline bool valid(const Vehicle& v) {
#define CHECK(name, value, lo, hi)                                                                 \
    if (!std::isfinite(v.name) || v.name < lo || v.name > hi)                                      \
        return false;
    VEHICLE_SETTINGS(CHECK)
#undef CHECK
    return true;
}
inline Json settings(const Vehicle& v) {
    Json j = {{"enabled", v.enabled}};
#define SAVE(name, value, lo, hi) j[#name] = v.name;
    VEHICLE_SETTINGS(SAVE)
#undef SAVE
    return j;
}
inline Vehicle parse(const Json& j) {
    if (!j.is_object()) {
        throw std::runtime_error("Expected Vehicle settings object");
    }
    Vehicle v;
    v.enabled = j.value("enabled", true);
#define LOAD(name, initial, lo, hi) v.name = j.value(#name, v.name);
    VEHICLE_SETTINGS(LOAD)
#undef LOAD
    if (!valid(v)) {
        throw std::runtime_error("Vehicle setting outside supported range");
    }
    return v;
}
} // namespace Vehicles
