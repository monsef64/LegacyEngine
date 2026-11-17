#include "ui/qt/qt_gizmo.hpp"

#include <QColor>
#include <QPainterPath>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace Gizmo {
namespace {

State gActive;

QColor axisColour(int axis, bool active) {
    if (active) {
        return QColor(255, 214, 96);
    }
    switch (axis) {
    case 0:
        return QColor(245, 82, 80);
    case 1:
        return QColor(88, 225, 132);
    default:
        return QColor(90, 155, 255);
    }
}

Quat multiply(Quat a, Quat b) {
    return normalize(Quat{a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                      a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                      a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
                      a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z});
}

Vec3 basisAxis(const Mat3& rotation, int axis) {
    return normalize(Vec3{rotation.m[axis * 3], rotation.m[axis * 3 + 1], rotation.m[axis * 3 + 2]});
}

Vec3 axisVector(const Mat3& rotation, int axis, bool local) {
    if (local) {
        return basisAxis(rotation, axis);
    }
    return axis == 0 ? Vec3{1, 0, 0} : axis == 1 ? Vec3{0, 1, 0} : Vec3{0, 0, 1};
}

bool project(const Mat4& matrix, Vec3 point, const EditorRect& viewport, Vec2& screen) {
    const float x =
        matrix.m[0] * point.x + matrix.m[4] * point.y + matrix.m[8] * point.z + matrix.m[12];
    const float y =
        matrix.m[1] * point.x + matrix.m[5] * point.y + matrix.m[9] * point.z + matrix.m[13];
    const float z =
        matrix.m[2] * point.x + matrix.m[6] * point.y + matrix.m[10] * point.z + matrix.m[14];
    const float w =
        matrix.m[3] * point.x + matrix.m[7] * point.y + matrix.m[11] * point.z + matrix.m[15];
    if (!(w > 0.0001f) || !std::isfinite(w)) {
        return false;
    }
    const float nx = x / w, ny = y / w, nz = z / w;
    screen = {viewport.x + (nx + 1.0f) * 0.5f * viewport.width,
              viewport.y + (1.0f - ny) * 0.5f * viewport.height};
    return std::isfinite(screen.x) && std::isfinite(screen.y) && nz >= -1.2f && nz <= 1.2f;
}

float pointSegmentDistanceSquared(Vec2 p, Vec2 a, Vec2 b) {
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float lengthSquared = dx * dx + dy * dy;
    const float t = lengthSquared > 0.0001f
                        ? std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSquared,
                                     0.0f,
                                     1.0f)
                        : 0.0f;
    const float x = a.x + dx * t - p.x, y = a.y + dy * t - p.y;
    return x * x + y * y;
}

Vec2 subtract(Vec2 a, Vec2 b) {
    return {a.x - b.x, a.y - b.y};
}

float quantize(float value, float step) {
    if (!(step > 0.00001f)) {
        return value;
    }
    return std::round(value / step) * step;
}

bool sameRect(const EditorRect& a, const EditorRect& b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

} // namespace

void cancel() {
    gActive = {};
}

const State& state() {
    return gActive;
}

Result manipulate(QPainter& painter,
                  const Input& input,
                  const Mat4& viewProjection,
                  float dpiScale,
                  Vec3 pivot,
                  Quat rotation,
                  Vec3 scale,
                  TransformTool tool,
                  bool localSpace,
                  bool snapEnabled,
                  float snapStep) {
    Result result;
    result.matrix = transformMatrix(pivot, rotation, scale);
    if (input.viewport.width < 1.0f || input.viewport.height < 1.0f) {
        cancel();
        return result;
    }
    if (gActive.active &&
        (gActive.tool != tool || gActive.localSpace != localSpace ||
         !sameRect(gActive.viewport, input.viewport))) {
        cancel();
    }

    const Mat3 basis = rotationMatrix(rotation);
    Vec2 pivotScreen;
    if (!project(viewProjection, pivot, input.viewport, pivotScreen)) {
        cancel();
        return result;
    }

    const Vec2 mouse = input.mouse;
    const bool mouseInside = input.hovered && input.viewport.contains(mouse.x, mouse.y);
    const float pixel = std::max(0.5f, dpiScale);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setClipRect(QRectF(input.viewport.x,
                               input.viewport.y,
                               input.viewport.width,
                               input.viewport.height));

    int hoveredAxis = -1;
    float nearest = std::numeric_limits<float>::max();
    std::array<Vec2, 3> endpoints{};
    std::array<Vec3, 3> axes{};
    std::array<float, 3> worldLengths{};
    std::array<bool, 3> projected{};

    if (tool == TransformTool::Rotate) {
        // Each ring is a 3D circle in the plane normal to its axis, projected
        // through the editor camera and hit-tested segment by segment.
        for (int axis = 0; axis < 3; ++axis) {
            axes[axis] = axisVector(basis, axis, localSpace);
            const Vec3 helper = std::abs(axes[axis].y) < .86f ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
            const Vec3 u = normalize(cross(axes[axis], helper));
            const Vec3 v = normalize(cross(axes[axis], u));
            constexpr float radius = .72f;
            std::array<Vec2, 49> points{};
            std::array<bool, 49> valid{};
            for (int i = 0; i <= 48; ++i) {
                const float angle = 2.0f * kPi * static_cast<float>(i) / 48.0f;
                const Vec3 point =
                    pivot + (u * std::cos(angle) + v * std::sin(angle)) * radius;
                const auto index = static_cast<std::size_t>(i);
                valid[index] = project(viewProjection, point, input.viewport, points[index]);
            }
            const bool ringActive = gActive.active && gActive.axis == axis;
            painter.setPen(QPen(axisColour(axis, ringActive),
                                (ringActive ? 3.0f : 2.0f) * pixel,
                                Qt::SolidLine,
                                Qt::FlatCap));
            for (int i = 1; i <= 48; ++i) {
                const auto a = static_cast<std::size_t>(i - 1), b = static_cast<std::size_t>(i);
                if (!valid[a] || !valid[b]) {
                    continue;
                }
                painter.drawLine(
                    QPointF(points[a].x, points[a].y), QPointF(points[b].x, points[b].y));
                const float distance = pointSegmentDistanceSquared(mouse, points[a], points[b]);
                if (mouseInside && distance < nearest) {
                    nearest = distance;
                    hoveredAxis = axis;
                }
            }
        }
        if (hoveredAxis >= 0 && nearest > 12.0f * 12.0f) {
            hoveredAxis = -1;
        }
    } else {
        for (int axis = 0; axis < 3; ++axis) {
            axes[axis] = axisVector(basis, axis, localSpace);
            Vec2 unitEnd;
            if (!project(viewProjection, pivot + axes[axis], input.viewport, unitEnd)) {
                continue;
            }
            const Vec2 unitDelta = subtract(unitEnd, pivotScreen);
            const float pixelsPerUnit =
                std::sqrt(unitDelta.x * unitDelta.x + unitDelta.y * unitDelta.y);
            if (!(pixelsPerUnit > .001f)) {
                continue;
            }
            worldLengths[axis] = std::clamp(58.0f / pixelsPerUnit, .0001f, 10000.0f);
            const Vec3 endpoint = pivot + axes[axis] * worldLengths[axis];
            projected[axis] = project(viewProjection, endpoint, input.viewport, endpoints[axis]);
            if (!projected[axis]) {
                continue;
            }
            const float distance = pointSegmentDistanceSquared(mouse, pivotScreen, endpoints[axis]);
            const float dx = mouse.x - endpoints[axis].x, dy = mouse.y - endpoints[axis].y;
            const float handleDistance = dx * dx + dy * dy;
            const float hitDistance =
                tool == TransformTool::Scale ? std::min(handleDistance, distance) : distance;
            if (mouseInside && hitDistance < nearest) {
                nearest = hitDistance;
                hoveredAxis = axis;
            }
        }
        if (hoveredAxis >= 0 &&
            nearest > (tool == TransformTool::Scale ? 15.0f * 15.0f : 10.0f * 10.0f)) {
            hoveredAxis = -1;
        }
        for (int axis = 0; axis < 3; ++axis) {
            if (!projected[axis]) {
                continue;
            }
            const bool selected =
                (gActive.active && gActive.axis == axis) || (!gActive.active && hoveredAxis == axis);
            const QColor color = axisColour(axis, selected);
            painter.setPen(
                QPen(color, (selected ? 3.5f : 2.5f) * pixel, Qt::SolidLine, Qt::FlatCap));
            painter.drawLine(QPointF(pivotScreen.x, pivotScreen.y),
                             QPointF(endpoints[axis].x, endpoints[axis].y));
            if (tool == TransformTool::Move) {
                const Vec2 direction = subtract(endpoints[axis], pivotScreen);
                const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y);
                if (length > 0.1f) {
                    const Vec2 n{direction.x / length, direction.y / length};
                    const Vec2 side{-n.y * 5.0f, n.x * 5.0f};
                    const QPointF tip(endpoints[axis].x, endpoints[axis].y);
                    const QPointF back(tip.x() - n.x * 11.0f, tip.y() - n.y * 11.0f);
                    QPolygonF arrow;
                    arrow << tip << QPointF(back.x() + side.x, back.y() + side.y)
                          << QPointF(back.x() - side.x, back.y() - side.y);
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(color);
                    painter.drawPolygon(arrow);
                }
            } else {
                constexpr float half = 5.5f;
                painter.setPen(Qt::NoPen);
                painter.setBrush(color);
                painter.drawRect(
                    QRectF(endpoints[axis].x - half, endpoints[axis].y - half, half * 2, half * 2));
            }
        }
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(245, 245, 250));
    painter.drawEllipse(QPointF(pivotScreen.x, pivotScreen.y), 4.0f * pixel, 4.0f * pixel);

    if (!gActive.active && hoveredAxis >= 0 && input.pressed) {
        gActive.active = true;
        gActive.axis = hoveredAxis;
        gActive.tool = tool;
        gActive.localSpace = localSpace;
        gActive.viewport = input.viewport;
        gActive.pivot = pivot;
        gActive.startScale = scale;
        gActive.startRotation = rotation;
        gActive.axisWorld = axes[hoveredAxis];
        gActive.startMouse = mouse;
        gActive.pivotScreen = pivotScreen;
        gActive.worldLength = tool == TransformTool::Rotate ? .72f : worldLengths[hoveredAxis];
        gActive.startAngle = std::atan2(mouse.y - pivotScreen.y, mouse.x - pivotScreen.x);
        if (tool != TransformTool::Rotate) {
            const Vec2 delta = subtract(endpoints[hoveredAxis], pivotScreen);
            const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
            if (length > 0.1f) {
                gActive.axisScreen = {delta.x / length, delta.y / length};
            }
        }
        result.clickedGizmo = true;
    }

    if (gActive.active && gActive.axis >= 0) {
        const bool down = input.down;
        result.hovered = true;
        result.usingHandle = down;
        const int axis = gActive.axis;
        result.matrix = transformMatrix(gActive.pivot, gActive.startRotation, gActive.startScale);
        if (down) {
            const Vec2 delta = subtract(mouse, gActive.startMouse);
            if (tool == TransformTool::Move) {
                const float screenAlong = delta.x * gActive.axisScreen.x +
                                          delta.y * gActive.axisScreen.y;
                const float worldAmount = screenAlong / 58.0f * gActive.worldLength;
                const float amount = snapEnabled ? quantize(worldAmount, snapStep) : worldAmount;
                const Vec3 moved = gActive.pivot + gActive.axisWorld * amount;
                result.matrix = transformMatrix(moved, gActive.startRotation, gActive.startScale);
                result.changed = std::abs(amount) > .000001f;
            } else if (tool == TransformTool::Rotate) {
                float angle = std::atan2(mouse.y - gActive.pivotScreen.y,
                                         mouse.x - gActive.pivotScreen.x) -
                              gActive.startAngle;
                angle = std::remainder(angle, 2.0f * kPi);
                if (snapEnabled) {
                    angle = radians(quantize(angle * 180.0f / kPi, std::max(.05f, snapStep)));
                }
                const Quat deltaRotation = axisAngle(gActive.axisWorld, angle);
                const Quat rotated = localSpace ? multiply(gActive.startRotation, deltaRotation)
                                                : multiply(deltaRotation, gActive.startRotation);
                result.matrix = transformMatrix(gActive.pivot, rotated, gActive.startScale);
                result.changed = std::abs(angle) > .000001f;
            } else {
                const float screenAlong = delta.x * gActive.axisScreen.x +
                                          delta.y * gActive.axisScreen.y;
                float factor = std::max(.05f, 1.0f + screenAlong / 58.0f);
                if (snapEnabled) {
                    factor = std::max(.05f, 1.0f + quantize(factor - 1.0f, std::max(.01f, snapStep)));
                }
                Vec3 next = gActive.startScale;
                if (axis == 0) {
                    next.x = std::clamp(next.x * factor, .05f, 100.0f);
                }
                if (axis == 1) {
                    next.y = std::clamp(next.y * factor, .05f, 100.0f);
                }
                if (axis == 2) {
                    next.z = std::clamp(next.z * factor, .05f, 100.0f);
                }
                result.matrix = transformMatrix(gActive.pivot, gActive.startRotation, next);
                result.changed = std::abs(factor - 1.0f) > .000001f;
            }
        } else {
            // The button was released (or the press never arrived): end the drag.
            cancel();
        }
        result.hovered = result.hovered || hoveredAxis == axis;
        painter.restore();
        return result;
    }

    result.hovered = hoveredAxis >= 0;
    painter.restore();
    return result;
}

} // namespace Gizmo
