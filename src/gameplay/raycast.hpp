#pragma once
#include "core/hierarchy.hpp"
#include "physics/physics.hpp"
#include "project/layers.hpp"
#include <map>
inline std::vector<RayHit>
queryScene(World& world, Physics& physics, Entity owner, const Raycast& query) {
    if (!world.has<Transform>(owner)) {
        return {};
    }
    const auto pose = world.get<Transform>(owner);
    const auto origin = pose.position + rotateVector(pose.rotation, query.offset);
    auto filter = [&](Entity target) {
        return target != owner && world.alive(target) && isActive(world, target) &&
               (query.layerMask & (1u << LayerSettings::effective(world, target))) &&
               (query.includeTriggers || !world.get<Collider>(target).isTrigger);
    };
    if (query.type == 2) {
        return physics.overlapSphere(origin, query.radius, filter);
    }
    std::map<std::pair<unsigned, unsigned>, RayHit> hits;
    const int count = query.type == 1 ? std::clamp(query.samples, 2, 181) : 1;
    for (int i = 0; i < count; ++i) {
        float angle = count == 1 ? 0 : radians(-query.angle * .5f + query.angle * i / (count - 1));
        const auto direction = rotateVector(pose.rotation, {std::sin(angle), 0, -std::cos(angle)});
        auto remember = [&](const RayHit& hit) {
            auto key = std::make_pair(hit.entity.index, hit.entity.generation);
            auto found = hits.find(key);
            if (found == hits.end() || found->second.distance > hit.distance) {
                hits[key] = hit;
            }
        };
        if (query.type == 1) {
            RayHit hit;
            if (physics.raycast({origin, direction}, query.distance, hit, filter)) {
                remember(hit);
            }
        } else {
            for (const auto& hit :
                 physics.raycastAll({origin, direction}, query.distance, filter)) {
                remember(hit);
            }
        }
    }
    std::vector<RayHit> result;
    for (auto& entry : hits) {
        result.push_back(entry.second);
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.distance < b.distance;
    });
    return result;
}
