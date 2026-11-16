#pragma once
#include "core/hierarchy.hpp"
namespace Particles {
inline float random(ParticleSystem& s) {
    s.random ^= s.random << 13;
    s.random ^= s.random >> 17;
    s.random ^= s.random << 5;
    return float(s.random >> 8) * (1.f / 16777216.f);
}
inline float between(ParticleSystem& s, float a, float b) {
    return a + (b - a) * random(s);
}
inline Vec3 sphere(ParticleSystem& s) {
    float y = between(s, -1, 1), angle = between(s, 0, 6.2831853f),
          r = std::sqrt(std::max(0.f, 1 - y * y));
    return {r * std::cos(angle), y, r * std::sin(angle)};
}
inline void step(ParticleSystem& s, const Transform& t, float dt, std::size_t& budget) {
    if (!s.started && s.playOnAwake) {
        s.play();
    }
    if (s.paused) {
        return;
    }
    dt = std::clamp(dt, 0.f, .1f);
    // Stopped non-cleared particles continue draining, but paused ones freeze.
    for (auto& p : s.particles) {
        p.age += dt;
        p.velocity = (p.velocity + s.gravity * dt) * (std::exp(-s.drag * dt));
        p.position = p.position + p.velocity * dt;
        p.rotation += radians(s.angularVelocity) * dt;
    }
    s.particles.erase(std::remove_if(s.particles.begin(),
                                     s.particles.end(),
                                     [](const Particle& p) { return p.age >= p.lifetime; }),
                      s.particles.end());
    int count = s.emitRequest;
    s.emitRequest = 0;
    if (s.playing) {
        const float previous = s.time;
        s.time += dt;
        if (s.time >= s.startDelay && s.emitting) {
            const float activeStart = std::max(previous, s.startDelay);
            const float activeEnd = s.loop ? s.time : std::min(s.time, s.startDelay + s.duration);
            if (!s.burstFired) {
                count += s.burstCount;
                s.burstFired = true;
            }
            if (activeEnd > activeStart) {
                s.accumulator += (activeEnd - activeStart) * s.rate;
                const int n = int(s.accumulator);
                count += n;
                s.accumulator -= n;
            }
            if (s.loop && s.time - s.startDelay >= s.duration) {
                s.time = s.startDelay + std::fmod(s.time - s.startDelay, s.duration);
                s.burstFired = false;
            } else if (!s.loop && s.time >= s.startDelay + s.duration) {
                s.emitting = false;
            }
        }
        if (!s.emitting && s.particles.empty()) {
            s.playing = false;
        }
    }
    const std::size_t capacity = std::size_t(std::clamp(s.maxParticles, 1, 10000));
    if (s.particles.size() > capacity) {
        s.particles.resize(capacity);
    }
    count = std::min({count, int(capacity - s.particles.size()), int(budget)});
    for (int i = 0; i < count; ++i) {
        Particle p;
        Vec3 offset{}, dir = normalize(s.direction);
        if (dot(dir, dir) < .001f) {
            dir = {0, 1, 0};
        }
        if (s.shape == 1) {
            offset = {between(s, -.5f, .5f) * s.boxSize.x,
                      between(s, -.5f, .5f) * s.boxSize.y,
                      between(s, -.5f, .5f) * s.boxSize.z};
        }
        if (s.shape == 2) {
            dir = sphere(s);
            offset = dir * (s.radius * std::cbrt(random(s)));
        }
        if (s.shape == 3) {
            const Vec3 right = normalize(
                           cross(std::abs(dir.y) < .9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}, dir)),
                       up = cross(dir, right);
            float a = between(s, 0, 6.2831853f), r = std::sqrt(random(s)) * s.radius;
            offset = (right * std::cos(a) + up * std::sin(a)) * r;
            const float cosine = between(s, std::cos(radians(s.coneAngle)), 1),
                        sine = std::sqrt(std::max(0.f, 1 - cosine * cosine));
            a = between(s, 0, 6.2831853f);
            dir = dir * cosine + (right * std::cos(a) + up * std::sin(a)) * sine;
        }
        p.position = offset;
        p.velocity = dir * between(s, s.speedMin, s.speedMax) + s.velocity;
        if (!s.localSpace) {
            p.position = t.position + rotateVector(t.rotation, p.position * t.scale);
            p.velocity = rotateVector(t.rotation, p.velocity);
        }
        p.lifetime = between(s, s.lifetimeMin, s.lifetimeMax);
        p.size = between(s, s.sizeMin, s.sizeMax);
        p.rotation = radians(between(s, s.rotationMin, s.rotationMax));
        s.particles.push_back(p);
    }
    budget -= std::size_t(count);
}
inline void
update(World& world, float dt, bool playing, bool paused, Entity preview, float previewSpeed) {
    std::size_t live = 0;
    world.each<ParticleSystem>([&](Entity, ParticleSystem& s) { live += s.particles.size(); });
    std::size_t budget = live >= 50000 ? 0 : 50000 - live;
    world.each<Transform, ParticleSystem>([&](Entity e, const Transform& t, ParticleSystem& s) {
        if (!playing && !paused && e != preview) {
            s.clear();
            s.started = false;
            s.playing = false;
            s.paused = false;
            return;
        }
        s.preview = !playing && !paused;
        if (!isActive(world, e) || paused) {
            return;
        }
        step(s, t, dt * (s.preview ? previewSpeed : 1.f), budget);
    });
}
inline void reset(World& world) {
    world.each<ParticleSystem>([](Entity, ParticleSystem& s) {
        s.stop();
        s.started = false;
        s.preview = false;
        s.time = 0;
    });
}
} // namespace Particles
