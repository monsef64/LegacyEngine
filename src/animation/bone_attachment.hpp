#pragma once
#include "core/ecs.hpp"
#include "core/math.hpp"
#include "project/json_io.hpp"
struct BoneAttachment {
    bool enabled = true;
    Entity source{};
    int node = -1;
    Vec3 offset{}, rotation{}; // Bone-local metres and Euler degrees.
    std::string error;         // Runtime only.
};
namespace BoneAttachments {
inline Json settings(const BoneAttachment& b) {
    return {{"enabled", b.enabled},
            {"node", b.node},
            {"offset", {b.offset.x, b.offset.y, b.offset.z}},
            {"rotation", {b.rotation.x, b.rotation.y, b.rotation.z}}};
}
inline BoneAttachment parse(const Json& j) {
    BoneAttachment b;
    b.enabled = j.value("enabled", true);
    b.node = j.value("node", -1);
    auto read = [&](const char* name) {
        auto a = j.value(name, Json::array({0, 0, 0}));
        if (!a.is_array() || a.size() != 3) {
            throw std::runtime_error("Expected bone offset/rotation vector");
        }
        Vec3 v{a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z) ||
            std::max({std::abs(v.x), std::abs(v.y), std::abs(v.z)}) > 100000) {
            throw std::runtime_error("Invalid bone offset/rotation");
        }
        return v;
    };
    b.offset = read("offset");
    b.rotation = read("rotation");
    if (b.node < -1 || b.node > 4095) {
        throw std::runtime_error("Invalid bone node");
    }
    return b;
}
} // namespace BoneAttachments
