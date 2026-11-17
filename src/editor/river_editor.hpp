#pragma once
#include "editor/editor_history.hpp"
#include "editor/editor_layout.hpp"
#include "scene/camera.hpp"
#include <imgui.h>

inline bool drawRiverCurve(World& world,
                           Entity selected,
                           OrbitCamera& camera,
                           const EditorRect& rect,
                           EditorHistory& history,
                           bool enabled) {
    auto* river = world.tryGet<River>(selected);
    const auto* t = world.tryGet<Transform>(selected);
    if (!river || !t || !river->editCurve || !Environment::valid(*river) || rect.width < 1 ||
        rect.height < 1) {
        return false;
    }
    const Mat4 vp = camera.viewProjection(rect.width / rect.height);
    auto* draw = ImGui::GetWindowDrawList();
    auto worldPoint = [&](Vec3 p) { return t->position + rotateVector(t->rotation, p * t->scale); };
    auto screen = [&](Vec3 p, ImVec2& out) {
        p = worldPoint(p);
        float w = vp.m[3] * p.x + vp.m[7] * p.y + vp.m[11] * p.z + vp.m[15];
        if (w <= .001f) {
            return false;
        }
        float x = (vp.m[0] * p.x + vp.m[4] * p.y + vp.m[8] * p.z + vp.m[12]) / w,
              y = (vp.m[1] * p.x + vp.m[5] * p.y + vp.m[9] * p.z + vp.m[13]) / w;
        out = {rect.x + (x + 1) * .5f * rect.width, rect.y + (1 - y) * .5f * rect.height};
        return true;
    };
    draw->PushClipRect({rect.x, rect.y}, {rect.x + rect.width, rect.y + rect.height}, true);
    ImVec2 previous;
    bool last = false;
    for (int i = 0; i <= 128; ++i) {
        ImVec2 p;
        bool ok = screen(Environment::curve(*river, float(i) / 128), p);
        if (ok && last) {
            draw->AddLine(previous, p, IM_COL32(55, 210, 245, 255), 2);
        }
        previous = p;
        last = ok;
    }
    auto cursor = ImGui::GetCursorScreenPos();
    bool interacting = false;
    ImGui::PushID("RiverCurve");
    ImGui::PushID(int(selected.index));
    for (int i = 0; i < int(river->points.size()); ++i) {
        ImVec2 p;
        if (!screen(river->points[i], p) || p.x < rect.x || p.y < rect.y ||
            p.x > rect.x + rect.width || p.y > rect.y + rect.height) {
            continue;
        }
        ImGui::PushID(i);
        ImGui::SetCursorScreenPos({p.x - 8, p.y - 8});
        ImGui::BeginDisabled(!enabled);
        ImGui::InvisibleButton("Point", {16, 16});
        bool active = ImGui::IsItemActive();
        interacting |= active || ImGui::IsItemHovered();
        if (ImGui::IsItemActivated()) {
            history.commit(world, selected);
        }
        if (active && ImGui::IsMouseDragging(0) &&
            std::abs(t->scale.x * t->scale.y * t->scale.z) > 1e-8f) {
            auto m = ImGui::GetIO().MousePos;
            Ray ray = camera.rayThrough((m.x - rect.x) / rect.width,
                                        (m.y - rect.y) / rect.height,
                                        rect.width / rect.height);
            Vec3 old = worldPoint(river->points[i]);
            if (std::abs(ray.direction.y) > .0001f) {
                float distance = (old.y - ray.origin.y) / ray.direction.y;
                if (distance > 0) {
                    Quat inv{-t->rotation.x, -t->rotation.y, -t->rotation.z, t->rotation.w};
                    auto local =
                        rotateVector(inv, ray.origin + ray.direction * distance - t->position);
                    local = {local.x / t->scale.x, local.y / t->scale.y, local.z / t->scale.z};
                    if (Environment::finite(local) && std::abs(local.x) <= 100000 &&
                        std::abs(local.y) <= 100000 && std::abs(local.z) <= 100000) {
                        river->points[i] = local;
                    }
                }
            }
        }
        if (ImGui::IsItemDeactivatedAfterEdit() || (!active && ImGui::IsMouseReleased(0))) {
            history.commit(world, selected);
        }
        draw->AddCircleFilled(
            p, 6, active ? IM_COL32(255, 200, 65, 255) : IM_COL32(55, 210, 245, 255));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("River point %d: drag XZ, edit height in Inspector", i + 1);
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    ImGui::PopID();
    ImGui::PopID();
    ImGui::SetCursorScreenPos(cursor);
    ImGui::Dummy({0, 0});
    draw->PopClipRect();
    return interacting;
}
