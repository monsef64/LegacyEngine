#pragma once

#include "math.hpp"

// A camera that looks at a target point and circles around it.
struct OrbitCamera {
    Vec3  target      = {0.0f, 0.5f, 0.0f}; // the point it looks at
    float yaw         = radians(35.0f);     // rotation around +Y
    float pitch       = radians(35.0f);     // height above the horizon
    float distance    = 13.0f;              // metres from the target
    float fieldOfView = radians(45.0f);     // the angle it sees from bottom to top

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
        const Mat4 projection = perspective(fieldOfView, aspect, 0.1f, 200.0f);
        const Mat4 view       = lookAt(position(), target, {0.0f, 1.0f, 0.0f});
        return multiply(projection, view);
    }

    // The ray from the camera through a point of the picture: the one under
    // the mouse pointer, for example. `x` and `y` are fractions of the
    // picture's width and height, counted the way mouse positions are: x from
    // 0 at the left edge to 1 at the right edge, y from 0 at the top to 1 at
    // the bottom.
    Ray rayThrough(float x, float y, float aspect) const
    {
        const Vec3 eye     = position();
        const Vec3 forward = normalize(target - eye);
        const Vec3 right   = normalize(cross(forward, {0.0f, 1.0f, 0.0f}));
        const Vec3 up      = cross(right, forward);

        // Half the height of the picture, taken as a rectangle that stands
        // 1 m in front of the camera.
        const float halfHeight = std::tan(fieldOfView * 0.5f);
        const Vec3  direction  = forward + right * ((2.0f * x - 1.0f) * halfHeight * aspect) +
                                 up * ((1.0f - 2.0f * y) * halfHeight);
        return {eye, normalize(direction)};
    }
};
