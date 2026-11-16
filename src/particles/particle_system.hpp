#pragma once
#include "core/math.hpp"
#include <cstdint>
#include <string>
#include <vector>

struct ParticleSettings {
    bool loop = true, playOnAwake = true, localSpace = false;
    bool collision = false;
    bool useSpriteSheet = false; // opt-in; a single complete sprite remains the default
    float restitution = .5f, collisionFriction = .1f;
    float duration = 5, startDelay = 0, rate = 40;
    int maxParticles = 1000, burstCount = 0, shape = 3, blend = 1, sprite = 0;
    float lifetimeMin = .6f, lifetimeMax = 1.2f, speedMin = .5f, speedMax = 1.5f;
    float sizeMin = .15f, sizeMax = .35f, endSize = 0;
    Vec3 color{1, .35f, .05f}, endColor{.2f, .02f, 0};
    float alpha = 1, endAlpha = 0;
    Vec3 direction{0, 1, 0}, velocity{}, gravity{0, .3f, 0}, boxSize{1, 1, 1};
    float radius = .15f, coneAngle = 15, drag = .1f;
    float rotationMin = -180, rotationMax = 180, angularVelocity = 30;
    int seed = 1, sheetColumns = 1, sheetRows = 1;
    float frameRate = 0;
    std::string texture; // empty uses bundled sprite; otherwise project-relative image
};
struct Particle {
    Vec3 position, velocity;
    float age = 0, lifetime = 1, size = 1, rotation = 0;
};
struct ParticleSystem : ParticleSettings {
    std::vector<Particle> particles;
    bool started = false, playing = false, paused = false, emitting = false, preview = false,
         burstFired = false;
    float time = 0, accumulator = 0;
    std::uint32_t random = 1;
    int emitRequest = 0;
    void clear() {
        particles.clear();
        accumulator = 0;
        emitRequest = 0;
    }
    void play() {
        if (!started || (!playing && !paused)) {
            time = 0;
            accumulator = 0;
            burstFired = false;
            random = std::uint32_t(seed) ? std::uint32_t(seed) : 1u;
        }
        started = true;
        playing = true;
        paused = false;
        emitting = true;
    }
    void pause() {
        if (playing) {
            paused = true;
            playing = false;
        }
    }
    void resume() {
        if (paused) {
            paused = false;
            playing = true;
        }
    }
    void stop(bool clearParticles = true) {
        playing = false;
        paused = false;
        emitting = false;
        started = true;
        if (clearParticles) {
            clear();
        }
    }
    void restart() {
        stop();
        started = false;
        play();
    }
    void emit(int count) {
        emitRequest = std::min(10000, emitRequest + std::clamp(count, 0, 10000));
        started = true;
    }
};
inline void applyParticlePreset(ParticleSystem& system, int preset) {
    ParticleSettings s;
    if (preset == 1) {
        s.sprite = 1;
        s.blend = 0;
        s.rate = 12;
        s.lifetimeMin = 2;
        s.lifetimeMax = 4;
        s.speedMin = .15f;
        s.speedMax = .4f;
        s.sizeMin = .3f;
        s.sizeMax = .6f;
        s.endSize = 2;
        s.color = {.5f, .5f, .5f};
        s.endColor = {.18f, .18f, .18f};
        s.alpha = .35f;
        s.gravity = {0, .05f, 0};
        s.angularVelocity = 12;
        s.coneAngle = 25;
        s.radius = .25f;
    } else if (preset == 2) {
        s.sprite = 2;
        s.blend = 1;
        s.shape = 2;
        s.radius = .3f;
        s.rate = 25;
        s.color = {1, .85f, .3f};
        s.endColor = {1, .4f, .1f};
        s.sizeMin = .04f;
        s.sizeMax = .12f;
        s.speedMin = .4f;
        s.speedMax = 1;
        s.gravity = {0, -.2f, 0};
        s.angularVelocity = 90;
    }
    static_cast<ParticleSettings&>(system) = s;
    system.clear();
    system.started = false;
    system.playing = false;
    system.paused = false;
    system.emitting = false;
}
