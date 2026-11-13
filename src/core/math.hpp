// Minimal math: 3D vectors, rotations and matrices.
//
// World conventions:
//   * 1 unit = 1 metre
//   * +Y is up, the ground is the XZ plane (y = 0)
//   * right-handed coordinates (the OpenGL convention)
//   * angles are in radians

#pragma once

#include <cmath>

constexpr float kPi = 3.14159265358979f;
constexpr float radians(float degrees) {
    return degrees * kPi / 180.0f;
}

// ---------------------------------------------------------------------------
// Vectors
// ---------------------------------------------------------------------------

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline Vec3 operator+(Vec3 a, Vec3 b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}
inline Vec3 operator-(Vec3 a, Vec3 b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
inline Vec3 operator*(Vec3 v, float s) {
    return {v.x * s, v.y * s, v.z * s};
}
inline Vec3 operator*(Vec3 a, Vec3 b) {
    return {a.x * b.x, a.y * b.y, a.z * b.z};
} // per axis
inline bool operator==(Vec3 a, Vec3 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}
inline bool operator!=(Vec3 a, Vec3 b) {
    return !(a == b);
}

inline float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

// Rescales v to length 1. A vector of length 0 has no direction and is returned
// as it is.
inline Vec3 normalize(Vec3 v) {
    const float lengthSquared = dot(v, v);
    return lengthSquared > 0.0f ? v * (1.0f / std::sqrt(lengthSquared)) : v;
}

// The point at fraction t of the way from a to b.
inline Vec3 lerp(Vec3 a, Vec3 b, float t) {
    return a + (b - a) * t;
}

// A line that starts at `origin` and goes on without end in `direction`, a
// unit vector.
struct Ray {
    Vec3 origin;
    Vec3 direction = {0.0f, 0.0f, -1.0f};
};

// ---------------------------------------------------------------------------
// Rotations
// ---------------------------------------------------------------------------

// A rotation, stored as a unit quaternion. The default value is "not rotated".
// Build one with axisAngle() rather than by filling in the four numbers.
struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

inline bool operator==(Quat a, Quat b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}
inline bool operator!=(Quat a, Quat b) {
    return !(a == b);
}

// A turn of `angle` radians around `axis`. Seen with the axis pointing at you,
// the turn is counter-clockwise: axisAngle({0, 1, 0}, radians(90)) turns +Z
// into +X.
inline Quat axisAngle(Vec3 axis, float angle) {
    const Vec3 n = normalize(axis);
    const float s = std::sin(angle * 0.5f);
    return {n.x * s, n.y * s, n.z * s, std::cos(angle * 0.5f)};
}

// Rescales q to length 1, which is what makes a quaternion a pure rotation.
// A quaternion whose length is 0, or not a usable number, gives "not rotated".
inline Quat normalize(Quat q) {
    const float lengthSquared = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    if (!(lengthSquared > 0.0f) || std::isinf(lengthSquared)) {
        return {};
    }
    const float s = 1.0f / std::sqrt(lengthSquared);
    return {q.x * s, q.y * s, q.z * s, q.w * s};
}

// The rotation at fraction t of the way from a to b, going the short way round.
inline Quat nlerp(Quat a, Quat b, float t) {
    // q and -q are the same rotation: flip b if that brings it closer to a.
    const float side = (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w) < 0.0f ? -1.0f : 1.0f;
    return normalize({a.x + (b.x * side - a.x) * t,
                      a.y + (b.y * side - a.y) * t,
                      a.z + (b.z * side - a.z) * t,
                      a.w + (b.w * side - a.w) * t});
}

// ---------------------------------------------------------------------------
// Matrices
// ---------------------------------------------------------------------------

// Matrices are stored column by column, which is the layout OpenGL expects:
// element (row, column) of a Mat4 lives at m[column * 4 + row], and of a Mat3
// at m[column * 3 + row].
struct Mat3 {
    float m[9];
};

struct Mat4 {
    float m[16];
};

inline Mat4 multiply(const Mat4& a, const Mat4& b) // a * b
{
    Mat4 result{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) {
                result.m[column * 4 + row] += a.m[k * 4 + row] * b.m[column * 4 + k];
            }
        }
    }
    return result;
}

// The rotation as a matrix. Its columns are where the X, Y and Z axes end up.
inline Mat3 rotationMatrix(Quat q) {
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    return {{
        1.0f - 2.0f * (yy + zz),
        2.0f * (xy + wz),
        2.0f * (xz - wy), // X axis
        2.0f * (xy - wz),
        1.0f - 2.0f * (xx + zz),
        2.0f * (yz + wx), // Y axis
        2.0f * (xz + wy),
        2.0f * (yz - wx),
        1.0f - 2.0f * (xx + yy), // Z axis
    }};
}

// Places an object in the world: it is scaled first, then rotated, then moved
// to `position`.
inline Mat4 transformMatrix(Vec3 position, Quat rotation, Vec3 scale) {
    const Mat3 r = rotationMatrix(rotation);
    const float s[3] = {scale.x, scale.y, scale.z};

    Mat4 result{};
    for (int column = 0; column < 3; ++column) {
        for (int row = 0; row < 3; ++row) {
            result.m[column * 4 + row] = r.m[column * 3 + row] * s[column];
        }
    }
    result.m[12] = position.x;
    result.m[13] = position.y;
    result.m[14] = position.z;
    result.m[15] = 1.0f;
    return result;
}

// Turns the normals (the directions surfaces face) of an object placed with
// transformMatrix(). Normals do not stretch with the surface: where a scale
// stretches the object, they have to shrink instead to stay perpendicular to
// it. The results are not unit length.
inline Mat3 normalMatrix(Quat rotation, Vec3 scale) {
    const float s[3] = {scale.x, scale.y, scale.z};

    Mat3 result = rotationMatrix(rotation);
    for (int column = 0; column < 3; ++column) {
        const float inverse = s[column] != 0.0f ? 1.0f / s[column] : 0.0f;
        for (int row = 0; row < 3; ++row) {
            result.m[column * 3 + row] *= inverse;
        }
    }
    return result;
}

inline Mat4 perspective(float verticalFov, float aspect, float zNear, float zFar) {
    const float f = 1.0f / std::tan(verticalFov * 0.5f);
    Mat4 result{};
    result.m[0] = f / aspect;
    result.m[5] = f;
    result.m[10] = (zFar + zNear) / (zNear - zFar);
    result.m[11] = -1.0f;
    result.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
    return result;
}

inline Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 forward = normalize(target - eye);
    const Vec3 right = normalize(cross(forward, up));
    const Vec3 trueUp = cross(right, forward);

    Mat4 result{};
    result.m[0] = right.x;
    result.m[4] = right.y;
    result.m[8] = right.z;
    result.m[12] = -dot(right, eye);
    result.m[1] = trueUp.x;
    result.m[5] = trueUp.y;
    result.m[9] = trueUp.z;
    result.m[13] = -dot(trueUp, eye);
    result.m[2] = -forward.x;
    result.m[6] = -forward.y;
    result.m[10] = -forward.z;
    result.m[14] = dot(forward, eye);
    result.m[15] = 1.0f;
    return result;
}
