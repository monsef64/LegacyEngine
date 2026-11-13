#pragma once
#include "core/components.hpp"
struct SkyboxTransition {
    Skybox from, to;
    Entity destination;
    float progress = 0, speed = 1;
    float weight() const {
        return progress * progress * (3 - 2 * progress);
    }
};
inline const char* beginSkyboxFade(World& world, Entity source, Entity target, float speed) {
    if (!std::isfinite(speed) || speed <= 0 || speed > 1000) {
        return "Fade speed must be finite and between 0 and 1000";
    }
    if (source == target || !world.has<Skybox>(source) || !world.has<Skybox>(target)) {
        return "Fade requires two different Skybox objects";
    }
    bool busy = false;
    world.eachReadOnly<SkyboxTransition>([&](Entity, const SkyboxTransition&) { busy = true; });
    if (busy) {
        return "A skybox fade is already in progress";
    }
    auto from = world.get<Skybox>(source), to = world.get<Skybox>(target);
    if (!from.enabled || !validSkybox(from) || !validSkybox(to)) {
        return "Fade requires a valid active source and destination";
    }
    to.enabled = true;
    world.get<Skybox>(target).enabled = false;
    world.add(source, SkyboxTransition{std::move(from), std::move(to), target, 0, speed});
    return nullptr;
}
inline void updateSkyboxTransitions(World& world, float dt) {
    if (!std::isfinite(dt) || dt < 0) {
        return;
    }
    world.each<SkyboxTransition>([&](Entity entity, SkyboxTransition& fade) {
        if (!world.has<Skybox>(entity) || !world.has<Skybox>(fade.destination)) {
            world.remove<SkyboxTransition>(entity);
            return;
        }
        fade.progress = std::min(1.0f, fade.progress + dt * fade.speed);
        if (fade.progress >= 1) {
            world.get<Skybox>(entity).enabled = false;
            world.get<Skybox>(fade.destination).enabled = true;
            world.remove<SkyboxTransition>(entity);
        }
    });
}
