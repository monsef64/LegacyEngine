#pragma once
#include "core/math.hpp"

// Extract/normalize once per view, rather than six square roots per object.
struct ViewFrustum {
    struct Plane {
        float a, b, c, d;
    };
    Plane planes[6];
    explicit ViewFrustum(const Mat4& vp) {
        int i = 0;
        for (int axis = 0; axis < 3; ++axis) {
            for (int sign : {-1, 1}) {
                const float a = vp.m[3] + sign * vp.m[axis], b = vp.m[7] + sign * vp.m[4 + axis],
                            c = vp.m[11] + sign * vp.m[8 + axis],
                            d = vp.m[15] + sign * vp.m[12 + axis];
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
