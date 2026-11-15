#pragma once
#include "core/math.hpp"
#include <initializer_list>

// Extract/normalize once per view, rather than six square roots per object.
struct ViewFrustum {
    // The engine's existing camera matrices use NegativeOneToOne. A Vulkan
    // clip-space matrix uses ZeroToOne; its near plane is row 2, not row 3+2.
    enum class DepthRange { NegativeOneToOne, ZeroToOne };
    struct Plane {
        float a, b, c, d;
    };
    Plane planes[6];
    explicit ViewFrustum(const Mat4& vp, DepthRange depth = DepthRange::NegativeOneToOne) {
        int i = 0;
        for (int axis = 0; axis < 3; ++axis) {
            for (int sign : {-1, 1}) {
                // The + sign is the lower clipping plane. Vulkan's lower Z
                // bound is z >= 0 rather than z >= -w.
                const float w =
                    axis == 2 && sign == 1 && depth == DepthRange::ZeroToOne ? 0.0f : 1.0f;
                const float a = w * vp.m[3] + sign * vp.m[axis],
                            b = w * vp.m[7] + sign * vp.m[4 + axis],
                            c = w * vp.m[11] + sign * vp.m[8 + axis],
                            d = w * vp.m[15] + sign * vp.m[12 + axis];
                const float length = std::sqrt(a * a + b * b + c * c);
                // A degenerate plane imposes no constraint.
                planes[i++] = length > 0 ? Plane{a / length, b / length, c / length, d / length}
                                         : Plane{0, 0, 0, 0};
            }
        }
    }
    bool visible(Vec3 center, float radius) const {
        for (const auto& p : planes) {
            if (p.a * center.x + p.b * center.y + p.c * center.z + p.d < -radius) {
                return false;
            }
        }
        return true;
    }
};
