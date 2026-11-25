#pragma once
#include "core/math.hpp"
#include "project/json_io.hpp"
#include <string>
#include <vector>

struct NavMeshAgent {
    bool enabled = true, stopped = false;
    float speed = 3.5f, stoppingDistance = .15f;
    float heightOffset = 0; // World-space pivot height above the navigation surface.
    bool updateRotation = true;
    float angularSpeed = 360;
    // Runtime request/status; not saved. All travel is constrained by Detour.
    Vec3 destination, velocity;
    bool destinationSet = false;
    bool requested = false, hasPath = false, pathPending = false;
    float remainingDistance = 0;
    std::string status = "Idle";
    std::vector<Vec3> corners;
    std::size_t corner = 0;
    unsigned navigationRevision = 0;
};
namespace Navigation {
inline Json agentSettings(const NavMeshAgent& a) {
    return {{"enabled", a.enabled},
            {"speed", a.speed},
            {"heightOffset", a.heightOffset},
            {"stoppingDistance", a.stoppingDistance},
            {"updateRotation", a.updateRotation},
            {"angularSpeed", a.angularSpeed}};
}
inline NavMeshAgent parseAgent(const Json& j) {
    NavMeshAgent a;
    a.enabled = j.value("enabled", true);
    a.updateRotation = j.value("updateRotation", true);
    a.angularSpeed = j.value("angularSpeed", 360.f);
    if (!std::isfinite(a.angularSpeed) || a.angularSpeed < 0 || a.angularSpeed > 3600) {
        throw std::runtime_error("Invalid angular speed");
    }
    a.heightOffset = j.value("heightOffset", 0.f);
    if (!std::isfinite(a.heightOffset) || std::abs(a.heightOffset) > 1000) {
        throw std::runtime_error("Invalid NavMeshAgent height offset");
    }
    a.speed = j.value("speed", 3.5f);
    a.stoppingDistance = j.value("stoppingDistance", .15f);
    if (!std::isfinite(a.speed) || a.speed < 0 || a.speed > 100 ||
        !std::isfinite(a.stoppingDistance) || a.stoppingDistance < .01f ||
        a.stoppingDistance > 10) {
        throw std::runtime_error("Invalid NavMeshAgent settings");
    }
    return a;
}
} // namespace Navigation
