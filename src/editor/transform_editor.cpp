#include "editor/transform_editor.hpp"
#include "core/hierarchy.hpp"
#include "models/model_asset.hpp"
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
                           ImDrawList* drawList,
                           const AssetManager* assets,
                           bool navigationOnly) {
    if (!enabled || !world.alive(entity) || !world.has<Transform>(entity)) {
        finish(world);
        return;
    }
    if ((heldBody_ || face_ >= 0) && heldEntity_ != entity) {
        finish(world);
    }
    if (!navigationOnly && scene &&
        drawColliderFaces(world,
                          entity,
                          camera,
                          aspect,
                          *scene,
                          drawList ? drawList : ImGui::GetBackgroundDrawList())) {
        ImGuizmo::Enable(false);
        return;
    }
    Transform& transform = world.get<Transform>(entity);
    if (!dragging_) {
        pivotLocal_ = {};
        if (centerPivot_ && !navigationOnly) {
            const auto center = selectionCenter(world, entity, assets);
            auto local = rotateVector(Quat{-transform.rotation.x,
                                           -transform.rotation.y,
                                           -transform.rotation.z,
                                           transform.rotation.w},
                                      center - transform.position);
            pivotLocal_ = {std::abs(transform.scale.x) > 1e-8f ? local.x / transform.scale.x : 0,
                           std::abs(transform.scale.y) > 1e-8f ? local.y / transform.scale.y : 0,
                           std::abs(transform.scale.z) > 1e-8f ? local.z / transform.scale.z : 0};
        }
    }
    const auto pivot =
        transform.position + rotateVector(transform.rotation, pivotLocal_ * transform.scale);
    Mat4 model = transformMatrix(pivot, transform.rotation, transform.scale);
    if (!dragging_) {
        gizmoAnchor_ = pivot;
    }
    const auto gizmoCamera = gizmoCameraMatrices(camera, aspect, gizmoAnchor_);
    const auto& view = gizmoCamera.view;
    const auto& projection = gizmoCamera.projection;
    const ImGuizmo::OPERATION operations[]{ImGuizmo::TRANSLATE, ImGuizmo::ROTATE, ImGuizmo::SCALE};
    ImGuizmo::Enable(!navigationOnly);
    ImGuizmo::SetOrthographic(camera.orthographic);
    ImGuizmo::SetDrawlist(drawList ? drawList : ImGui::GetBackgroundDrawList());
    const auto* viewport = ImGui::GetMainViewport();
    ImGuizmo::SetRect(scene ? scene->x : viewport->Pos.x,
                      scene ? scene->y : viewport->Pos.y,
                      scene ? scene->width : viewport->Size.x,
                      scene ? scene->height : viewport->Size.y);
    const bool wasDragging = dragging_;
    const float step = tool_ == TransformTool::Rotate ? rotationSnap_ : snap_;
    const float snapValues[3]{step, step, step};
    const bool changed = ImGuizmo::Manipulate(view.m,
                                              projection.m,
                                              operations[static_cast<int>(tool_)],
                                              localSpace_ ? ImGuizmo::LOCAL : ImGuizmo::WORLD,
                                              model.m,
                                              nullptr,
                                              ImGui::GetIO().KeyCtrl ? snapValues : nullptr);
    hovered_ = !navigationOnly && ImGuizmo::IsOver(operations[static_cast<int>(tool_)]);
    dragging_ = !navigationOnly && ImGuizmo::IsUsing();
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
    if (changed && !navigationOnly) {
        const auto* collider = world.tryGet<Collider>(entity);
        const bool round = collider && (collider->shape == ColliderShape::Capsule ||
                                        collider->shape == ColliderShape::Cone);
        Transform candidate = transform;
        candidate.position = pivot;
        if (round && tool_ == TransformTool::Scale) {
            candidate.scale = scaleAtStart_;
        }
        if (applyGizmoMatrix(candidate, model, tool_, round && tool_ == TransformTool::Scale)) {
            candidate.position = candidate.position -
                                 rotateVector(candidate.rotation, pivotLocal_ * candidate.scale);
            transform = candidate;
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
    const bool compact = ImGui::GetContentRegionAvail().x < 900;
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
    if (compact) {
        ImGui::NewLine();
    } else {
        ImGui::SameLine();
    }
    ImGui::SetNextItemWidth(85);
    int pivot = centerPivot_ ? 1 : 0;
    if (ImGui::Combo("##Pivot", &pivot, "Origin\0Center\0")) {
        centerPivot_ = pivot == 1;
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Origin: object position. Center: combined mesh bounds of this object and its children.");
    }
    ImGui::SameLine();
    const float width = ImGui::GetContentRegionAvail().x;
    if (width > 390) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + width - 390);
    }
    ImGui::AlignTextToFramePadding();
    const bool rotation = tool_ == TransformTool::Rotate;
    ImGui::TextUnformatted(rotation ? "Snap (deg)" : "Snap");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130);
    float& step = rotation ? rotationSnap_ : snap_;
    if (ImGui::InputFloat("##Snap", &step, rotation ? 5.f : .25f, rotation ? 15.f : 1.f, "%.2f")) {
        step = std::isfinite(step)
                   ? std::clamp(step, rotation ? 1.f : .25f, rotation ? 180.f : 10000.f)
                   : (rotation ? 15.f : .5f);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Hold Ctrl while dragging: move in world units, rotate in degrees, scale in increments. Rotation has its own degree step (default 15).");
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

Vec3 TransformEditor::selectionCenter(World& world, Entity selected, const AssetManager* assets) {
    if (assets && (boundsRoot_ != assets->root || boundsRevision_ != modelAssetRevision)) {
        modelBounds_.clear();
        boundsRoot_ = assets->root;
        boundsRevision_ = modelAssetRevision;
    }
    bool haveMeshes = false;
    Vec3 low{}, high{};
    auto include = [](Vec3 p, Vec3& a, Vec3& b, bool& any) {
        if (!any) {
            a = b = p;
            any = true;
        }
        a = {std::min(a.x, p.x), std::min(a.y, p.y), std::min(a.z, p.z)};
        b = {std::max(b.x, p.x), std::max(b.y, p.y), std::max(b.z, p.z)};
    };
    for (auto entity : world.entities()) {
        auto ancestor = entity;
        unsigned depth = 0;
        while (ancestor != selected && ancestor != Entity{} && depth++ < 128) {
            ancestor = getParent(world, ancestor);
        }
        if (ancestor != selected || !world.has<Transform>(entity)) {
            continue;
        }
        const auto& pose = world.get<Transform>(entity);
        const auto* mesh = world.tryGet<MeshRenderer>(entity);
        if (!mesh) {
            continue;
        }
        Bounds bounds;
        bool valid = false;
        if (!mesh->model.empty() && assets) {
            auto it = modelBounds_.find(mesh->model);
            if (it == modelBounds_.end()) {
                try {
                    if (modelBounds) {
                        bounds.valid = modelBounds(*assets, mesh->model, bounds.low, bounds.high);
                        if (!bounds.valid) {
                            continue; // Retry after streamed geometry is ready.
                        }
                    } else {
                        const auto data = loadModelAsset(*assets, mesh->model);
                        for (const auto& vertex : data.vertices) {
                            include(vertex.position, bounds.low, bounds.high, bounds.valid);
                        }
                    }
                } catch (const std::exception&) {
                } // Invalid models still allow origin/child editing.
                if (modelBounds_.size() >= 4096) {
                    modelBounds_.clear();
                }
                it = modelBounds_.emplace(mesh->model, bounds).first;
            }
            bounds = it->second;
            valid = bounds.valid;
        } else if (meshBounds) {
            valid = meshBounds(mesh->mesh, bounds.low, bounds.high);
        }
        if (valid) {
            for (int i = 0; i < 8; ++i) {
                const Vec3 p{i & 1 ? bounds.high.x : bounds.low.x,
                             i & 2 ? bounds.high.y : bounds.low.y,
                             i & 4 ? bounds.high.z : bounds.low.z};
                include(pose.position + rotateVector(pose.rotation, p * pose.scale),
                        low,
                        high,
                        haveMeshes);
            }
        }
    }
    // An empty hierarchy has no mesh center. Never invent an offset pivot
    // from child origins: empties and particle emitters use their actual origin.
    return haveMeshes ? (low + high) * .5f : world.get<Transform>(selected).position;
}
