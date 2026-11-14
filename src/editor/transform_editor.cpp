#include "editor/transform_editor.hpp"
#include "core/hierarchy.hpp"
#include <limits>

namespace {
// Vector-drawn icons: no font-dependent Unicode symbols or external icon files.
void toolButton(const char* id, const char* tooltip, TransformTool value, TransformTool& current) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (ImGui::Button(id, ImVec2(26, 24))) {
        current = value;
    }
    auto* draw = ImGui::GetWindowDrawList();
    const ImU32 color = IM_COL32(235, 240, 245, 255);
    auto point = [&](float x, float y) { return ImVec2(p.x + x * .6f + 1, p.y + y * .55f + 1); };
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
        draw->PathArcTo(point(20, 20), 6, -kPi * 0.8f, kPi * 0.65f, 24);
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
    if (!enabled || !world.alive(entity) || !world.has<Transform>(entity)) {
        finish(world);
        return;
    }
    if ((heldBody_ || face_ >= 0) && heldEntity_ != entity) {
        finish(world);
    }
    if (scene && drawColliderFaces(world,
                                   entity,
                                   camera,
                                   aspect,
                                   *scene,
                                   drawList ? drawList : ImGui::GetBackgroundDrawList())) {
        ImGuizmo::Enable(false);
        return;
    }
    Transform& transform = world.get<Transform>(entity);
    Mat4 model = transformMatrix(transform.position, transform.rotation, transform.scale);
    const Mat4 view = lookAt(camera.position(), camera.target, {0, 1, 0});
    const Mat4 projection = perspective(camera.fieldOfView, aspect, 0.1f, 200);
    const ImGuizmo::OPERATION operations[]{ImGuizmo::TRANSLATE, ImGuizmo::ROTATE, ImGuizmo::SCALE};
    ImGuizmo::Enable(true);
    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist(drawList ? drawList : ImGui::GetBackgroundDrawList());
    const auto* viewport = ImGui::GetMainViewport();
    ImGuizmo::SetRect(scene ? scene->x : viewport->Pos.x,
                      scene ? scene->y : viewport->Pos.y,
                      scene ? scene->width : viewport->Size.x,
                      scene ? scene->height : viewport->Size.y);
    const bool wasDragging = dragging_;
    const bool changed = ImGuizmo::Manipulate(view.m,
                                              projection.m,
                                              operations[static_cast<int>(tool_)],
                                              localSpace_ ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
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

void TransformEditor::drawToolbar() {
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
    ImGui::SameLine();
    ImGui::BeginDisabled(dragging_);
    if (ImGui::RadioButton("World", !localSpace_)) {
        localSpace_ = false;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Local", localSpace_)) {
        localSpace_ = true;
    }
    ImGui::EndDisabled();
}

// Face gestures edit the collider, never the object's Transform. Ray/axis closest
// points give perspective-correct motion, including rotated and mirrored objects.
bool TransformEditor::drawColliderFaces(World& world,
                                        Entity entity,
                                        const OrbitCamera& camera,
                                        float aspect,
                                        const EditorRect& scene,
                                        ImDrawList* draw) {
    auto* collider = world.tryGet<Collider>(entity);
    if (!collider || collider->shape != ColliderShape::Box || scene.width <= 0 ||
        scene.height <= 0) {
        if (face_ >= 0) {
            finish(world);
        }
        return false;
    }
    auto component = [](Vec3& v, int axis) -> float& {
        return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
    };
    const auto pose = world.get<Transform>(entity);
    const auto mouse = ImGui::GetIO().MousePos;
    auto parameter = [&](Vec3 origin, Vec3 axis, float& value) {
        const auto ray = camera.rayThrough(
            (mouse.x - scene.x) / scene.width, (mouse.y - scene.y) / scene.height, aspect);
        const auto delta = origin - ray.origin;
        const float c = dot(axis, ray.direction), denominator = 1 - c * c;
        if (denominator < .0001f) {
            return false;
        }
        value = (c * dot(ray.direction, delta) - dot(axis, delta)) / denominator;
        return std::isfinite(value);
    };
    if (face_ >= 0) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            finish(world);
            return true;
        }
        float current;
        if (parameter(faceOrigin_, faceAxis_, current)) {
            const int axis = face_ / 2;
            const float sign = face_ % 2 ? 1.f : -1.f;
            const float scale = component(faceTransform_.scale, axis);
            const float delta = (current - faceParameter_) / scale;
            const float size = std::max(.001f, component(faceStart_.size, axis) + sign * delta);
            *collider = faceStart_;
            component(collider->size, axis) = size;
            component(collider->center, axis) +=
                sign * (size - component(faceStart_.size, axis)) * .5f;
        }
    }
    const auto vp = camera.viewProjection(aspect);
    ImVec2 points[6];
    bool visible[6]{};
    Vec3 worldPoints[6];
    int hovered = -1;
    float nearest = 64, depth = std::numeric_limits<float>::max();
    draw->PushClipRect(
        ImVec2(scene.x, scene.y), ImVec2(scene.x + scene.width, scene.y + scene.height), true);
    for (int face = 0; face < 6; ++face) {
        const int axis = face / 2;
        auto local = collider->center;
        component(local, axis) +=
            (face % 2 ? 1.f : -1.f) * std::abs(component(collider->size, axis)) * .5f;
        const auto p = pose.position + rotateVector(pose.rotation, local * pose.scale);
        worldPoints[face] = p;
        const auto& m = vp.m;
        const float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
        if (w <= .001f) {
            continue;
        }
        const float x = (m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12]) / w,
                    y = (m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13]) / w;
        const float z = (m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]) / w;
        if (z < -1 || z > 1) {
            continue;
        }
        points[face] =
            ImVec2(scene.x + (x + 1) * .5f * scene.width, scene.y + (1 - y) * .5f * scene.height);
        visible[face] = true;
        const float dx = mouse.x - points[face].x, dy = mouse.y - points[face].y,
                    d = dx * dx + dy * dy;
        if (d <= nearest && (d < nearest - .01f || w < depth) && scene.contains(mouse.x, mouse.y) &&
            ImGui::IsWindowHovered()) {
            nearest = d;
            depth = w;
            hovered = face;
        }
    }
    for (int face = 0; face < 6; ++face) {
        if (visible[face]) {
            const bool active = face == face_ || (face_ < 0 && face == hovered);
            draw->AddCircleFilled(points[face],
                                  active ? 6.f : 4.f,
                                  active ? IM_COL32(255, 210, 70, 255)
                                         : IM_COL32(80, 245, 130, 255));
            draw->AddCircle(points[face], active ? 6.f : 4.f, IM_COL32(10, 30, 15, 255), 0, 1.5f);
        }
    }
    draw->PopClipRect();
    if (face_ < 0 && !dragging_ && hovered >= 0) {
        hovered_ = true;
        ImGui::SetTooltip("Drag to resize this collider face (opposite face stays fixed)");
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            faceTransform_ = pose;
            const int axis = hovered / 2;
            if (std::abs(component(faceTransform_.scale, axis)) >= 1e-8f) {
                Vec3 unit{};
                component(unit, axis) = 1;
                faceAxis_ = rotateVector(pose.rotation, unit);
                faceOrigin_ = worldPoints[hovered];
                if (parameter(faceOrigin_, faceAxis_, faceParameter_)) {
                    face_ = hovered;
                    faceStart_ = *collider;
                    heldEntity_ = entity;
                    dragging_ = true;
                    if (world.has<RigidBody>(entity)) {
                        savedBody_ = world.get<RigidBody>(entity);
                        savedBody_.linearVelocity = savedBody_.angularVelocity = {};
                        heldBody_ = true;
                        world.remove<RigidBody>(entity);
                    }
                }
            }
        }
        return true;
    }
    return face_ >= 0;
}
