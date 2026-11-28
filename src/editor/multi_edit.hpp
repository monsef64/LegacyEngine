#pragma once
#include <imgui_internal.h>
#include <type_traits>

// Field-level edits: never paste an entire component or overwrite other objects'
// unrelated values/runtime state. Each field change is validated for the whole
// selection before publishing any assignments; normal history observes the batch.
#if defined(__GNUC__) || defined(__clang__)
#define LEGACY_BATCH_NOINLINE __attribute__((noinline))
#else
#define LEGACY_BATCH_NOINLINE
#endif
namespace MultiEdit {
template <class C> bool valid(const C& c) {
    if constexpr (std::is_base_of_v<LightSettings, C>) {
        return validLight(c);
    } else if constexpr (std::is_same_v<C, Camera>) {
        return validCamera(c);
    } else if constexpr (std::is_same_v<C, Fog>) {
        return Fogs::valid(c);
    } else if constexpr (std::is_same_v<C, CloudSystem> || std::is_base_of_v<River, C>) {
        return Environment::valid(c);
    } else if constexpr (std::is_same_v<C, Skybox>) {
        return validSkybox(c);
    } else if constexpr (std::is_same_v<C, CameraEffects>) {
        return CameraFx::valid(c);
    } else if constexpr (std::is_same_v<C, Vehicle>) {
        return Vehicles::valid(c);
    } else if constexpr (std::is_same_v<C, Laser>) {
        Lasers::parse(Lasers::settings(c));
        return true;
    } else if constexpr (std::is_same_v<C, LightShafts>) {
        Shafts::parse(Shafts::settings(c));
        return true;
    } else if constexpr (std::is_same_v<C, ParticleSystem>) {
        Particles::parse(Particles::settings(c));
        return true;
    } else if constexpr (std::is_same_v<C, Recoil>) {
        Recoils::parse(Recoils::settings(c));
        return true;
    } else if constexpr (std::is_same_v<C, TPSCamera>) {
        TPSCameras::parse(TPSCameras::settings(c));
        return true;
    } else if constexpr (std::is_same_v<C, NavMeshAgent>) {
        Navigation::parseAgent(Navigation::agentSettings(c));
        return true;
    } else if constexpr (std::is_same_v<C, NavMeshObstacle>) {
        Navigation::parseObstacle(Navigation::obstacleSettings(c));
        return true;
    } else if constexpr (std::is_same_v<C, BoneAttachment>) {
        BoneAttachments::parse(BoneAttachments::settings(c));
        return true;
    } else {
        return true;
    }
}
template <class C> bool header(World& world, const char* title) {
    for (auto e : hierarchySelection) {
        if (!world.has<C>(e)) {
            return false;
        }
    }
    return ImGui::CollapsingHeader(title);
}
template <class C, class Owner, class V>
LEGACY_BATCH_NOINLINE void field(World& world,
                                 Entity primary,
                                 const char* label,
                                 V Owner::* member,
                                 float minimum = 0,
                                 float maximum = 1000000) {
    auto* source = world.tryGet<C>(primary);
    if (!source) {
        return;
    }
    const V before = source->*member;
    V edited = before;
    bool mixed = false;
    for (auto e : hierarchySelection) {
        const auto& other = world.get<C>(e).*member;
        if constexpr (std::is_same_v<V, Vec3>) {
            mixed |= other.x != before.x || other.y != before.y || other.z != before.z;
        } else {
            mixed |= !(other == before);
        }
    }
    ImGui::PushID(label);
    if (mixed) {
        ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
    }
    bool changed = false;
    if constexpr (std::is_same_v<V, bool>) {
        changed = ImGui::Checkbox(label, &edited);
    } else if constexpr (std::is_same_v<V, Vec3>) {
        changed = editVector(label, edited, .05f, minimum, maximum, C{}.*member);
    } else if constexpr (std::is_same_v<V, std::string>) {
        char buffer[4097];
        std::snprintf(buffer, sizeof(buffer), "%s", edited.c_str());
        changed = ImGui::InputText(label, buffer, sizeof(buffer));
        if (changed) {
            edited = buffer;
            changed = materialPathValid(edited);
        }
    } else if constexpr (std::is_same_v<V, Entity>) {
        const auto* name = world.tryGet<ObjectName>(edited);
        ImGui::Button(name ? name->value.c_str() : "None (drop GameObject)");
        if (ImGui::BeginDragDropTarget()) {
            if (const auto* payload = ImGui::AcceptDragDropPayload("LEVEL_OBJECT")) {
                if (payload->DataSize == sizeof(Entity)) {
                    edited = *static_cast<const Entity*>(payload->Data);
                    changed = world.alive(edited);
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
            edited = {};
            changed = true;
        }
    } else if constexpr (std::is_enum_v<V> || std::is_integral_v<V>) {
        int value = int(edited);
        changed = ImGui::DragInt(
            label, &value, 1, int(minimum), int(maximum), "%d", ImGuiSliderFlags_AlwaysClamp);
        edited = V(value);
    } else {
        changed = ImGui::DragFloat(
            label, &edited, .05f, minimum, maximum, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    }
    if (mixed) {
        ImGui::PopItemFlag();
    }
    if (changed) {
        try {
            std::vector<std::pair<Entity, C>> updates;
            for (auto e : hierarchySelection) {
                C copy = world.get<C>(e);
                if constexpr (std::is_same_v<V, Vec3>) {
                    auto& dst = copy.*member;
                    if (before.x != edited.x) {
                        dst.x = edited.x;
                    }
                    if (before.y != edited.y) {
                        dst.y = edited.y;
                    }
                    if (before.z != edited.z) {
                        dst.z = edited.z;
                    }
                } else {
                    copy.*member = edited;
                }
                if (!valid(copy)) {
                    throw std::runtime_error("Value violates component constraints");
                }
                updates.emplace_back(e, std::move(copy));
            }
            for (auto& [e, copy] : updates) {
                if constexpr (std::is_base_of_v<LightSettings, C>) {
                    if (copy.mode != world.get<C>(e).mode) {
                        detachLightBake(world, world.get<C>(e));
                        copy.bake.clear();
                    }
                }
                world.get<C>(e) = std::move(copy);
            }
        } catch (const std::exception& error) {
            ImGui::SetTooltip("Not applied to any object: %s", error.what());
        }
    }
    ImGui::PopID();
}
LEGACY_BATCH_NOINLINE inline void draw(World& w, Entity p, bool runtime) {
#define F(C, n, lo, hi) field<C>(w, p, #n, &C::n, lo, hi)
#define B(C, n) F(C, n, 0, 1)
#define S(C, n) F(C, n, 0, 1000000)
#define V(C, n) F(C, n, -1000000, 1000000)
#define H(C) if (header<C>(w, #C))
    H(MeshRenderer) {
        S(MeshRenderer, model);
        S(MeshRenderer, material);
        F(MeshRenderer, color, 0, 100);
        B(MeshRenderer, checkerboard);
        B(MeshRenderer, castShadow);
        F(MeshRenderer, specular, 0, 1);
    }
    H(Collider) {
        F(Collider, shape, 0, 4);
        F(Collider, size, .001f, 1000000);
        V(Collider, center);
        F(Collider, radius, .001f, 1000000);
        F(Collider, height, .001f, 1000000);
        F(Collider, friction, 0, 1);
        F(Collider, bounciness, 0, 1);
        B(Collider, isTrigger);
        S(Collider, model);
        F(Collider, meshDetail, 2, 15);
    }
    H(RigidBody) {
        S(RigidBody, mass);
        B(RigidBody, useGravity);
        B(RigidBody, isKinematic);
        B(RigidBody, floatOnRivers);
        S(RigidBody, drag);
        S(RigidBody, angularDrag);
        V(RigidBody, linearVelocity);
        V(RigidBody, angularVelocity);
    }

    H(LuaScript) {
        B(LuaScript, enabled);
        S(LuaScript, path);
        ImGui::TextDisabled(
            "Additional script attachments and exported references remain per-object.");
    }
    H(Animation) {
        S(Animation, clip);
        B(Animation, loop);
        B(Animation, playOnStart);
    }
    H(AudioListener) {
        B(AudioListener, enabled);
    }
    H(AudioSource) {
        S(AudioSource, clip);
        F(AudioSource, volume, 0, 1);
        F(AudioSource, pitch, .1f, 4);
        F(AudioSource, pan, -1, 1);
        S(AudioSource, distance);
        F(AudioSource, blend, 0, 1);
        B(AudioSource, looping);
        B(AudioSource, muted);
        B(AudioSource, playOnStart);
    }
    auto light = [&](auto tag, const char* title) {
        using C = decltype(tag);
        if (!header<C>(w, title)) {
            return;
        }
        ImGui::BeginDisabled(runtime);
        field<C>(w, p, "Mode (0 Dynamic, 1 Static)", &LightSettings::mode, 0, 1);
        ImGui::EndDisabled();
        field<C>(w, p, "Enabled", &LightSettings::enabled);
        field<C>(w, p, "Color", &LightSettings::color, 0, 100);
        field<C>(w, p, "Intensity", &LightSettings::intensity, 0, 1000);
        field<C>(w, p, "Shadows", &LightSettings::shadows);
        field<C>(w, p, "Range", &LightSettings::range, .1f, 10000);
        field<C>(w, p, "Source softness", &LightSettings::softness, 0, 10);
        field<C>(w, p, "Shadow distance", &LightSettings::shadowDistance, 1, 10000);
        field<C>(w, p, "Shadow quality", &LightSettings::shadowQuality, 0, 3);
        field<C>(w, p, "Shadow bias", &LightSettings::shadowBias, 0, .1f);
        if constexpr (std::is_same_v<C, SpotLight>) {
            field<C>(w, p, "Inner angle", &LightSettings::innerAngle, 0, 89);
            field<C>(w, p, "Outer angle", &LightSettings::outerAngle, .1f, 89.9f);
            field<C>(w, p, "Cookie", &LightSettings::cookie);
        }
    };
    light(DirectionalLight{}, "DirectionalLight");
    light(PointLight{}, "PointLight");
    light(SpotLight{}, "SpotLight");
    H(Camera) {
        B(Camera, orthographic);
        F(Camera, fieldOfView, 1, 179);
        F(Camera, nearClip, .001f, 10000);
        F(Camera, farClip, .01f, 1000000);
        F(Camera, orthographicSize, .01f, 100000);
        F(Camera, viewportX, 0, 1);
        F(Camera, viewportY, 0, 1);
        F(Camera, viewportWidth, .001f, 1);
        F(Camera, viewportHeight, .001f, 1);
    }
    H(FPSCamera) {
        B(FPSCamera, isFreecam);
        B(FPSCamera, mouseLook);
        F(FPSCamera, sensitivity, .001f, 10);
        S(FPSCamera, speed);
    }
    H(Recoil) {
        B(Recoil, enabled);
        B(Recoil, playOnStart);
        B(Recoil, sequenceLoop);
        S(Recoil, burstCount);
        S(Recoil, seed);
        for (const auto& f : Recoils::fields) {
            field<Recoil>(w, p, f.name, f.member, f.low, f.high);
        }
    }
    H(TPSCamera) {
        B(TPSCamera, enabled);
        S(TPSCamera, target);
        V(TPSCamera, offset);
        B(TPSCamera, mouseLook);
        B(TPSCamera, lockMouse);
        B(TPSCamera, collision);
        S(TPSCamera, distance);
        S(TPSCamera, minDistance);
        S(TPSCamera, maxDistance);
        F(TPSCamera, yaw, -360, 360);
        F(TPSCamera, pitch, -89, 89);
        F(TPSCamera, minPitch, -89, 89);
        F(TPSCamera, maxPitch, -89, 89);
        S(TPSCamera, sensitivity);
        S(TPSCamera, zoomSpeed);
        S(TPSCamera, smoothing);
        F(TPSCamera, shoulder, -10, 10);
        F(TPSCamera, collisionRadius, .02f, 5);
    }
    H(CharacterController) {
        S(CharacterController, speed);
        S(CharacterController, sprintMultiplier);
        S(CharacterController, jumpSpeed);
        F(CharacterController, groundProbe, .001f, 10);
        B(CharacterController, canMove);
        B(CharacterController, canSprint);
        B(CharacterController, canJump);
    }
    H(Raycast) {
        F(Raycast, type, 0, 2);
        S(Raycast, distance);
        S(Raycast, radius);
        F(Raycast, angle, 0, 360);
        F(Raycast, samples, 1, 256);
        V(Raycast, offset);
        B(Raycast, includeTriggers);
    }
    H(NavMeshAgent) {
        B(NavMeshAgent, enabled);
        F(NavMeshAgent, speed, 0, 100);
        F(NavMeshAgent, stoppingDistance, .01f, 10);
        B(NavMeshAgent, updateRotation);
        F(NavMeshAgent, angularSpeed, 0, 3600);
    }
    H(NavMeshObstacle) {
        B(NavMeshObstacle, enabled);
        F(NavMeshObstacle, size, .001f, 1000000);
        V(NavMeshObstacle, center);
    }
    H(BoneAttachment) {
        B(BoneAttachment, enabled);
        S(BoneAttachment, source);
        F(BoneAttachment, node, -1, 4095);
        V(BoneAttachment, offset);
        F(BoneAttachment, rotation, -360, 360);
    }
    H(Vehicle) {
        B(Vehicle, enabled);
#define VF(n, initial, lo, hi) F(Vehicle, n, lo, hi);
        VEHICLE_SETTINGS(VF)
#undef VF
    }
    H(Fog) {
        B(Fog, enabled);
        F(Fog, color, 0, 1);
        F(Fog, density, 0, 10);
        S(Fog, start);
        S(Fog, end);
    }
    H(CloudSystem) {
        B(CloudSystem, enabled);
        B(CloudSystem, volumetric);
        F(CloudSystem, color, 0, 1);
        F(CloudSystem, size, .1f, 10000);
        F(CloudSystem, coverage, 0, 1);
        F(CloudSystem, density, 0, 10);
        F(CloudSystem, scale, .001f, 10);
        F(CloudSystem, speed, 0, 10);
        F(CloudSystem, opacity, 0, 1);
        F(CloudSystem, steps, 4, 32);
    }
    auto water = [&](auto tag, const char* title) {
        using C = decltype(tag);
        if (!header<C>(w, title)) {
            return;
        }
#define W(n, lo, hi) field<C>(w, p, #n, &River::n, lo, hi)
        W(enabled, 0, 1);
        W(flowing, 0, 1);
        W(transparent, 0, 1);
        W(collisionEffects, 0, 1);
        W(color, 0, 1);
        W(width, .1f, 1000);
        W(opacity, 0, 1);
        W(speed, 0, 100);
        W(flowAmount, 0, 10);
        W(collisionStrength, 0, 10);
        W(waveScale, .01f, 100);
        W(waveStrength, 0, 10);
        W(foam, 0, 1);
        W(subdivisions, 2, 32);
        W(shaderStyle, 0, 7);
        W(waveHeight, 0, 3);
        W(waveLength, .1f, 100);
        W(waveSpeed, 0, 10);
#undef W
        if constexpr (std::is_same_v<C, Water>)
            S(Water, mesh);
        ImGui::TextDisabled("Curve control points remain per-object.");
    };
    water(River{}, "River");
    water(Water{}, "Water");
    H(Laser) {
#define L(n, lo, hi) field<Laser>(w, p, #n, &LaserSettings::n, lo, hi)
        L(enabled, 0, 1);
        L(color, 0, 100);
        L(intensity, 0, 100);
        L(range, .01f, 10000);
        L(width, .001f, 10);
        L(glowWidth, .001f, 100);
        L(impactSize, .001f, 100);
#undef L
    }
    H(LightShafts) {
        B(LightShafts, enabled);
        V(LightShafts, direction);
        F(LightShafts, color, 0, 100);
        S(LightShafts, height);
        S(LightShafts, width);
        S(LightShafts, endWidth);
        S(LightShafts, intensity);
        S(LightShafts, density);
        S(LightShafts, falloff);
        S(LightShafts, texture);
        B(LightShafts, useBakedTexture);
    }
    H(ParticleSystem) {
#define P(n, lo, hi) field<ParticleSystem>(w, p, #n, &ParticleSettings::n, lo, hi)
        P(loop, 0, 1);
        P(playOnAwake, 0, 1);
        P(localSpace, 0, 1);
        P(collision, 0, 1);
        P(useSpriteSheet, 0, 1);
        P(duration, .01f, 3600);
        P(startDelay, 0, 3600);
        P(rate, 0, 10000);
        P(maxParticles, 1, 50000);
        P(burstCount, 0, 50000);
        P(shape, 0, 3);
        P(blend, 0, 1);
        P(sprite, 0, 15);
        P(lifetimeMin, .01f, 3600);
        P(lifetimeMax, .01f, 3600);
        P(speedMin, 0, 1000);
        P(speedMax, 0, 1000);
        P(sizeMin, 0, 100);
        P(sizeMax, 0, 100);
        P(endSize, 0, 100);
        P(color, 0, 100);
        P(endColor, 0, 100);
        P(alpha, 0, 1);
        P(endAlpha, 0, 1);
        P(direction, -1000, 1000);
        P(velocity, -1000, 1000);
        P(gravity, -1000, 1000);
        P(boxSize, 0, 1000);
        P(radius, 0, 1000);
        P(coneAngle, 0, 180);
        P(drag, 0, 100);
        P(rotationMin, -360, 360);
        P(rotationMax, -360, 360);
        P(angularVelocity, -3600, 3600);
        P(restitution, 0, 1);
        P(collisionFriction, 0, 1);
        P(seed, 0, 1000000);
        P(sheetColumns, 1, 64);
        P(sheetRows, 1, 64);
        P(frameRate, 0, 1000);
        P(texture, 0, 1);
#undef P
    }
    H(CameraEffects) {
        for (const auto& f : CameraFx::boolFields) {
            field<CameraEffects>(w, p, f.label, f.member);
        }
        for (const auto& f : CameraFx::floatFields) {
            field<CameraEffects>(w, p, f.label, f.member, f.low, f.high);
        }
    }
    H(Skybox) {
        B(Skybox, enabled);
        F(Skybox, type, 0, 1);
        F(Skybox, color, 0, 100);
        B(Skybox, affectEnvironment);
        F(Skybox, environmentIntensity, 0, 5);
        S(Skybox, cubemap);
        S(Skybox, hdr);
        S(Skybox, right);
        S(Skybox, left);
        S(Skybox, top);
        S(Skybox, bottom);
        S(Skybox, front);
        S(Skybox, back);
    }
    H(Terrain) {
        S(Terrain, asset);
        ImGui::TextDisabled(
            "Sculpt/paint/foliage brushes remain per terrain to preserve distinct heightfields.");
    }
#undef H
#undef V
#undef S
#undef B
#undef F
}
} // namespace MultiEdit

#undef LEGACY_BATCH_NOINLINE
