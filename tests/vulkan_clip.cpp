#include "rendering/vulkan_clip.hpp"
#include "core/frustum.hpp"

#include <cassert>
#include <cmath>
#include <initializer_list>

namespace {
struct ClipPoint {
    float x, y, z, w;
};
ClipPoint project(const Mat4& m, Vec3 p) {
    return {m.m[0] * p.x + m.m[4] * p.y + m.m[8] * p.z + m.m[12],
            m.m[1] * p.x + m.m[5] * p.y + m.m[9] * p.z + m.m[13],
            m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14],
            m.m[3] * p.x + m.m[7] * p.y + m.m[11] * p.z + m.m[15]};
}
bool close(float a, float b) {
    return std::abs(a - b) < 0.0001f;
}
void checkProjection(const Mat4& gl, Vec3 nearPoint, Vec3 farPoint) {
    const Mat4 vk = vulkanClipMatrix(gl);
    const auto n = project(vk, nearPoint), f = project(vk, farPoint);
    assert(close(n.z / n.w, 0.0f));
    assert(close(f.z / f.w, 1.0f));
    for (const Vec3 p : {nearPoint, farPoint, Vec3{0.5f, 0.3f, -2.0f}}) {
        const auto a = project(gl, p), b = project(vk, p);
        assert(close(a.x, b.x));
        assert(close(a.y, -b.y));
        assert(close((a.z + a.w) * 0.5f, b.z));
        assert(close(a.w, b.w));
    }
}
void checkFrustum(const Mat4& gl) {
    ViewFrustum original(gl);
    ViewFrustum converted(vulkanClipMatrix(gl), ViewFrustum::DepthRange::ZeroToOne);
    for (int x = -5; x <= 5; ++x) {
        for (int y = -5; y <= 5; ++y) {
            for (int z = -12; z <= 2; ++z) {
                for (float radius : {0.0f, 0.2f}) {
                    Vec3 point{x * 0.7f, y * 0.7f, z * 0.8f + 0.13f};
                    assert(original.visible(point, radius) == converted.visible(point, radius));
                }
            }
        }
    }
    // A point between the eye and near plane must not be admitted by using
    // the symmetric-depth near-plane extraction on a Vulkan matrix.
    assert(!converted.visible({0, 0, -0.75f}, 0));
    assert(converted.visible({0, 0, -1.1f}, 0));
    assert(!converted.visible({0, 0, -11}, 0));
}
} // namespace

int main() {
    const Mat4 perspectiveProjection = perspective(kPi / 2, 1.5f, 1.0f, 10.0f);
    checkProjection(perspectiveProjection, {0, 0, -1}, {0, 0, -10});
    checkFrustum(perspectiveProjection);

    // Right-handed orthographic projection, with the same clip planes.
    Mat4 orthographic{};
    orthographic.m[0] = 0.5f;
    orthographic.m[5] = 0.5f;
    orthographic.m[10] = -2.0f / 9.0f;
    orthographic.m[14] = -11.0f / 9.0f;
    orthographic.m[15] = 1;
    checkProjection(orthographic, {0, 0, -1}, {0, 0, -10});
    checkFrustum(orthographic);

    // Conversion applies to the whole view-projection matrix, not just a
    // projection whose fourth row has the standard perspective form.
    const Mat4 view = lookAt({2, 3, 4}, {0, 0, 0}, {0, 1, 0});
    const Mat4 gl = multiply(perspectiveProjection, view);
    const Mat4 vk = vulkanClipMatrix(gl);
    ViewFrustum original(gl), converted(vk, ViewFrustum::DepthRange::ZeroToOne);
    for (int x = -5; x <= 5; ++x) {
        for (int y = -5; y <= 5; ++y) {
            for (int z = -5; z <= 5; ++z) {
                const Vec3 point{x * 0.51f, y * 0.73f, z * 0.91f};
                assert(original.visible(point, .1f) == converted.visible(point, .1f));
            }
        }
    }
}
