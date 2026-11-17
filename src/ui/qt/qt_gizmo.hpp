#pragma once

#include "core/math.hpp"
#include "editor/editor_geometry.hpp"
#include "editor/transform_edit.hpp"
#include <QPainter>

// The scene gizmo, extracted from the old immediate-mode UI. It keeps the same
// hit-testing and drag maths as before; only the drawing changed: instead of
// emitting triangles into a DrawList, it paints with QPainter straight onto the
// Scene view's copy of the engine framebuffer.
//
// Input is passed in rather than read from ui::io(): mouse position, buttons
// and viewport come from the Qt widget, so this stays independent of any UI
// toolkit and can be tested without a window.
namespace Gizmo {

// A 2D point in widget pixels. The old ui:: layer had its own Vec2; the Qt
// layer only needs this one shape, so it stays local.
struct Vec2 {
    float x = 0, y = 0;
};

struct Input {
    Vec2 mouse{};
    bool hovered = false;   // pointer is over the Scene view
    bool pressed = false;   // left button went down this frame
    bool down = false;      // left button is held
    bool released = false;  // left button went up this frame
    EditorRect viewport{};  // Scene view rectangle in widget coordinates
};

struct State {
    bool active = false;
    int axis = -1;
    TransformTool tool = TransformTool::Move;
    bool localSpace = false;
    Vec3 pivot{};
    Vec3 startScale{1, 1, 1};
    Quat startRotation{};
    Vec3 axisWorld{};
    Vec2 startMouse{};
    Vec2 pivotScreen{};
    Vec2 axisScreen{};
    float worldLength = 1.0f;
    float startAngle = 0.0f;
    EditorRect viewport{};
};

struct Result {
    bool hovered = false;    // a handle is under the pointer, or a drag is running
    bool usingHandle = false; // a handle is being dragged
    bool changed = false;     // the transform matrix differs from the start
    bool clickedGizmo = false; // the press landed on a handle
    Mat4 matrix{};
};

void cancel();
const State& state();

// Draws the gizmo and returns the transform the dragged object should take.
// `viewProjection` is the editor camera, `aspect` the Scene picture's aspect.
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
                  float snapStep);

} // namespace Gizmo
