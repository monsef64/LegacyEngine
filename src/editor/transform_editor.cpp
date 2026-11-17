#include "editor/transform_editor.hpp"
#include "core/hierarchy.hpp"
#include "models/model_asset.hpp"
#include <limits>

namespace {
void toolButton(const char* id, const char* tooltip, TransformTool value, TransformTool& current) {
    const ui::Vec2 p = ui::cursorScreenPos();
    if (ui::button(id, {26, 24})) current = value;
    auto* draw = ui::windowDrawList();
    const uint32_t color = UI_COL32(235, 240, 245, 255);
    auto point = [&](float x, float y) { return ui::Vec2(p.x + x * .6f + 1, p.y + y * .55f + 1); };
    auto line = [&](float x, float y, float a, float b) { draw->addLine(point(x, y), point(a, b), color, 2); };
    if (value == TransformTool::Move) {
        line(9, 20, 31, 20); line(20, 9, 20, 31);
        line(9, 20, 13, 16); line(9, 20, 13, 24); line(31, 20, 27, 16);
        line(31, 20, 27, 24); line(20, 9, 16, 13); line(20, 9, 24, 13);
        line(20, 31, 16, 27); line(20, 31, 24, 27);
    } else if (value == TransformTool::Rotate) {
        draw->pathArcTo(point(20, 20), 6, -kPi * .8f, kPi * .65f, 24);
        draw->pathStroke(color, false, 2);
        line(13, 29, 19, 29); line(13, 29, 13, 23);
    } else {
        draw->addRect(point(10, 22), point(18, 30), color, 0, 2);
        line(19, 21, 30, 10); line(30, 10, 22, 10); line(30, 10, 30, 18);
    }
    if (ui::isItemHovered()) ui::setTooltip("%s", tooltip);
}

float component(const Vec3& value, int axis) {
    return axis == 0 ? value.x : axis == 1 ? value.y : value.z;
}
float& component(Vec3& value, int axis) {
    return axis == 0 ? value.x : axis == 1 ? value.y : value.z;
}
} // namespace

void TransformEditor::draw(World& world,
                           Entity entity,
                           bool enabled,
                           const OrbitCamera& camera,
                           float aspect,
                           const EditorRect* scene,
                           ui::DrawList* drawList,
                           const AssetManager* assets) {
    if (!enabled || !world.alive(entity) || !world.has<Transform>(entity)) {
        finish(world);
        return;
    }
    if ((heldBody_ || face_ >= 0) && heldEntity_ != entity) finish(world);
    auto* draw = drawList ? drawList : ui::backgroundDrawList();
    if (scene && drawColliderFaces(world, entity, camera, aspect, *scene, draw)) {
        ui::cancelGizmo();
        return;
    }

    Transform& transform = world.get<Transform>(entity);
    if (!dragging_) {
        pivotLocal_ = {};
        if (centerPivot_) {
            const auto center = selectionCenter(world, entity, assets);
            const Quat inverse{-transform.rotation.x, -transform.rotation.y, -transform.rotation.z,
                               transform.rotation.w};
            const auto local = rotateVector(inverse, center - transform.position);
            pivotLocal_ = {std::abs(transform.scale.x) > 1e-8f ? local.x / transform.scale.x : 0,
                           std::abs(transform.scale.y) > 1e-8f ? local.y / transform.scale.y : 0,
                           std::abs(transform.scale.z) > 1e-8f ? local.z / transform.scale.z : 0};
        }
    }
    const Vec3 pivot = transform.position + rotateVector(transform.rotation, pivotLocal_ * transform.scale);
    const auto* viewport = ui::mainViewport();
    const ui::Rect rect = scene
        ? ui::Rect{scene->x, scene->y, scene->width, scene->height}
        : ui::Rect{viewport->Pos.x, viewport->Pos.y, viewport->Size.x, viewport->Size.y};
    const auto operation = tool_ == TransformTool::Move ? ui::GizmoOperation::Translate
                           : tool_ == TransformTool::Rotate ? ui::GizmoOperation::Rotate
                                                            : ui::GizmoOperation::Scale;
    const bool wasDragging = dragging_;
    const auto result = ui::manipulateGizmo(draw,
                                            rect,
                                            camera.viewProjection(aspect),
                                            pivot,
                                            transform.rotation,
                                            transform.scale,
                                            operation,
                                            localSpace_,
                                            ui::io().KeyCtrl,
                                            snap_);
    hovered_ = result.hovered;
    dragging_ = result.usingHandle;
    if (dragging_ && !wasDragging) scaleAtStart_ = transform.scale;
    if (dragging_ && !heldBody_ && world.has<RigidBody>(entity)) {
        savedBody_ = world.get<RigidBody>(entity);
        savedBody_.linearVelocity = savedBody_.angularVelocity = {};
        heldEntity_ = entity;
        heldBody_ = true;
        world.remove<RigidBody>(entity);
    }
    if (result.changed) {
        const auto* collider = world.tryGet<Collider>(entity);
        const bool round = collider && (collider->shape == ColliderShape::Sphere ||
                                        collider->shape == ColliderShape::Capsule ||
                                        collider->shape == ColliderShape::Cone);
        Transform candidate = transform;
        candidate.position = pivot;
        if (round && tool_ == TransformTool::Scale) candidate.scale = scaleAtStart_;
        if (applyGizmoMatrix(candidate,
                             result.matrix,
                             tool_,
                             round && tool_ == TransformTool::Scale)) {
            candidate.position = candidate.position -
                                 rotateVector(candidate.rotation, pivotLocal_ * candidate.scale);
            transform = candidate;
        }
    }
    if (!dragging_ && heldBody_) {
        const bool hovered = hovered_;
        finish(world);
        hovered_ = hovered;
    }
}

void TransformEditor::drawToolbar() {
    const bool compact = ui::contentRegionAvail().x < 620;
    const char* ids[]{"##move", "##rotate", "##scale"};
    const char* tips[]{"Move: drag an axis arrow", "Rotate: drag an axis ring", "Scale: drag an axis handle"};
    for (int i = 0; i < 3; ++i) {
        if (i) ui::sameLine();
        const auto tool = static_cast<TransformTool>(i);
        const bool active = tool_ == tool;
        if (active) ui::pushStyleColor(ui::Col_Button, {.20f, .42f, .64f, 1});
        toolButton(ids[i], tips[i], tool, tool_);
        if (active) ui::popStyleColor();
    }
    ui::sameLine();
    ui::beginDisabled(dragging_);
    if (ui::radioButton("World", !localSpace_)) localSpace_ = false;
    ui::sameLine();
    if (ui::radioButton("Local", localSpace_)) localSpace_ = true;
    if (compact) ui::newLine(); else ui::sameLine();
    ui::setNextItemWidth(85);
    int pivot = centerPivot_ ? 1 : 0;
    if (ui::combo("##Pivot", &pivot, "Origin\0Center\0")) centerPivot_ = pivot == 1;
    if (ui::isItemHovered()) {
        ui::setTooltip("Origin: object position. Center: combined mesh bounds of this object and its children.");
    }
    ui::sameLine();
    const float width = ui::contentRegionAvail().x;
    if (width > 180) ui::setCursorPosX(ui::cursorPosX() + width - 180);
    ui::alignTextToFramePadding();
    ui::textUnformatted("Snap");
    ui::sameLine();
    ui::setNextItemWidth(130);
    if (ui::inputFloat("##Snap", &snap_, .25f, .25f, "%.2f")) {
        snap_ = std::isfinite(snap_) ? std::clamp(std::round(snap_ * 4) / 4, .25f, 10000.f) : .5f;
    }
    if (ui::isItemHovered()) {
        ui::setTooltip("Hold Ctrl while dragging: move in world units, rotate in degrees, and scale in increments.");
    }
    ui::endDisabled();
}

bool TransformEditor::drawColliderFaces(World& world,
                                        Entity entity,
                                        const OrbitCamera& camera,
                                        float aspect,
                                        const EditorRect& scene,
                                        ui::DrawList* draw) {
    auto* collider = world.tryGet<Collider>(entity);
    if (!collider || collider->shape != ColliderShape::Box || scene.width <= 0 || scene.height <= 0) {
        if (face_ >= 0) finish(world);
        return false;
    }
    const auto pose = world.get<Transform>(entity);
    const auto mouse = ui::io().MousePos;
    auto parameter = [&](Vec3 origin, Vec3 axis, float& value) {
        const auto ray = camera.rayThrough((mouse.x - scene.x) / scene.width,
                                           (mouse.y - scene.y) / scene.height,
                                           aspect);
        const auto delta = origin - ray.origin;
        const float c = dot(axis, ray.direction), denominator = 1 - c * c;
        if (denominator < .0001f) return false;
        value = (c * dot(ray.direction, delta) - dot(axis, delta)) / denominator;
        return std::isfinite(value);
    };
    if (face_ >= 0) {
        if (!ui::isMouseDown(ui::MouseButton_Left)) {
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
            component(collider->center, axis) += sign * (size - component(faceStart_.size, axis)) * .5f;
        }
    }
    const auto vp = camera.viewProjection(aspect);
    ui::Vec2 points[6];
    bool visible[6]{};
    Vec3 worldPoints[6];
    int hovered = -1;
    float nearest = 64, depth = std::numeric_limits<float>::max();
    draw->pushClipRect({scene.x, scene.y}, {scene.x + scene.width, scene.y + scene.height}, true);
    for (int face = 0; face < 6; ++face) {
        const int axis = face / 2;
        auto local = collider->center;
        component(local, axis) += (face % 2 ? 1.f : -1.f) * std::abs(component(collider->size, axis)) * .5f;
        const auto p = pose.position + rotateVector(pose.rotation, local * pose.scale);
        worldPoints[face] = p;
        const auto& m = vp.m;
        const float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
        if (w <= .001f) continue;
        const float x = (m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12]) / w;
        const float y = (m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13]) / w;
        const float z = (m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]) / w;
        if (z < -1 || z > 1) continue;
        points[face] = {scene.x + (x + 1) * .5f * scene.width, scene.y + (1 - y) * .5f * scene.height};
        visible[face] = true;
        const float dx = mouse.x - points[face].x, dy = mouse.y - points[face].y;
        const float d = dx * dx + dy * dy;
        if (d <= nearest && (d < nearest - .01f || w < depth) && scene.contains(mouse.x, mouse.y) &&
            ui::isWindowHovered()) {
            nearest = d;
            depth = w;
            hovered = face;
        }
    }
    for (int face = 0; face < 6; ++face) {
        if (!visible[face]) continue;
        const bool active = face == face_ || (face_ < 0 && face == hovered);
        draw->addCircleFilled(points[face], active ? 6.f : 4.f,
                              active ? UI_COL32(255, 210, 70, 255) : UI_COL32(80, 245, 130, 255));
        draw->addCircle(points[face], active ? 6.f : 4.f, UI_COL32(10, 30, 15, 255), 0, 1.5f);
    }
    draw->popClipRect();
    if (face_ < 0 && !dragging_ && hovered >= 0) {
        hovered_ = true;
        ui::setTooltip("Drag to resize this collider face (opposite face stays fixed)");
        if (ui::isMouseClicked(ui::MouseButton_Left)) {
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
    bool haveMeshes = false, havePoints = false;
    Vec3 low{}, high{}, pointLow{}, pointHigh{};
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
        if (entity != selected) {
            include(pose.position, pointLow, pointHigh, havePoints);
        }
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
                    const auto data = loadModelAsset(*assets, mesh->model);
                    for (const auto& vertex : data.vertices) {
                        include(vertex.position, bounds.low, bounds.high, bounds.valid);
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
    return haveMeshes   ? (low + high) * .5f
           : havePoints ? (pointLow + pointHigh) * .5f
                        : world.get<Transform>(selected).position;
}
