#pragma once
#include "editor/editor_layout.hpp"
#include "scene/camera.hpp"
#include <algorithm>
#include "ui/mygui_ui.hpp"
#include "ui/mygui_ui.hpp"

inline EditorRect orientationRect(const EditorRect& scene) {
    const float size = std::min(126.0f, std::min(scene.width, scene.height));
    return {scene.x + scene.width - size, scene.y, size, size};
}
// Scene-only navigation, with six axis endpoints sorted back-to-front.
inline bool drawSceneOrientation(OrbitCamera& camera, const EditorRect& scene, bool enabled) {
    if (scene.width < 100 || scene.height < 100) {
        return false;
    }
    const auto rect = orientationRect(scene);
    const auto old = ui::cursorScreenPos();
    ui::setCursorScreenPos({rect.x, rect.y});
    ui::beginDisabled(!enabled);
    ui::invisibleButton("##SceneOrientation", {rect.width, rect.height});
    const bool hovered = ui::isItemHovered(), active = ui::isItemActive();
    auto* draw = ui::windowDrawList();
    const ui::Vec2 centre(rect.x + rect.width * .5f, rect.y + rect.height * .45f);
    const Vec3 right = camera.right(), up = camera.up(),
               eye = normalize(camera.position() - camera.target);
    struct Endpoint {
        int axis, sign;
        float depth;
        ui::Vec2 screen;
    };
    Endpoint ends[6];
    int n = 0;
    for (int axis = 0; axis < 3; ++axis) {
        for (int sign : {-1, 1}) {
            Vec3 v{};
            if (axis == 0) {
                v.x = float(sign);
            }
            if (axis == 1) {
                v.y = float(sign);
            }
            if (axis == 2) {
                v.z = float(sign);
            }
            ends[n++] = {axis,
                         sign,
                         dot(v, eye),
                         {centre.x + dot(v, right) * 36, centre.y - dot(v, up) * 36}};
        }
    }
    std::stable_sort(
        ends, ends + 6, [](const auto& a, const auto& b) { return a.depth < b.depth; });
    const uint32_t colors[]{
        UI_COL32(222, 87, 77, 255), UI_COL32(145, 205, 82, 255), UI_COL32(85, 146, 222, 255)};
    int hit = -1;
    float closest = 144;
    for (int i = 0; i < 6; ++i) {
        const auto& e = ends[i];
        const auto mouse = ui::io().MousePos;
        const float dx = mouse.x - e.screen.x, dy = mouse.y - e.screen.y, d = dx * dx + dy * dy;
        if (d <= closest) {
            closest = d;
            hit = i;
        }
        const auto color = e.sign > 0 ? colors[e.axis] : UI_COL32(190, 190, 190, 255);
        draw->addLine(centre, e.screen, color, 3);
        draw->addCircleFilled(e.screen, 7, color);
        if (e.sign > 0) {
            const char* label = e.axis == 0 ? "X" : e.axis == 1 ? "Y" : "Z";
            draw->addText({e.screen.x + 7, e.screen.y - 10}, UI_COL32(240, 240, 240, 255), label);
        }
    }
    draw->addRectFilled({centre.x - 5, centre.y - 5},
                        {centre.x + 5, centre.y + 5},
                        UI_COL32(215, 215, 215, 255),
                        1);
    draw->addText({rect.x + 35, rect.y + rect.height - 25}, UI_COL32(220, 220, 220, 255), "Persp");
    if (enabled && hovered) {
        ui::setTooltip("Drag to orbit; click X/Y/Z endpoints for aligned views");
        if (ui::isMouseReleased(ui::MouseButton_Left) &&
            !ui::isMouseDragPastThreshold(ui::MouseButton_Left) && hit >= 0) {
            camera.snapAxis(ends[hit].axis, ends[hit].sign);
        }
    }
    if (enabled && active && ui::isMouseDragging(ui::MouseButton_Left)) {
        const auto delta = ui::io().MouseDelta;
        camera.yaw -= delta.x * .01f;
        camera.pitch = std::clamp(camera.pitch + delta.y * .01f, radians(-89.0f), radians(89.0f));
    }
    ui::endDisabled();
    ui::setCursorScreenPos(old);
    // Restoring the cursor after the full-size Scene image can place it just
    // outside the content region. Consume the restored cursor position with a
    // zero-sized item so it does not create an untracked layout extension.
    ui::dummy(ui::Vec2(0, 0));
    return hovered || active;
}
