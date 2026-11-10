#pragma once

#include "math.hpp"

// A camera that looks at a target point and circles around it.
struct OrbitCamera {
    Vec3  target   = {0.0f, 0.5f, 0.0f}; // the point it looks at
    float yaw      = radians(35.0f);     // rotation around +Y
    float pitch    = radians(35.0f);     // height above the horizon
    float distance = 13.0f;              // metres from the target

    Vec3 position() const
    {
        const Vec3 direction = {std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                                std::cos(pitch) * std::cos(yaw)};
        return target + direction * distance;
    }

    // Takes a point of the world to its place on the screen. `aspect` is the
    // width of the picture divided by its height.
    Mat4 viewProjection(float aspect) const
    {
        const Mat4 projection = perspective(radians(45.0f), aspect, 0.1f, 200.0f);
        const Mat4 view       = lookAt(position(), target, {0.0f, 1.0f, 0.0f});
        return multiply(projection, view);
    }
};
