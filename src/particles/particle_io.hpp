#pragma once
#include "materials/material.hpp"
#include "particles/particle_system.hpp"
#include "project/json_io.hpp"
#include <stdexcept>
namespace Particles {
inline Json settings(const ParticleSettings& s) {
    Json j = Json::object();
#define P(name) j[#name] = s.name
    P(collision);
    P(restitution);
    P(collisionFriction);
    P(loop);
    P(playOnAwake);
    P(localSpace);
    P(duration);
    P(startDelay);
    P(rate);
    P(maxParticles);
    P(burstCount);
    P(shape);
    P(blend);
    P(sprite);
    P(lifetimeMin);
    P(lifetimeMax);
    P(speedMin);
    P(speedMax);
    P(sizeMin);
    P(sizeMax);
    P(endSize);
    P(alpha);
    P(endAlpha);
    P(radius);
    P(coneAngle);
    P(drag);
    P(rotationMin);
    P(rotationMax);
    P(angularVelocity);
    P(seed);
    P(sheetColumns);
    P(sheetRows);
    P(frameRate);
    P(texture);
#undef P
#define V(name) j[#name] = Json::array({s.name.x, s.name.y, s.name.z})
    V(color);
    V(endColor);
    V(direction);
    V(velocity);
    V(gravity);
    V(boxSize);
#undef V
    return j;
}
inline ParticleSettings parse(const Json& j) {
    ParticleSettings s;
#define P(name)                                                                                    \
    if (j.contains(#name))                                                                         \
    j.at(#name).get_to(s.name)
    P(collision);
    P(restitution);
    P(collisionFriction);
    P(loop);
    P(playOnAwake);
    P(localSpace);
    P(duration);
    P(startDelay);
    P(rate);
    P(maxParticles);
    P(burstCount);
    P(shape);
    P(blend);
    P(sprite);
    P(lifetimeMin);
    P(lifetimeMax);
    P(speedMin);
    P(speedMax);
    P(sizeMin);
    P(sizeMax);
    P(endSize);
    P(alpha);
    P(endAlpha);
    P(radius);
    P(coneAngle);
    P(drag);
    P(rotationMin);
    P(rotationMax);
    P(angularVelocity);
    P(seed);
    P(sheetColumns);
    P(sheetRows);
    P(frameRate);
    P(texture);
#undef P
#define V(name)                                                                                    \
    if (j.contains(#name)) {                                                                       \
        const auto& a = j.at(#name);                                                               \
        if (!a.is_array() || a.size() != 3)                                                        \
            throw std::runtime_error("Expected a three-number particle vector");                   \
        s.name = {a[0].get<float>(), a[1].get<float>(), a[2].get<float>()};                        \
    }
    V(color);
    V(endColor);
    V(direction);
    V(velocity);
    V(gravity);
    V(boxSize);
#undef V
    auto range = [](float v, float lo, float hi) { return std::isfinite(v) && v >= lo && v <= hi; };
    auto vector = [&](Vec3 v, float lo, float hi) {
        return range(v.x, lo, hi) && range(v.y, lo, hi) && range(v.z, lo, hi);
    };
    if (!j.is_object() || !range(s.restitution, 0, 1) || !range(s.collisionFriction, 0, 1) ||
        !range(s.duration, .01f, 3600) || !range(s.startDelay, 0, 3600) ||
        !range(s.rate, 0, 10000) || s.maxParticles < 1 || s.maxParticles > 10000 ||
        s.burstCount < 0 || s.burstCount > 10000 || s.shape < 0 || s.shape > 3 || s.blend < 0 ||
        s.blend > 1 || s.sprite < 0 || s.sprite > 2 || !range(s.lifetimeMin, .01f, 120) ||
        !range(s.lifetimeMax, s.lifetimeMin, 120) || !range(s.speedMin, 0, 1000) ||
        !range(s.speedMax, s.speedMin, 1000) || !range(s.sizeMin, .001f, 1000) ||
        !range(s.sizeMax, s.sizeMin, 1000) || !range(s.endSize, 0, 1000) || !range(s.alpha, 0, 1) ||
        !range(s.endAlpha, 0, 1) || !range(s.radius, 0, 1000) || !range(s.coneAngle, 0, 90) ||
        !range(s.drag, 0, 100) || !range(s.rotationMin, -36000, 36000) ||
        !range(s.rotationMax, s.rotationMin, 36000) || !range(s.angularVelocity, -36000, 36000) ||
        s.seed < 0 || !range(s.frameRate, 0, 1000) || s.sheetColumns < 1 || s.sheetColumns > 64 ||
        s.sheetRows < 1 || s.sheetRows > 64 || !vector(s.color, 0, 16) ||
        !vector(s.endColor, 0, 16) || !vector(s.direction, -1000, 1000) ||
        !vector(s.velocity, -1000, 1000) || !vector(s.gravity, -1000, 1000) ||
        !vector(s.boxSize, 0, 1000) || !materialTexturePath(s.texture)) {
        throw std::runtime_error("Invalid particle settings or value out of range");
    }
    return s;
}
} // namespace Particles
