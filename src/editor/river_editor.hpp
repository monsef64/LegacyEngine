#pragma once
#include "editor/editor_history.hpp"
#include "editor/editor_layout.hpp"
#include "scene/camera.hpp"
#include <imgui.h>

// Reserve point/toolbar right clicks before SDL starts Scene freecam capture.
inline bool riverCurveOwnsPointer(World& world,
                                  Entity selected,
                                  const OrbitCamera& camera,
                                  const EditorRect& rect,
                                  float x,
                                  float y) {
    const auto* river = world.tryGet<River>(selected);
    const auto* t = world.tryGet<Transform>(selected);
    if (!river || !t || rect.width <= 0 || rect.height <= 0 || !rect.contains(x, y)) {
        return false;
    }
    if (x < rect.x + 360 && y < rect.y + ImGui::GetFrameHeight() + 16) {
        return true;
    }
    if (!river->editCurve) {
        return false;
    }
    const auto vp = camera.viewProjection(rect.width / rect.height);
    for (auto point : river->points) {
        auto p = t->position + rotateVector(t->rotation, point * t->scale);
        float w = vp.m[3] * p.x + vp.m[7] * p.y + vp.m[11] * p.z + vp.m[15];
        if (w <= .001f) {
            continue;
        }
        float sx = rect.x + ((vp.m[0] * p.x + vp.m[4] * p.y + vp.m[8] * p.z + vp.m[12]) / w + 1) *
                                .5f * rect.width,
              sy = rect.y + (1 - (vp.m[1] * p.x + vp.m[5] * p.y + vp.m[9] * p.z + vp.m[13]) / w) *
                                .5f * rect.height;
        if (std::abs(sx - x) <= 10 && std::abs(sy - y) <= 10) {
            return true;
        }
    }
    return false;
}

// Scene controls operate on the same local-space points as the Inspector.
inline bool drawRiverCurve(World& world,
                           Entity selected,
                           OrbitCamera& camera,
                           const EditorRect& rect,
                           EditorHistory& history,
                           bool enabled) {
    auto* river = world.tryGet<River>(selected);
    const auto* t = world.tryGet<Transform>(selected);
    if (!river || !t || !Environment::valid(*river) || rect.width < 1 || rect.height < 1) {
        return false;
    }
    static Entity owner;
    static int selectedPoint = -1;
    if (owner != selected) {
        owner = selected;
        selectedPoint = -1;
    }
    if (selectedPoint >= int(river->points.size())) {
        selectedPoint = -1;
    }
    auto worldPoint = [&](Vec3 p) { return t->position + rotateVector(t->rotation, p * t->scale); };
    auto localPoint = [&](Vec3 p, Vec3& out) {
        if (std::abs(t->scale.x * t->scale.y * t->scale.z) < 1e-8f) {
            return false;
        }
        Quat inverse{-t->rotation.x, -t->rotation.y, -t->rotation.z, t->rotation.w};
        out = rotateVector(inverse, p - t->position);
        out = {out.x / t->scale.x, out.y / t->scale.y, out.z / t->scale.z};
        return Environment::finite(out) && std::abs(out.x) <= 100000 && std::abs(out.y) <= 100000 &&
               std::abs(out.z) <= 100000;
    };
    auto append = [&](Vec3 point) {
        if (river->points.size() >= 64 || !Environment::finite(point) ||
            std::abs(point.x) > 100000 || std::abs(point.y) > 100000 ||
            std::abs(point.z) > 100000) {
            return;
        }
        history.commit(world, selected);
        river->points.push_back(point);
        selectedPoint = int(river->points.size()) - 1;
        history.commit(world, selected);
    };
    const Mat4 vp = camera.viewProjection(rect.width / rect.height);
    auto* draw = ImGui::GetWindowDrawList();
    auto screen = [&](Vec3 local, ImVec2& out) {
        Vec3 p = worldPoint(local);
        float w = vp.m[3] * p.x + vp.m[7] * p.y + vp.m[11] * p.z + vp.m[15];
        if (w <= .001f) {
            return false;
        }
        out = {rect.x + ((vp.m[0] * p.x + vp.m[4] * p.y + vp.m[8] * p.z + vp.m[12]) / w + 1) * .5f *
                            rect.width,
               rect.y + (1 - (vp.m[1] * p.x + vp.m[5] * p.y + vp.m[9] * p.z + vp.m[13]) / w) * .5f *
                            rect.height};
        return true;
    };
    auto cursor = ImGui::GetCursorScreenPos();
    bool interacting = false;
    ImGui::PushID("RiverCurve");
    ImGui::PushID(int(selected.index));
    draw->PushClipRect({rect.x, rect.y}, {rect.x + rect.width, rect.y + rect.height}, true);
    draw->AddRectFilled(
        {rect.x + 4, rect.y + 4},
        {rect.x + std::min(360.f, rect.width - 4), rect.y + ImGui::GetFrameHeight() + 12},
        IM_COL32(15, 20, 24, 220),
        4);
    ImGui::SetCursorScreenPos({rect.x + 8, rect.y + 8});
    ImGui::BeginDisabled(!enabled);
    ImGui::Checkbox("Edit river curve", &river->editCurve);
    interacting |= ImGui::IsItemHovered() || ImGui::IsItemActive();
    ImGui::SameLine();
    ImGui::BeginDisabled(!river->editCurve || river->points.size() >= 64);
    if (ImGui::Button("+ Point")) {
        const auto n = river->points.size();
        append(river->points.back() + (river->points[n - 1] - river->points[n - 2]));
    }
    interacting |= ImGui::IsItemHovered() || ImGui::IsItemActive();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!river->editCurve || river->points.size() <= 2);
    if (ImGui::Button("- Point")) {
        int index = selectedPoint < 0 ? int(river->points.size()) - 1 : selectedPoint;
        history.commit(world, selected);
        river->points.erase(river->points.begin() + index);
        selectedPoint = std::min(index, int(river->points.size()) - 1);
        history.commit(world, selected);
    }
    interacting |= ImGui::IsItemHovered() || ImGui::IsItemActive();
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    if (river->editCurve) {
        ImVec2 previous{};
        bool last = false;
        for (int i = 0; i <= 128; ++i) {
            ImVec2 p{};
            bool ok = screen(Environment::curve(*river, float(i) / 128), p);
            if (ok && last) {
                draw->AddLine(previous, p, IM_COL32(55, 210, 245, 255), 2);
            }
            previous = p;
            last = ok;
        }
        int erase = -1, insert = -1;
        for (int i = 0; i < int(river->points.size()); ++i) {
            ImVec2 p;
            if (!screen(river->points[i], p) || p.x < rect.x || p.y < rect.y ||
                p.x > rect.x + rect.width || p.y > rect.y + rect.height) {
                continue;
            }
            // The on-viewport toolbar owns its rectangle, not point handles underneath it.
            if (p.y < rect.y + ImGui::GetFrameHeight() + 16 && p.x < rect.x + 360) {
                continue;
            }
            ImGui::PushID(i);
            ImGui::SetCursorScreenPos({p.x - 9, p.y - 9});
            ImGui::BeginDisabled(!enabled);
            ImGui::InvisibleButton("Point", {18, 18});
            bool active = ImGui::IsItemActive();
            interacting |= active || ImGui::IsItemHovered();
            if (ImGui::IsItemActivated()) {
                selectedPoint = i;
                history.commit(world, selected);
            }
            if (active && ImGui::IsMouseDragging(0)) {
                Vec3 old = worldPoint(river->points[i]), worldPosition = old, local;
                bool moved = false;
                if (ImGui::GetIO().KeyCtrl) {
                    float units = std::max(.0001f,
                                           camera.distance * 2 *
                                               std::tan(camera.fieldOfView * .5f) / rect.height);
                    worldPosition.y -= ImGui::GetIO().MouseDelta.y * units;
                    moved = true;
                } else {
                    auto m = ImGui::GetIO().MousePos;
                    auto ray = camera.rayThrough((m.x - rect.x) / rect.width,
                                                 (m.y - rect.y) / rect.height,
                                                 rect.width / rect.height);
                    if (std::abs(ray.direction.y) > .0001f) {
                        float distance = (old.y - ray.origin.y) / ray.direction.y;
                        if (distance > 0) {
                            worldPosition = ray.origin + ray.direction * distance;
                            moved = true;
                        }
                    }
                }
                if (moved && localPoint(worldPosition, local)) {
                    river->points[i] = local;
                }
            }
            if (ImGui::IsItemDeactivated()) {
                history.commit(world, selected);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Point %d: drag XZ, Ctrl-drag height. Right-click to insert/delete. Shift-click empty Scene to append.",
                    i + 1);
            }
            if (ImGui::BeginPopupContextItem("Point actions")) {
                selectedPoint = i;
                interacting = true;
                if (ImGui::MenuItem(
                        "Insert point after", nullptr, false, river->points.size() < 64)) {
                    insert = i;
                }
                if (ImGui::MenuItem("Delete point", nullptr, false, river->points.size() > 2)) {
                    erase = i;
                }
                ImGui::EndPopup();
            }
            draw->AddCircleFilled(p,
                                  selectedPoint == i ? 8.f : 6.f,
                                  selectedPoint == i ? IM_COL32(255, 200, 65, 255)
                                                     : IM_COL32(55, 210, 245, 255));
            char number[16];
            std::snprintf(number, sizeof(number), "%d", i + 1);
            draw->AddText({p.x + 10, p.y - 8}, IM_COL32(245, 245, 245, 255), number);
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        if (erase >= 0) {
            history.commit(world, selected);
            river->points.erase(river->points.begin() + erase);
            selectedPoint = -1;
            history.commit(world, selected);
        }
        if (insert >= 0) {
            auto point = insert + 1 < int(river->points.size())
                             ? (river->points[insert] + river->points[insert + 1]) * .5f
                             : river->points.back() +
                                   (river->points.back() - river->points[river->points.size() - 2]);
            if (!Environment::finite(point) || std::abs(point.x) > 100000 ||
                std::abs(point.y) > 100000 || std::abs(point.z) > 100000) {
                point = river->points.back();
            }
            history.commit(world, selected);
            river->points.insert(river->points.begin() + insert + 1, point);
            selectedPoint = insert + 1;
            history.commit(world, selected);
        }
        auto mouse = ImGui::GetIO().MousePos;
        bool inside = mouse.x >= rect.x && mouse.y >= rect.y && mouse.x < rect.x + rect.width &&
                      mouse.y < rect.y + rect.height;
        if (enabled && !interacting && inside && ImGui::IsWindowHovered() &&
            ImGui::GetIO().KeyShift && ImGui::IsMouseClicked(0) &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
            auto ray = camera.rayThrough((mouse.x - rect.x) / rect.width,
                                         (mouse.y - rect.y) / rect.height,
                                         rect.width / rect.height);
            if (std::abs(ray.direction.y) > .0001f) {
                float distance =
                    (worldPoint(river->points.back()).y - ray.origin.y) / ray.direction.y;
                Vec3 local;
                if (distance > 0 && localPoint(ray.origin + ray.direction * distance, local)) {
                    append(local);
                    interacting = true;
                }
            }
        }
    }
    draw->PopClipRect();
    ImGui::PopID();
    ImGui::PopID();
    ImGui::SetCursorScreenPos(cursor);
    ImGui::Dummy({0, 0});
    return interacting;
}
