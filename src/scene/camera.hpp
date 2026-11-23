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

    bool orthographic = false; // Editor navigation only; never changes scene Camera components.

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

    // Acquire the world-space transform origin once per gesture, preserving the eye.
    void beginOrbit(Vec3 pivot) {
        if (!std::isfinite(pivot.x) || !std::isfinite(pivot.y) || !std::isfinite(pivot.z)) {
            return;
        }
        const Vec3 offset = position() - pivot;
        const float radius = std::sqrt(dot(offset, offset));
        if (radius < .001f || !std::isfinite(radius)) {
            target = pivot;
            distance = .1f;
            return;
        }
        distance = radius;
        if (offset.x * offset.x + offset.z * offset.z > 1e-10f) {
            yaw = std::atan2(offset.x, offset.z);
        }
        pitch = std::asin(std::clamp(offset.y / radius, -1.0f, 1.0f));
        target = pivot;
    }
    void orbit(Vec3 pivot, float dx, float dy) {
        target = pivot;
        yaw -= dx * .005f;
        pitch = std::clamp(pitch + dy * .005f, radians(-89.0f), radians(89.0f));
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
    float halfHeight() const {
        return distance * std::tan(fieldOfView * .5f);
    }
    Mat4 projection(float aspect) const {
        if (!orthographic) {
            return perspective(fieldOfView, aspect, .1f, 200.f);
        }
        Mat4 result{};
        result.m[0] = 1 / (halfHeight() * aspect);
        result.m[5] = 1 / halfHeight();
        result.m[10] = -2 / (200.f - .1f);
        result.m[14] = -(200.f + .1f) / (200.f - .1f);
        result.m[15] = 1;
        return result;
    }
    Mat4 viewProjection(float aspect) const {
        const Mat4 projection = this->projection(aspect);
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
        if (orthographic) {
            return {eye + right * ((2 * x - 1) * halfHeight() * aspect) +
                        up * ((1 - 2 * y) * halfHeight()),
                    forward};
        }
        const float halfHeight = std::tan(fieldOfView * 0.5f);
        const Vec3 direction = forward + right * ((2.0f * x - 1.0f) * halfHeight * aspect) +
                               up * ((1.0f - 2.0f * y) * halfHeight);
        return {eye, normalize(direction)};
    }
};
