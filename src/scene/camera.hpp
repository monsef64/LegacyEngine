#pragma once

#include "core/math.hpp"
#include <algorithm>

// A camera that looks at a target point and circles around it.
struct OrbitCamera {
    Vec3 target = {0.0f, 0.5f, 0.0f};   // the point it looks at
    float yaw = radians(35.0f);         // rotation around +Y
    float pitch = radians(35.0f);       // height above the horizon
    float distance = 13.0f;             // metres from the target
    float fieldOfView = radians(45.0f); // the angle it sees from bottom to top

    Vec3 position() const {
        const Vec3 direction = {
            std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
        return target + direction * distance;
    }

    Vec3 right() const {
        return {std::cos(yaw), 0, -std::sin(yaw)};
    }
    Vec3 up() const {
        return normalize(cross(right(), normalize(target - position())));
    }
    void snapAxis(int axis, int sign) {
        if (axis < 0 || axis > 2) {
            return;
        }
        pitch = axis == 1 ? sign * kPi * .5f : 0;
        yaw = axis == 0 ? sign * kPi * .5f : axis == 2 ? (sign > 0 ? 0 : kPi) : yaw;
    }

    // Frame a world-space bounding sphere while preserving viewing direction.
    void focus(Vec3 centre, float radius = 1.0f, float aspect = 1.0f) {
        if (!std::isfinite(centre.x) || !std::isfinite(centre.y) || !std::isfinite(centre.z) ||
            !std::isfinite(radius) || !std::isfinite(aspect) || aspect <= 0) {
            return;
        }
        const float halfVertical = std::clamp(fieldOfView * .5f, radians(1.0f), radians(89.0f));
        const float halfHorizontal = std::atan(std::tan(halfVertical) * aspect);
        const float halfAngle = std::max(radians(1.0f), std::min(halfVertical, halfHorizontal));
        target = centre;
        distance = std::clamp(std::max(radius, .25f) * 1.2f / std::sin(halfAngle), 2.0f, 150.0f);
    }

    // Free-look rotates about the eye, unlike orbiting about the target.
    void look(float dx, float dy) {
        const Vec3 eye = position();
        yaw -= dx * 0.005f;
        pitch = std::clamp(pitch + dy * 0.005f, radians(-89.0f), radians(89.0f));
        const Vec3 offset{
            std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw)};
        target = eye - offset * distance;
    }

    void orbit(Vec3 pivot, float dx, float dy) {
        const Vec3 offset = position() - pivot;
        distance = std::clamp(std::sqrt(dot(offset, offset)), .1f, 10000.0f);
        yaw = std::atan2(offset.x, offset.z) - dx * .005f;
        pitch = std::clamp(std::asin(std::clamp(offset.y / distance, -1.0f, 1.0f)) + dy * .005f,
                           radians(-89.0f),
                           radians(89.0f));
        target = pivot;
    }
    void zoom(float wheel) {
        distance = std::clamp(distance * std::pow(.85f, wheel), .1f, 10000.0f);
    }
    void pan(float wheel) {
        const auto right = this->right();
        target = target + right * (wheel * distance * .05f);
    }

    // Local forward/right and world-up movement, normalized to avoid faster diagonals.
    void move(float forwardInput, float rightInput, float upInput, float metres) {
        const Vec3 forward = normalize(target - position());
        const Vec3 right = this->right();
        const Vec3 direction = forward * forwardInput + right * rightInput + Vec3{0, upInput, 0};
        if (dot(direction, direction) > 0) {
            target = target + normalize(direction) * metres;
        }
    }

    // Takes a point of the world to its place on the screen. `aspect` is the
    // width of the picture divided by its height.
    Mat4 viewProjection(float aspect) const {
        const Mat4 projection = perspective(fieldOfView, aspect, 0.1f, 200.0f);
        const Mat4 view = lookAt(position(), target, up());
        return multiply(projection, view);
    }

    // The ray from the camera through a point of the picture: the one under
    // the mouse pointer, for example. `x` and `y` are fractions of the
    // picture's width and height, counted the way mouse positions are: x from
    // 0 at the left edge to 1 at the right edge, y from 0 at the top to 1 at
    // the bottom.
    Ray rayThrough(float x, float y, float aspect) const {
        const Vec3 eye = position();
        const Vec3 forward = normalize(target - eye);
        const Vec3 right = this->right();
        const Vec3 up = cross(right, forward);

        // Half the height of the picture, taken as a rectangle that stands
        // 1 m in front of the camera.
        const float halfHeight = std::tan(fieldOfView * 0.5f);
        const Vec3 direction = forward + right * ((2.0f * x - 1.0f) * halfHeight * aspect) +
                               up * ((1.0f - 2.0f * y) * halfHeight);
        return {eye, normalize(direction)};
    }
};
