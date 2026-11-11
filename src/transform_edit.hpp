#pragma once

#include "components.hpp"
#include <algorithm>
#include <cmath>

enum class TransformTool { Move, Rotate, Scale };

// Read a positive-scale TRS matrix from the gizmo. Only change the active
// channel, so moving an object never perturbs its rotation or dimensions.
inline bool applyGizmoMatrix(Transform& transform, const Mat4& matrix, TransformTool tool,
                             bool uniformScale)
{
    for (float value : matrix.m) if (!std::isfinite(value)) return false;
    if (tool == TransformTool::Move) {
        transform.position = {matrix.m[12], matrix.m[13], matrix.m[14]};
    } else {
        Vec3 columns[3]{{matrix.m[0], matrix.m[1], matrix.m[2]},
                        {matrix.m[4], matrix.m[5], matrix.m[6]},
                        {matrix.m[8], matrix.m[9], matrix.m[10]}};
        float scales[3];
        for (int i = 0; i < 3; ++i) {
            scales[i] = std::sqrt(dot(columns[i], columns[i]));
            if (!(scales[i] > 1e-6f)) return false;
            columns[i] = columns[i] * (1.0f / scales[i]);
        }
        if (tool == TransformTool::Scale) {
            if (uniformScale) {
                const float previous[3]{transform.scale.x, transform.scale.y, transform.scale.z};
                int changed = 0;
                for (int i = 1; i < 3; ++i)
                    if (std::abs(scales[i]-previous[i]) > std::abs(scales[changed]-previous[changed])) changed = i;
                const float size = std::clamp(scales[changed], 0.05f, 100.0f);
                transform.scale = {size, size, size};
            } else {
                transform.scale = {std::clamp(scales[0], 0.05f, 100.0f),
                                   std::clamp(scales[1], 0.05f, 100.0f),
                                   std::clamp(scales[2], 0.05f, 100.0f)};
            }
        } else {
            // Orthonormal matrix to quaternion, including rotations near 180 degrees.
            const float m00=columns[0].x, m01=columns[1].x, m02=columns[2].x;
            const float m10=columns[0].y, m11=columns[1].y, m12=columns[2].y;
            const float m20=columns[0].z, m21=columns[1].z, m22=columns[2].z;
            Quat q;
            const float trace=m00+m11+m22;
            if (trace > 0) {
                const float s=2*std::sqrt(trace+1);
                q={(m21-m12)/s, (m02-m20)/s, (m10-m01)/s, s*0.25f};
            } else if (m00 > m11 && m00 > m22) {
                const float s=2*std::sqrt(1+m00-m11-m22);
                q={s*0.25f, (m01+m10)/s, (m02+m20)/s, (m21-m12)/s};
            } else if (m11 > m22) {
                const float s=2*std::sqrt(1+m11-m00-m22);
                q={(m01+m10)/s, s*0.25f, (m12+m21)/s, (m02-m20)/s};
            } else {
                const float s=2*std::sqrt(1+m22-m00-m11);
                q={(m02+m20)/s, (m12+m21)/s, s*0.25f, (m10-m01)/s};
            }
            transform.rotation=normalize(q);
        }
    }
    return true;
}
