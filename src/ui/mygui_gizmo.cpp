#include "ui/mygui_ui.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace ui {
namespace {

struct ActiveGizmo {
    bool active = false;
    int axis = -1;
    GizmoOperation operation = GizmoOperation::Translate;
    bool localSpace = false;
    Rect viewport{};
    Vec3 pivot{};
    Vec3 startScale{1, 1, 1};
    Quat startRotation{};
    Vec3 axisWorld{};
    Vec2 startMouse{};
    Vec2 pivotScreen{};
    Vec2 axisScreen{};
    float worldLength = 1.0f;
    float startAngle = 0.0f;
};

ActiveGizmo gActive;

Quat multiply(Quat a, Quat b) {
    return normalize({a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                      a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                      a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                      a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z});
}

Vec3 basisAxis(const Mat3& rotation, int axis) {
    return normalize(Vec3{rotation.m[axis * 3], rotation.m[axis * 3 + 1], rotation.m[axis * 3 + 2]});
}

bool project(const Mat4& matrix, Vec3 point, Rect viewport, Vec2& screen, float* depth = nullptr) {
    const float x = matrix.m[0] * point.x + matrix.m[4] * point.y + matrix.m[8] * point.z + matrix.m[12];
    const float y = matrix.m[1] * point.x + matrix.m[5] * point.y + matrix.m[9] * point.z + matrix.m[13];
    const float z = matrix.m[2] * point.x + matrix.m[6] * point.y + matrix.m[10] * point.z + matrix.m[14];
    const float w = matrix.m[3] * point.x + matrix.m[7] * point.y + matrix.m[11] * point.z + matrix.m[15];
    if (!(w > 0.0001f) || !std::isfinite(w)) return false;
    const float nx = x / w;
    const float ny = y / w;
    const float nz = z / w;
    screen = {viewport.x + (nx + 1.0f) * 0.5f * viewport.width,
              viewport.y + (1.0f - ny) * 0.5f * viewport.height};
    if (depth) *depth = nz;
    return std::isfinite(screen.x) && std::isfinite(screen.y) && nz >= -1.2f && nz <= 1.2f;
}

float pointSegmentDistanceSquared(Vec2 p, Vec2 a, Vec2 b, float* along = nullptr) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float denom = dx * dx + dy * dy;
    const float t = denom > 0.0001f ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / denom, 0.0f, 1.0f) : 0.0f;
    if (along) *along = t;
    const float x = a.x + dx * t - p.x;
    const float y = a.y + dy * t - p.y;
    return x * x + y * y;
}

Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }

uint32_t axisColor(int axis, bool active) {
    if (active) return UI_COL32(255, 214, 96, 255);
    static constexpr uint32_t colors[]{UI_COL32(245, 82, 80, 255),
                                       UI_COL32(88, 225, 132, 255),
                                       UI_COL32(90, 155, 255, 255)};
    return colors[axis];
}

Vec3 axisVector(const Mat3& rotation, int axis, bool local) {
    if (local) return basisAxis(rotation, axis);
    return axis == 0 ? Vec3{1, 0, 0} : axis == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
}

float quantize(float value, float step) {
    if (!(step > 0.00001f)) return value;
    return std::round(value / step) * step;
}

bool sameRect(const Rect& a, const Rect& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

} // namespace

GizmoResult manipulateGizmo(DrawList* draw,
                            Rect viewport,
                            const Mat4& viewProjection,
                            Vec3 pivot,
                            Quat rotation,
                            Vec3 scale,
                            GizmoOperation operation,
                            bool localSpace,
                            bool snapEnabled,
                            float snapStep) {
    GizmoResult result;
    result.matrix = transformMatrix(pivot, rotation, scale);
    if (!draw || viewport.width < 1.0f || viewport.height < 1.0f) {
        cancelGizmo();
        return result;
    }
    if (gActive.active && (gActive.operation != operation || gActive.localSpace != localSpace ||
                           !sameRect(gActive.viewport, viewport))) {
        cancelGizmo();
    }

    const Mat3 basis = rotationMatrix(rotation);
    Vec2 pivotScreen;
    if (!project(viewProjection, pivot, viewport, pivotScreen)) {
        cancelGizmo();
        return result;
    }
    const Vec2 mouse = io().MousePos;
    const bool mouseInside = viewport.contains(mouse.x, mouse.y) && isWindowHovered();
    int hoveredAxis = -1;
    float nearest = std::numeric_limits<float>::max();
    std::array<Vec2, 3> endpoints{};
    std::array<Vec3, 3> axes{};
    std::array<float, 3> worldLengths{};
    std::array<bool, 3> projected{};

    if (operation == GizmoOperation::Rotate) {
        // Each native ring is a 3D circle in the plane normal to its axis. It is
        // projected through the editor camera and hit-tested segment by segment.
        for (int axis = 0; axis < 3; ++axis) {
            axes[axis] = axisVector(basis, axis, localSpace);
            Vec3 helper = std::abs(axes[axis].y) < .86f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
            const Vec3 u = normalize(cross(axes[axis], helper));
            const Vec3 v = normalize(cross(axes[axis], u));
            const float radius = .72f;
            std::array<Vec2, 49> points{};
            std::array<bool, 49> valid{};
            for (int i = 0; i <= 48; ++i) {
                const float angle = 2.0f * kPi * static_cast<float>(i) / 48.0f;
                const Vec3 p = pivot + (u * std::cos(angle) + v * std::sin(angle)) * radius;
                valid[static_cast<std::size_t>(i)] = project(viewProjection, p, viewport, points[static_cast<std::size_t>(i)]);
            }
            for (int i = 1; i <= 48; ++i) {
                if (valid[static_cast<std::size_t>(i - 1)] && valid[static_cast<std::size_t>(i)]) {
                    const float d = pointSegmentDistanceSquared(mouse,
                                                                points[static_cast<std::size_t>(i - 1)],
                                                                points[static_cast<std::size_t>(i)]);
                    draw->addLine(points[static_cast<std::size_t>(i - 1)],
                                  points[static_cast<std::size_t>(i)],
                                  axisColor(axis, gActive.active && gActive.axis == axis),
                                  gActive.active && gActive.axis == axis ? 3.0f : 2.0f);
                    if (mouseInside && d < nearest) {
                        nearest = d;
                        hoveredAxis = axis;
                    }
                }
            }
        }
        if (hoveredAxis >= 0 && nearest > 12.0f * 12.0f) hoveredAxis = -1;
    } else {
        for (int axis = 0; axis < 3; ++axis) {
            axes[axis] = axisVector(basis, axis, localSpace);
            Vec2 unitEnd;
            if (!project(viewProjection, pivot + axes[axis], viewport, unitEnd)) continue;
            const Vec2 unitDelta = unitEnd - pivotScreen;
            const float pixelsPerUnit = std::sqrt(unitDelta.x * unitDelta.x + unitDelta.y * unitDelta.y);
            if (!(pixelsPerUnit > .001f)) continue;
            worldLengths[axis] = std::clamp(58.0f / pixelsPerUnit, .0001f, 10000.0f);
            const Vec3 endpoint = pivot + axes[axis] * worldLengths[axis];
            projected[axis] = project(viewProjection, endpoint, viewport, endpoints[axis]);
            if (!projected[axis]) continue;
            const float distance = pointSegmentDistanceSquared(mouse, pivotScreen, endpoints[axis]);
            const float dx = mouse.x - endpoints[axis].x;
            const float dy = mouse.y - endpoints[axis].y;
            const float handleDistance = dx * dx + dy * dy;
            const float hitDistance = operation == GizmoOperation::Scale
                                          ? std::min(handleDistance, distance)
                                          : distance;
            if (mouseInside && hitDistance < nearest) {
                nearest = hitDistance;
                hoveredAxis = axis;
            }
        }
        if (hoveredAxis >= 0 && nearest > (operation == GizmoOperation::Scale ? 15.0f * 15.0f : 10.0f * 10.0f)) {
            hoveredAxis = -1;
        }
        for (int axis = 0; axis < 3; ++axis) {
            if (!projected[axis]) continue;
            const bool selected = (gActive.active && gActive.axis == axis) || (!gActive.active && hoveredAxis == axis);
            const uint32_t color = axisColor(axis, selected);
            draw->addLine(pivotScreen, endpoints[axis], color, selected ? 3.5f : 2.5f);
            if (operation == GizmoOperation::Translate) {
                const Vec2 direction = endpoints[axis] - pivotScreen;
                const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
                if (length > 0.1f) {
                    const Vec2 n{direction.x / length, direction.y / length};
                    const Vec2 side{-n.y * 5.0f, n.x * 5.0f};
                    const Vec2 tip{endpoints[axis].x, endpoints[axis].y};
                    const Vec2 back{tip.x - n.x * 11.0f, tip.y - n.y * 11.0f};
                    draw->addTriangleFilled(tip, {back.x + side.x, back.y + side.y},
                                            {back.x - side.x, back.y - side.y}, color);
                }
            } else {
                constexpr float half = 5.5f;
                draw->addRectFilled({endpoints[axis].x - half, endpoints[axis].y - half},
                                    {endpoints[axis].x + half, endpoints[axis].y + half},
                                    color,
                                    2.0f);
            }
        }
    }
    draw->addCircleFilled(pivotScreen, 4.0f, UI_COL32(245, 245, 250, 255), 16);

    if (!gActive.active && hoveredAxis >= 0 && isMouseClicked(MouseButton_Left)) {
        gActive.active = true;
        gActive.axis = hoveredAxis;
        gActive.operation = operation;
        gActive.localSpace = localSpace;
        gActive.viewport = viewport;
        gActive.pivot = pivot;
        gActive.startScale = scale;
        gActive.startRotation = rotation;
        gActive.axisWorld = axes[hoveredAxis];
        gActive.startMouse = mouse;
        gActive.pivotScreen = pivotScreen;
        gActive.worldLength = operation == GizmoOperation::Rotate ? .72f : worldLengths[hoveredAxis];
        gActive.startAngle = std::atan2(mouse.y - pivotScreen.y, mouse.x - pivotScreen.x);
        if (operation != GizmoOperation::Rotate) {
            const Vec2 delta = endpoints[hoveredAxis] - pivotScreen;
            const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
            if (length > 0.1f) gActive.axisScreen = {delta.x / length, delta.y / length};
        }
    }

    if (gActive.active && gActive.axis >= 0) {
        const bool down = isMouseDown(MouseButton_Left);
        result.hovered = true;
        result.usingHandle = down;
        const int axis = gActive.axis;
        result.matrix = transformMatrix(gActive.pivot, gActive.startRotation, gActive.startScale);
        if (down) {
            const Vec2 delta = mouse - gActive.startMouse;
            if (operation == GizmoOperation::Translate) {
                const float screenAlong = delta.x * gActive.axisScreen.x + delta.y * gActive.axisScreen.y;
                const float worldAmount = screenAlong / 58.0f * gActive.worldLength;
                const float amount = snapEnabled ? quantize(worldAmount, snapStep) : worldAmount;
                const Vec3 moved = gActive.pivot + gActive.axisWorld * amount;
                result.matrix = transformMatrix(moved, gActive.startRotation, gActive.startScale);
                result.changed = std::abs(amount) > .000001f;
            } else if (operation == GizmoOperation::Rotate) {
                float angle = std::atan2(mouse.y - gActive.pivotScreen.y, mouse.x - gActive.pivotScreen.x) - gActive.startAngle;
                angle = std::remainder(angle, 2.0f * kPi);
                if (snapEnabled) angle = radians(quantize(angle * 180.0f / kPi, std::max(.05f, snapStep)));
                const Quat deltaRotation = axisAngle(gActive.axisWorld, angle);
                const Quat rotated = localSpace ? multiply(gActive.startRotation, deltaRotation)
                                                : multiply(deltaRotation, gActive.startRotation);
                result.matrix = transformMatrix(gActive.pivot, rotated, gActive.startScale);
                result.changed = std::abs(angle) > .000001f;
            } else {
                const float screenAlong = delta.x * gActive.axisScreen.x + delta.y * gActive.axisScreen.y;
                float factor = std::max(.05f, 1.0f + screenAlong / 58.0f);
                if (snapEnabled) factor = std::max(.05f, 1.0f + quantize(factor - 1.0f, std::max(.01f, snapStep)));
                Vec3 next = gActive.startScale;
                if (axis == 0) next.x = std::clamp(next.x * factor, .05f, 100.0f);
                if (axis == 1) next.y = std::clamp(next.y * factor, .05f, 100.0f);
                if (axis == 2) next.z = std::clamp(next.z * factor, .05f, 100.0f);
                result.matrix = transformMatrix(gActive.pivot, gActive.startRotation, next);
                result.changed = std::abs(factor - 1.0f) > .000001f;
            }
        } else {
            cancelGizmo();
        }
        result.hovered = result.hovered || hoveredAxis == axis;
        return result;
    }

    result.hovered = hoveredAxis >= 0;
    return result;
}

void cancelGizmo() {
    gActive = {};
}

} // namespace ui
