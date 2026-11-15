#pragma once
#include "core/components.hpp"
#include "rendering/mesh.hpp"

// Local-space raycast debug geometry, shared by rendering backends.
inline MeshData raycastWire(const Raycast& query) {
    MeshData wire;
    wire.topology = MeshData::Topology::Lines;
    auto line = [&](Vec3 a, Vec3 b) {
        wire.vertices.push_back({a + query.offset, {}});
        wire.vertices.push_back({b + query.offset, {}});
    };
    if (query.type == 2) {
        for (int axis = 0; axis < 3; ++axis) {
            for (int i = 0; i < 24; ++i) {
                auto point = [&](int j) {
                    float a = 2 * kPi * j / 24, u = query.radius * std::cos(a),
                          v = query.radius * std::sin(a);
                    return axis == 0 ? Vec3{0, u, v} : axis == 1 ? Vec3{u, 0, v} : Vec3{u, v, 0};
                };
                line(point(i), point(i + 1));
            }
        }
    } else {
        int count = query.type == 1 ? std::clamp(query.samples, 2, 181) : 1;
        Vec3 previous;
        for (int i = 0; i < count; ++i) {
            float a = count == 1 ? 0 : radians(-query.angle * .5f + query.angle * i / (count - 1));
            Vec3 end{query.distance * std::sin(a), 0, -query.distance * std::cos(a)};
            line({}, end);
            if (i) {
                line(previous, end);
            }
            previous = end;
        }
    }
    return wire;
}
