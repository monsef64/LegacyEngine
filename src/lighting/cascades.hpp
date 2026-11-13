#pragma once
#include "scene/scene_camera.hpp"
#include <array>
// Four practical frustum splits; projection-agnostic (perspective and orthographic).
namespace Lighting {
constexpr int cascadeCount = 4;
struct Cascades {
    std::array<Mat4, cascadeCount> matrices;
    std::array<float, cascadeCount> splits;
    Vec3 origin, forward;
};
inline Cascades
buildCascades(const Mat4& cameraVP, Vec3 eye, Vec3 lightDirection, float distance, int resolution) {
    const auto inverse = inverseCameraMatrix(cameraVP);
    std::array<Vec3, 4> nearCorners, farCorners;
    for (int i = 0; i < 4; ++i) {
        const float x = (i & 1) ? 1.f : -1.f, y = (i & 2) ? 1.f : -1.f;
        nearCorners[i] = projectCameraPoint(inverse, {x, y, -1});
        farCorners[i] = projectCameraPoint(inverse, {x, y, 1});
    }
    Cascades result{};
    Vec3 farCenter;
    for (int i = 0; i < 4; ++i) {
        result.origin = result.origin + nearCorners[i] * .25f;
        farCenter = farCenter + farCorners[i] * .25f;
    }
    result.forward = normalize(farCenter - result.origin);
    const float fullRange = dot(farCenter - result.origin, result.forward);
    if (!std::isfinite(fullRange) || fullRange <= 0 || resolution < 1) {
        throw std::runtime_error("Invalid cascade camera frustum");
    }
    const float nearDistance = std::max(.01f, dot(result.origin - eye, result.forward));
    const float farDistance = nearDistance + std::min(fullRange, std::max(1.f, distance));
    Vec3 direction = normalize(lightDirection), up{0, 1, 0};
    if (std::abs(dot(direction, up)) > .98f) {
        up = {0, 0, 1};
    }
    const Vec3 right = normalize(cross(direction, up)), vertical = cross(right, direction);
    float previous = 0;
    for (int split = 0; split < cascadeCount; ++split) {
        const float fraction = float(split + 1) / cascadeCount;
        const float logarithmic = nearDistance * std::pow(farDistance / nearDistance, fraction);
        const float uniform = nearDistance + (farDistance - nearDistance) * fraction;
        const float end =
            (split == cascadeCount - 1 ? farDistance : .6f * logarithmic + .4f * uniform) -
            nearDistance;
        result.splits[split] = end;
        // Overlap the previous slice's blend band so both maps cover the transition.
        const float begin = split == 0 ? 0 : previous * .9f;
        std::array<Vec3, 8> corners;
        Vec3 center;
        for (int i = 0; i < 4; ++i) {
            const auto ray = farCorners[i] - nearCorners[i];
            corners[i] = nearCorners[i] + ray * (begin / fullRange);
            corners[i + 4] = nearCorners[i] + ray * (end / fullRange);
            center = center + (corners[i] + corners[i + 4]) * .125f;
        }
        float radius = 0;
        for (auto p : corners) {
            auto d = p - center;
            radius = std::max(radius, std::sqrt(dot(d, d)));
        }
        radius = std::max(.1f, std::ceil(radius * 16) / 16) * (1 + 2.f / resolution);
        const float texel = 2 * radius / resolution;
        center =
            center + right * (std::round(dot(center, right) / texel) * texel - dot(center, right)) +
            vertical * (std::round(dot(center, vertical) / texel) * texel - dot(center, vertical));
        const float depthExtent = std::max(radius * 2, distance);
        Mat4 projection{};
        projection.m[0] = projection.m[5] = 1 / radius;
        projection.m[10] = -1 / depthExtent;
        projection.m[14] = -1;
        projection.m[15] = 1;
        result.matrices[split] =
            multiply(projection, lookAt(center - direction * depthExtent, center, up));
        previous = end;
    }
    return result;
}
} // namespace Lighting
