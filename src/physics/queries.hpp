#pragma once
#include "core/components.hpp"
#include <functional>
struct RayHit {
    Entity entity;
    Vec3 point, normal;
    float distance = 0;
};
// Installed by the running engine; scripting remains usable without a physics backend.
inline std::function<std::vector<RayHit>(World&, Entity, const Raycast&)> sceneQuery;
