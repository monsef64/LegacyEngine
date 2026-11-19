#pragma once
#include "core/ecs.hpp"
#include "core/math.hpp"
#include "project/json_io.hpp"

struct TPSCamera {
    bool enabled = true, mouseLook = true, collision = true;
    Entity target;
    Vec3 offset{0, 1.5f, 0};
    float distance = 5, minDistance = .5f, maxDistance = 15;
    float yaw = 0, pitch = 15, minPitch = -30, maxPitch = 75;
    float sensitivity = .12f, zoomSpeed = 1, smoothing = 10, shoulder = 0, collisionRadius = .25f;
    float resolvedDistance = 0;
    bool initialized = false;
    Vec3 previousPosition;
};
namespace TPSCameras {
inline Json settings(const TPSCamera& c) {
    return {{"enabled", c.enabled},
            {"mouseLook", c.mouseLook},
            {"collision", c.collision},
            {"offset", {c.offset.x, c.offset.y, c.offset.z}},
            {"distance", c.distance},
            {"minDistance", c.minDistance},
            {"maxDistance", c.maxDistance},
            {"yaw", c.yaw},
            {"pitch", c.pitch},
            {"minPitch", c.minPitch},
            {"maxPitch", c.maxPitch},
            {"sensitivity", c.sensitivity},
            {"zoomSpeed", c.zoomSpeed},
            {"smoothing", c.smoothing},
            {"shoulder", c.shoulder},
            {"collisionRadius", c.collisionRadius}};
}
inline TPSCamera parse(const Json& j) {
    TPSCamera c;
    c.enabled = j.value("enabled", true);
    c.mouseLook = j.value("mouseLook", true);
    c.collision = j.value("collision", true);
    if (j.contains("offset")) {
        auto v = j.at("offset");
        c.offset = {v.at(0).get<float>(), v.at(1).get<float>(), v.at(2).get<float>()};
    }
#define READ(n) c.n = j.value(#n, c.n)
    READ(distance);
    READ(minDistance);
    READ(maxDistance);
    READ(yaw);
    READ(pitch);
    READ(minPitch);
    READ(maxPitch);
    READ(sensitivity);
    READ(zoomSpeed);
    READ(smoothing);
    READ(shoulder);
    READ(collisionRadius);
#undef READ
    for (const auto& n : {c.distance,
                          c.minDistance,
                          c.maxDistance,
                          c.yaw,
                          c.pitch,
                          c.minPitch,
                          c.maxPitch,
                          c.sensitivity,
                          c.zoomSpeed,
                          c.smoothing,
                          c.shoulder,
                          c.collisionRadius,
                          c.offset.x,
                          c.offset.y,
                          c.offset.z}) {
        if (!std::isfinite(n) || std::abs(n) > 100000) {
            throw std::runtime_error("Invalid TPSCamera value");
        }
    }
    if (c.minDistance < .05f || c.maxDistance < c.minDistance || c.maxDistance > 1000 ||
        c.distance < c.minDistance || c.distance > c.maxDistance || c.minPitch < -89 ||
        c.maxPitch > 89 || c.minPitch > c.maxPitch || c.pitch < c.minPitch ||
        c.pitch > c.maxPitch || c.sensitivity < 0 || c.sensitivity > 10 || c.smoothing < 0 ||
        c.smoothing > 100 || c.collisionRadius < .02f || c.collisionRadius > 5 || c.zoomSpeed < 0 ||
        c.zoomSpeed > 20 || std::abs(c.shoulder) > 10) {
        throw std::runtime_error("TPSCamera settings outside supported range");
    }
    return c;
}
} // namespace TPSCameras
