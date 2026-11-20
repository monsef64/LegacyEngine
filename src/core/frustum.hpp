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
    // Plane support of a transformed local AABB; works with rotation, mirrors
    // and non-uniform scale without the overdraw of a large bounding sphere.
    bool visibleBox(Vec3 center, Vec3 half, const Mat4& model) const {
        const Vec3 p{
            model.m[0] * center.x + model.m[4] * center.y + model.m[8] * center.z + model.m[12],
            model.m[1] * center.x + model.m[5] * center.y + model.m[9] * center.z + model.m[13],
            model.m[2] * center.x + model.m[6] * center.y + model.m[10] * center.z + model.m[14]};
        for (const auto& plane : planes) {
            const float radius =
                half.x *
                    std::abs(plane.a * model.m[0] + plane.b * model.m[1] + plane.c * model.m[2]) +
                half.y *
                    std::abs(plane.a * model.m[4] + plane.b * model.m[5] + plane.c * model.m[6]) +
                half.z *
                    std::abs(plane.a * model.m[8] + plane.b * model.m[9] + plane.c * model.m[10]);
            if (plane.a * p.x + plane.b * p.y + plane.c * p.z + plane.d < -radius) {
                return false;
            }
        }
        return true;
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
