#pragma once
#include "core/math.hpp"
#include "project/json_io.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// Local-space, render-only spring offsets. Never write the controller's Transform.
struct Recoil {
    bool enabled = true, playOnStart = false, sequenceLoop = false;
    float pitch = 2, yaw = 0, roll = 0, back = .025f, up = 0, side = 0;
    float rotationRandom = .35f, positionRandom = .1f, frequency = 8, damping = .8f;
    float positionFrequency = 12, positionDamping = .9f, maxAngle = 15, maxDistance = .25f;
    float interval = .1f, duration = 0, delay = 0, strength = 1, recovery = 1;
    int burstCount = 0, seed = 1;
    // Transient state is excluded from settings, undo fingerprints and serialization.
    Vec3 angles{}, offset{}, angleVelocity{}, velocity{};
    bool initialized = false, firing = false, sequencePlaying = false;
    unsigned randomState = 1, shots = 0;
    float elapsed = 0, nextShot = 0, sequenceTime = 0;
    std::size_t key = 0;
    struct Key {
        float time = 0, strength = 1;
    };
    std::vector<Key> sequence;
    float random() {
        if (!randomState) {
            randomState = 1;
        }
        randomState ^= randomState << 13;
        randomState ^= randomState >> 17;
        randomState ^= randomState << 5;
        return float(randomState & 0xffffff) / 8388607.5f - 1.f;
    }
    void trigger(float strengthScale = 1) {
        if (!enabled || !std::isfinite(strengthScale) || strengthScale < 0 || strengthScale > 10) {
            return;
        }
        if (!initialized) {
            randomState = unsigned(seed);
            initialized = true;
        }
        const float s = strength * strengthScale;
        // Convert authored displacement to an impulse, preserving a similar feel
        // when the spring frequency changes. Angles in degrees; positions in metres.
        const float r = s * 6.2831853f * frequency, p = s * 6.2831853f * positionFrequency;
        angleVelocity = angleVelocity + Vec3{pitch + random() * rotationRandom,
                                             yaw + random() * rotationRandom,
                                             roll + random() * rotationRandom} *
                                            r;
        velocity = velocity + Vec3{side + random() * std::abs(back) * positionRandom,
                                   up + random() * std::abs(back) * positionRandom,
                                   back * (1 + random() * positionRandom)} *
                                  p;
    }
    void start() {
        firing = true;
        elapsed = 0;
        nextShot = delay;
        shots = 0;
    }
    void startSequence() {
        sequencePlaying = true;
        sequenceTime = -delay;
        key = 0;
    }
    void stop() {
        firing = false;
        sequencePlaying = false;
    }
    void reset() {
        stop();
        angles = offset = angleVelocity = velocity = {};
        elapsed = nextShot = sequenceTime = 0;
        shots = 0;
        key = 0;
        randomState = unsigned(seed);
    }
};
namespace Recoils {
struct Field {
    const char* name;
    float Recoil::* member;
    float low, high;
};
inline constexpr Field fields[]{{"pitch", &Recoil::pitch, -45, 45},
                                {"yaw", &Recoil::yaw, -45, 45},
                                {"roll", &Recoil::roll, -45, 45},
                                {"back", &Recoil::back, -1, 1},
                                {"up", &Recoil::up, -1, 1},
                                {"side", &Recoil::side, -1, 1},
                                {"rotationRandom", &Recoil::rotationRandom, 0, 45},
                                {"positionRandom", &Recoil::positionRandom, 0, 2},
                                {"frequency", &Recoil::frequency, .1f, 40},
                                {"damping", &Recoil::damping, .1f, 3},
                                {"positionFrequency", &Recoil::positionFrequency, .1f, 40},
                                {"positionDamping", &Recoil::positionDamping, .1f, 3},
                                {"maxAngle", &Recoil::maxAngle, 0, 89},
                                {"maxDistance", &Recoil::maxDistance, 0, 2},
                                {"interval", &Recoil::interval, .01f, 60},
                                {"duration", &Recoil::duration, 0, 3600},
                                {"delay", &Recoil::delay, 0, 3600},
                                {"strength", &Recoil::strength, 0, 10},
                                {"recovery", &Recoil::recovery, .1f, 4}};
inline Json settings(const Recoil& c) {
    Json j{{"enabled", c.enabled},
           {"playOnStart", c.playOnStart},
           {"sequenceLoop", c.sequenceLoop},
           {"burstCount", c.burstCount},
           {"seed", c.seed}};
    for (const auto& f : fields) {
        j[f.name] = c.*(f.member);
    }
    j["sequence"] = Json::array();
    for (const auto& k : c.sequence) {
        j["sequence"].push_back({{"time", k.time}, {"strength", k.strength}});
    }
    return j;
}
inline Recoil parse(const Json& j) {
    if (!j.is_object()) {
        throw std::runtime_error("Expected Recoil settings object");
    }
    Recoil c;
    c.enabled = j.value("enabled", true);
    c.playOnStart = j.value("playOnStart", false);
    c.sequenceLoop = j.value("sequenceLoop", false);
    c.burstCount = j.value("burstCount", 0);
    c.seed = j.value("seed", 1);
    if (c.burstCount < 0 || c.burstCount > 100000 || c.seed < 0) {
        throw std::runtime_error("Invalid recoil burst count/seed");
    }
    for (const auto& f : fields) {
        const float v = j.value(f.name, c.*(f.member));
        if (!std::isfinite(v) || v < f.low || v > f.high) {
            throw std::runtime_error(std::string("Recoil value outside range: ") + f.name);
        }
        c.*(f.member) = v;
    }
    if (j.contains("sequence")) {
        const auto& keys = j.at("sequence");
        if (!keys.is_array() || keys.size() > 256) {
            throw std::runtime_error("Recoil sequence supports up to 256 keys");
        }
        float previous = -1;
        for (const auto& k : keys) {
            const float t = k.at("time"), s = k.value("strength", 1.f);
            if (!std::isfinite(t) || !std::isfinite(s) || t < previous || t < 0 || t > 3600 ||
                s < 0 || s > 10) {
                throw std::runtime_error("Invalid recoil sequence key");
            }
            c.sequence.push_back({t, s});
            previous = t;
        }
    }
    return c;
}
// Exact damped harmonic integration, stable even at the highest frequency.
inline void spring(float& x, float& v, float w, float z, float dt) {
    const float a = z * w;
    if (std::abs(z - 1) < .001f) {
        const float b = v + w * x, e = std::exp(-w * dt);
        v = (v - w * b * dt) * e;
        x = (x + b * dt) * e;
    } else if (z < 1) {
        const float b = w * std::sqrt(1 - z * z), e = std::exp(-a * dt), cs = std::cos(b * dt),
                    sn = std::sin(b * dt), old = x;
        x = e * (old * cs + (v + a * old) / b * sn);
        v = e * (v * cs - (a * v + w * w * old) / b * sn);
    } else {
        const float b = w * std::sqrt(z * z - 1), r1 = -a + b, r2 = -a - b,
                    c1 = (v - r2 * x) / (r1 - r2), c2 = x - c1, e1 = std::exp(r1 * dt),
                    e2 = std::exp(r2 * dt);
        x = c1 * e1 + c2 * e2;
        v = r1 * c1 * e1 + r2 * c2 * e2;
    }
}
inline void clamp(Vec3& x, Vec3& v, float limit) {
    const float length = std::sqrt(dot(x, x));
    if (length > limit) {
        x = x * (limit / length);
        if (dot(x, v) > 0) {
            v = {};
        }
    }
}
inline void update(Recoil& c, float dt) {
    if (!std::isfinite(dt) || dt < 0) {
        return;
    }
    if (!c.initialized) {
        c.randomState = unsigned(c.seed);
        c.initialized = true;
        if (c.playOnStart) {
            if (c.sequence.empty()) {
                c.start();
            } else {
                c.startSequence();
            }
        }
    }
    if (!c.enabled) {
        c.reset();
        return;
    }
    // Integrate the timeline in bounded substeps so impulses at scheduled times do
    // not all land at the end of a long frame. Simulation follows engine game time.
    dt = std::min(dt, .25f);
    while (dt > 0) {
        const float step = std::min(dt, 1.f / 240);
        dt -= step;
        if (c.sequencePlaying) {
            if (c.sequence.empty()) {
                c.sequencePlaying = false;
            } else {
                c.sequenceTime += step;
                while (c.key < c.sequence.size() && c.sequence[c.key].time <= c.sequenceTime) {
                    c.trigger(c.sequence[c.key].strength);
                    ++c.key;
                }
                const float period = c.sequence.back().time + c.interval;
                if (c.key >= c.sequence.size()) {
                    if (!c.sequenceLoop) {
                        c.sequencePlaying = false;
                    } else if (c.sequenceTime >= period) {
                        c.sequenceTime = std::fmod(c.sequenceTime, period);
                        c.key = 0;
                    }
                }
            }
        }
        if (c.firing) {
            c.elapsed += step;
            while (c.firing && c.elapsed >= c.nextShot) {
                if (c.duration > 0 && c.nextShot >= c.delay + c.duration) {
                    c.firing = false;
                    break;
                }
                c.trigger();
                ++c.shots;
                c.nextShot += c.interval;
                if (c.burstCount > 0 && c.shots >= unsigned(c.burstCount)) {
                    c.firing = false;
                }
            }
        }
        float* x[]{&c.angles.x, &c.angles.y, &c.angles.z, &c.offset.x, &c.offset.y, &c.offset.z};
        float* v[]{&c.angleVelocity.x,
                   &c.angleVelocity.y,
                   &c.angleVelocity.z,
                   &c.velocity.x,
                   &c.velocity.y,
                   &c.velocity.z};
        for (int i = 0; i < 6; ++i) {
            spring(*x[i],
                   *v[i],
                   6.2831853f * (i < 3 ? c.frequency : c.positionFrequency) * c.recovery,
                   i < 3 ? c.damping : c.positionDamping,
                   step);
        }
        clamp(c.angles, c.angleVelocity, c.maxAngle);
        clamp(c.offset, c.velocity, c.maxDistance);
    }
}
} // namespace Recoils
