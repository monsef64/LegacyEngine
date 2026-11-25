#pragma once
#include "core/hierarchy.hpp"
#include "editor/editor_layout.hpp"
#include "scene/camera.hpp"
#include <imgui.h>
#include <map>
#include <set>

// One primary component badge per object. Follow Inspector order, but never
// manufacture a badge for Transform or editor/name metadata.
struct SceneComponentBadge {
    const char* name;
    const char* icon;
};
inline SceneComponentBadge sceneComponentBadge(World& world, Entity entity) {
    auto badge = [&](int id) -> SceneComponentBadge {
#define BADGE(Id, Type, Glyph)                                                                     \
    case Id:                                                                                       \
        if (world.has<Type>(entity))                                                               \
            return {#Type, Glyph};                                                                 \
        break
        switch (id) {
            BADGE(1, RigidBody, "\xef\x86\xb2");
            BADGE(2, Collider, "\xef\x86\xb2");
            BADGE(3, MeshRenderer, "\xef\x86\xb2");
            BADGE(5, LuaScript, "\xef\x87\x89");
            BADGE(6, Camera, "\xef\x80\xbd");
            BADGE(7, AudioSource, "\xef\x80\xa8");
            BADGE(8, AudioListener, "\xef\x80\xa8");
            BADGE(9, DirectionalLight, "\xef\x86\x85");
            BADGE(10, PointLight, "\xef\x83\xab");
            BADGE(11, SpotLight, "\xef\x83\xab");
            BADGE(12, Skybox, "\xef\x86\x85");
            BADGE(13, CharacterController, "\xef\x80\x93");
            BADGE(14, FPSCamera, "\xef\x80\xbd");
            BADGE(15, Raycast, "\xef\x80\x93");
            BADGE(16, Animation, "\xef\x80\x88");
            BADGE(17, ParticleSystem, "\xef\x80\x93");
            BADGE(18, Laser, "\xef\x80\x93");
            BADGE(19, LightShafts, "\xef\x83\xab");
            BADGE(20, Terrain, "\xef\x9b\xbc");
            BADGE(21, CameraEffects, "\xef\x80\xbd");
            BADGE(22, Fog, "\xef\x80\x93");
            BADGE(23, CloudSystem, "\xef\x80\x93");
            BADGE(24, River, "\xef\x80\x93");
            BADGE(25, Vehicle, "\xef\x86\xb2");
            BADGE(26, Water, "\xef\x80\x93");
            BADGE(27, BoneAttachment, "\xef\x80\x93");
            BADGE(28, TPSCamera, "\xef\x80\xbd");
            BADGE(29, NavMeshAgent, "\xef\x80\x93");
            BADGE(30, NavMeshObstacle, "\xef\x86\xb2");
            BADGE(31, Animator, "\xef\x80\x88");
            BADGE(32, UI, "\xef\x80\x93");
            BADGE(33, Decal, "\xef\x87\x85");
            BADGE(34, Recoil, "\xef\x80\xbd");
        default:
            break;
        }
#undef BADGE
        return {nullptr, nullptr};
    };
    if (const auto* order = world.tryGet<InspectorOrder>(entity)) {
        for (int id : order->types) {
            auto result = badge(id);
            if (result.name) {
                return result;
            }
        }
    }
    for (int id : {3,  2,  1,  6,  9,  10, 11, 12, 8,  7,  5,  13, 14, 15, 16, 17, 18,
                   19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34}) {
        auto result = badge(id);
        if (result.name) {
            return result;
        }
    }
    return {nullptr, nullptr};
}

// Breadth-first traversal makes parents own coincident icon/picking locations.
// Rendering/picking here is editor-only: no components or game cameras are changed.
inline bool drawSceneComponentGizmos(World& world,
                                     const OrbitCamera& camera,
                                     const EditorRect& rect,
                                     Entity selected,
                                     bool interactive,
                                     Entity& clicked) {
    using Key = std::pair<unsigned, unsigned>;
    auto key = [](Entity e) { return Key{e.index, e.generation}; };
    std::map<Key, std::vector<Entity>> children;
    std::vector<Entity> ordered;
    for (auto e : world.entities()) {
        const auto parent = getParent(world, e);
        if (world.alive(parent)) {
            children[key(parent)].push_back(e);
        } else {
            ordered.push_back(e);
        }
    }
    std::set<Key> visited;
    std::map<std::pair<int, int>, std::vector<ImVec2>> occupied;
    const auto view = multiply(camera.projection(rect.width / std::max(1.f, rect.height)),
                               lookAt(camera.position(), camera.target, camera.up()));
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect({rect.x, rect.y}, {rect.x + rect.width, rect.y + rect.height}, true);
    bool hovered = false;
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        const auto entity = ordered[i];
        if (!visited.insert(key(entity)).second) {
            continue;
        }
        const auto found = children.find(key(entity));
        if (found != children.end()) {
            ordered.insert(ordered.end(), found->second.begin(), found->second.end());
        }
        const auto* t = world.tryGet<Transform>(entity);
        if (!t || !isActive(world, entity)) {
            continue;
        }
        const auto badge = sceneComponentBadge(world, entity);
        if (!badge.name) {
            continue;
        }
        const auto p = t->position;
        const auto& m = view.m;
        const float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
        if (w <= .00001f) {
            continue;
        }
        const float x = (m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12]) / w;
        const float y = (m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13]) / w;
        const float z = (m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]) / w;
        if (std::abs(x) > 1 || std::abs(y) > 1 || std::abs(z) > 1) {
            continue;
        }
        const ImVec2 centre{rect.x + (x + 1) * .5f * rect.width,
                            rect.y + (1 - y) * .5f * rect.height};
        // Reserve the orientation cube and selected-camera preview overlays.
        if (centre.x > rect.x + rect.width - 126 && centre.y < rect.y + 126) {
            continue;
        }
        if (const auto* cameraPreview = world.tryGet<Camera>(selected)) {
            const float aspect = std::clamp(cameraPreview->aspect, .25f, 4.f);
            float width = std::min(240.f, rect.width * .35f), height = width / aspect;
            if (height > rect.height * .4f) {
                height = rect.height * .4f;
                width = height * aspect;
            }
            if (centre.x > rect.x + rect.width - width - 12 &&
                centre.y > rect.y + rect.height - height - 32) {
                continue;
            }
        }
        const int cellX = int(std::floor(centre.x / 24)), cellY = int(std::floor(centre.y / 24));
        bool overlaps = false;
        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                const auto bucket = occupied.find({cellX + dx, cellY + dy});
                if (bucket == occupied.end()) {
                    continue;
                }
                for (const auto other : bucket->second) {
                    if (std::abs(other.x - centre.x) < 24 && std::abs(other.y - centre.y) < 24) {
                        overlaps = true;
                        break;
                    }
                }
            }
        }
        if (overlaps) {
            continue;
        }
        occupied[{cellX, cellY}].push_back(centre);
        const ImVec2 low{centre.x - 11, centre.y - 11}, high{centre.x + 11, centre.y + 11};
        draw->AddRectFilled(low, high, IM_COL32(25, 30, 40, 220), 4);
        draw->AddRect(low,
                      high,
                      entity == selected ? IM_COL32(255, 200, 75, 255)
                                         : IM_COL32(170, 195, 220, 255),
                      4);
        const auto size = ImGui::CalcTextSize(badge.icon);
        draw->AddText(
            {centre.x - size.x * .5f, centre.y - size.y * .5f}, IM_COL32_WHITE, badge.icon);
        if (interactive && entity != selected && ImGui::IsWindowHovered() &&
            ImGui::IsMouseHoveringRect(low, high)) {
            hovered = true;
            const auto* name = world.tryGet<ObjectName>(entity);
            ImGui::SetTooltip("%s — %s", name ? name->value.c_str() : "GameObject", badge.name);
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                clicked = entity;
            }
        }
    }
    draw->PopClipRect();
    return hovered;
}
