#include "editor/transform_editor.hpp"

namespace {
// Vector-drawn icons: no font-dependent Unicode symbols or external icon files.
void toolButton(const char* id, const char* tooltip, TransformTool value, TransformTool& current) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::Button(id, ImVec2(40, 40))) {
        current = value;
    }
    auto* draw = ImGui::GetWindowDrawList();
    const ImU32 color = IM_COL32(235, 240, 245, 255);
    auto point = [&](float x, float y) { return ImVec2(p.x + x, p.y + y); };
    auto line = [&](float x, float y, float a, float b) {
        draw->AddLine(point(x, y), point(a, b), color, 2);
    };
    if (value == TransformTool::Move) {
        line(9, 20, 31, 20);
        line(20, 9, 20, 31);
        line(9, 20, 13, 16);
        line(9, 20, 13, 24);
        line(31, 20, 27, 16);
        line(31, 20, 27, 24);
        line(20, 9, 16, 13);
        line(20, 9, 24, 13);
        line(20, 31, 16, 27);
        line(20, 31, 24, 27);
    } else if (value == TransformTool::Rotate) {
        draw->PathArcTo(point(20, 20), 11, -kPi * 0.8f, kPi * 0.65f, 24);
        draw->PathStroke(color, 0, 2);
        line(13, 29, 19, 29);
        line(13, 29, 13, 23);
    } else {
        draw->AddRect(point(10, 22), point(18, 30), color, 0, 0, 2);
        line(19, 21, 30, 10);
        line(30, 10, 22, 10);
        line(30, 10, 30, 18);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", tooltip);
    }
}
} // namespace

void TransformEditor::draw(World& world,
                           Entity entity,
                           bool enabled,
                           const OrbitCamera& camera,
                           float aspect,
                           const EditorRect* scene,
                           ImDrawList* drawList) {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        scene ? ImVec2(scene->x + 8, scene->y + scene->height - 8)
              : ImVec2(viewport->WorkPos.x + 16, viewport->WorkPos.y + viewport->WorkSize.y - 16),
        ImGuiCond_Always,
        ImVec2(0, 1));
    ImGui::Begin("Transform tools",
                 nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);
    const char* ids[]{"##move", "##rotate", "##scale"};
    const char* tips[]{
        "Move: drag an axis arrow", "Rotate: drag an axis ring", "Scale: drag an axis handle"};
    for (int i = 0; i < 3; ++i) {
        if (i) {
            ImGui::SameLine();
        }
        const auto tool = static_cast<TransformTool>(i);
        const bool active = tool_ == tool;
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.42f, 0.64f, 1));
        }
        toolButton(ids[i], tips[i], tool, tool_);
        if (active) {
            ImGui::PopStyleColor();
        }
    }
    ImGui::End();
    if (!enabled || !world.alive(entity) || !world.has<Transform>(entity)) {
        finish(world);
        return;
    }
    if (heldBody_ && heldEntity_ != entity) {
        finish(world);
    }
    Transform& transform = world.get<Transform>(entity);
    Mat4 model = transformMatrix(transform.position, transform.rotation, transform.scale);
    const Mat4 view = lookAt(camera.position(), camera.target, {0, 1, 0});
    const Mat4 projection = perspective(camera.fieldOfView, aspect, 0.1f, 200);
    const ImGuizmo::OPERATION operations[]{ImGuizmo::TRANSLATE, ImGuizmo::ROTATE, ImGuizmo::SCALE};
    ImGuizmo::Enable(true);
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(drawList ? drawList : ImGui::GetBackgroundDrawList());
    ImGuizmo::SetRect(scene ? scene->x : viewport->Pos.x,
                      scene ? scene->y : viewport->Pos.y,
                      scene ? scene->width : viewport->Size.x,
                      scene ? scene->height : viewport->Size.y);
    const bool wasDragging = dragging_;
    const bool changed =
        ImGuizmo::Manipulate(view.m,
                             projection.m,
                             operations[static_cast<int>(tool_)],
                             tool_ == TransformTool::Move ? ImGuizmo::WORLD : ImGuizmo::LOCAL,
                             model.m);
    hovered_ = ImGuizmo::IsOver(operations[static_cast<int>(tool_)]);
    dragging_ = ImGuizmo::IsUsing();
    if (dragging_ && !wasDragging) {
        scaleAtStart_ = transform.scale;
    }
    if (dragging_ && !heldBody_ && world.has<RigidBody>(entity)) {
        savedBody_ = world.get<RigidBody>(entity);
        savedBody_.linearVelocity = savedBody_.angularVelocity = {};
        heldEntity_ = entity;
        heldBody_ = true;
        world.remove<RigidBody>(entity);
    }
    if (changed) {
        const auto* collider = world.tryGet<Collider>(entity);
        const bool round = collider && collider->shape != ColliderShape::Box;
        if (round && tool_ == TransformTool::Scale) {
            // ImGuizmo returns absolute scale relative to the start of its drag.
            // Compare against that baseline, not last frame's uniform result.
            Transform candidate = transform;
            candidate.scale = scaleAtStart_;
            if (applyGizmoMatrix(candidate, model, tool_, true)) {
                transform.scale = candidate.scale;
            }
        } else {
            applyGizmoMatrix(transform, model, tool_, false);
        }
    }
    if (!dragging_ && heldBody_) {
        // Keep this frame's hover state when restoring the physics body.
        const bool hovered = hovered_;
        finish(world);
        hovered_ = hovered;
    }
}
