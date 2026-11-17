#include "ui/qt/qt_viewport.hpp"

#include "core/ecs.hpp"
#include "editor/asset_browser.hpp"
#include "editor/editor_geometry.hpp"
#include "editor/editor_history.hpp"
#include "editor/play_mode.hpp"
#include "editor/transform_edit.hpp"
#include "editor/transform_editor.hpp"
#include "ui/qt/qt_gizmo.hpp"

#include <QApplication>
#include <QEnterEvent>
#include <QFontMetrics>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QUrl>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace ui {
namespace qt {

// ---------------------------------------------------------------------------
// Viewport
// ---------------------------------------------------------------------------

Viewport::Viewport(const QString& label, QWidget* parent) : QWidget(parent), label_(label) {
    setObjectName(label);
    setMinimumSize(160, 120);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAcceptDrops(true);
}

void Viewport::setImage(const QImage& image) {
    if (image.isNull()) {
        return;
    }
    image_ = image;
    update();
}

void Viewport::setOverlayText(const QString& text) {
    if (overlayText_ == text) {
        return;
    }
    overlayText_ = text;
    update();
}

void Viewport::setPlayState(bool playing) {
    if (playing_ == playing) {
        return;
    }
    playing_ = playing;
    update();
}

void Viewport::setFocused(bool focused) {
    if (focused_ == focused) {
        return;
    }
    focused_ = focused;
    update();
}

void Viewport::setFpsVisible(bool visible) {
    if (fpsVisible_ == visible) {
        return;
    }
    fpsVisible_ = visible;
    update();
}

void Viewport::setFps(float fps) {
    if (std::abs(fps - fps_) < 0.05f) {
        return;
    }
    fps_ = fps;
    if (fpsVisible_) {
        update();
    }
}

QRect Viewport::targetRect() const {
    if (image_.isNull()) {
        return rect();
    }
    const QSize scaled = image_.size().scaled(size(), Qt::KeepAspectRatio);
    return QRect(QPoint((width() - scaled.width()) / 2, (height() - scaled.height()) / 2), scaled);
}

QSize Viewport::pictureSize() const {
    return targetRect().size();
}

void Viewport::picturePosition(const QPoint& point, float& x, float& y) const {
    const QRect target = targetRect();
    x = target.width() > 0 ? float(point.x() - target.x()) / float(target.width()) : .5f;
    y = target.height() > 0 ? float(point.y() - target.y()) / float(target.height()) : .5f;
}

void Viewport::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(22, 24, 28));
    if (image_.isNull()) {
        painter.setPen(QColor(122, 138, 158));
        painter.drawText(rect(), Qt::AlignCenter, label_);
        return;
    }
    const QRect target = targetRect();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(target, image_);

    QFont font = painter.font();
    font.setPixelSize(12);
    painter.setFont(font);
    int banner = 8;
    if (fpsVisible_) {
        const QString text = QStringLiteral("FPS: %1").arg(std::lround(fps_));
        const QFontMetrics metrics(font);
        const QRect box(target.left() + 8,
                        target.top() + banner,
                        metrics.horizontalAdvance(text) + 12,
                        20);
        painter.fillRect(box, QColor(0, 0, 0, 150));
        painter.setPen(Qt::white);
        painter.drawText(box, Qt::AlignCenter, text);
        banner += 24;
    }
    if (!overlayText_.isEmpty()) {
        const QFontMetrics metrics(font);
        const QRect box(target.left() + 8,
                        target.top() + banner,
                        metrics.horizontalAdvance(overlayText_) + 12,
                        20);
        painter.fillRect(box, QColor(0, 0, 0, 150));
        painter.setPen(Qt::white);
        painter.drawText(box, Qt::AlignCenter, overlayText_);
    }
    if (playing_) {
        const QString text = QStringLiteral("PLAYING");
        const QFontMetrics metrics(font);
        const QRect box(target.right() - metrics.horizontalAdvance(text) - 20,
                        target.top() + 8,
                        metrics.horizontalAdvance(text) + 12,
                        20);
        painter.fillRect(box, QColor(200, 44, 44, 190));
        painter.setPen(Qt::white);
        painter.drawText(box, Qt::AlignCenter, text);
    }
    if (focused_ || hasFocus()) {
        painter.setPen(QPen(QColor(89, 166, 255), 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(target.adjusted(0, 0, -1, -1));
    }
}

void Viewport::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    Q_EMIT resized();
}

void Viewport::mousePressEvent(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
    Q_EMIT mousePressedQt(event->position().toPoint(), int(event->button()), event->modifiers());
    QWidget::mousePressEvent(event);
}

void Viewport::mouseMoveEvent(QMouseEvent* event) {
    Q_EMIT mouseMovedQt(event->position().toPoint(), int(event->buttons()), event->modifiers());
    QWidget::mouseMoveEvent(event);
}

void Viewport::mouseReleaseEvent(QMouseEvent* event) {
    Q_EMIT mouseReleasedQt(event->position().toPoint(), int(event->button()), event->modifiers());
    QWidget::mouseReleaseEvent(event);
}

void Viewport::mouseDoubleClickEvent(QMouseEvent* event) {
    Q_EMIT doubleClickedQt(event->position().toPoint());
    QWidget::mouseDoubleClickEvent(event);
}

void Viewport::wheelEvent(QWheelEvent* event) {
    const QPoint angle = event->angleDelta();
    const float wheel = (angle.y() != 0 ? float(angle.y()) : float(angle.x())) / 120.0f;
    if (wheel != 0.0f) {
        Q_EMIT wheelQt(wheel);
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

void Viewport::enterEvent(QEnterEvent* event) {
    hovered_ = true;
    QWidget::enterEvent(event);
}

void Viewport::leaveEvent(QEvent* event) {
    hovered_ = false;
    QWidget::leaveEvent(event);
}

#ifndef QT_NO_DRAGANDDROP
void Viewport::dragEnterEvent(QDragEnterEvent* event) {
    if (!event->mimeData()->hasUrls()) {
        event->ignore();
        return;
    }
    QStringList paths;
    for (const auto& url : event->mimeData()->urls()) {
        paths << url.toLocalFile();
    }
    Q_EMIT dragEntered(paths);
    event->acceptProposedAction();
}

void Viewport::dropEvent(QDropEvent* event) {
    QStringList paths;
    for (const auto& url : event->mimeData()->urls()) {
        paths << url.toLocalFile();
    }
    Q_EMIT filesDropped(paths);
    event->acceptProposedAction();
}
#endif

// ---------------------------------------------------------------------------
// SceneView
// ---------------------------------------------------------------------------

SceneView::SceneView(QWidget* parent) : Viewport(QStringLiteral("Scene"), parent) {
    setToolTip(tr("Scene view: right drag orbits, Alt+right drag looks, middle drag pans, "
                  "wheel zooms. W/E/R switch Move/Rotate/Scale."));
}

void SceneView::bind(World& world0,
                     AssetBrowser& browser0,
                     EditorHistory& history0,
                     Renderer& renderer0,
                     TransformEditor& transform0,
                     PlayMode& playMode0) {
    world_ = &world0;
    browser_ = &browser0;
    history_ = &history0;
    renderer_ = &renderer0;
    transform_ = &transform0;
    playMode_ = &playMode0;
}

void SceneView::setSelectedEntity(Entity entity) {
    if (entity.index != selected_.index || entity.generation != selected_.generation) {
        selected_ = entity;
        Gizmo::cancel();
    }
}

void SceneView::refresh() {
    if (transform_) {
        tool_ = transform_->tool();
    }
    setPlayState(playMode_ && playMode_->active());
}

void SceneView::setTool(TransformTool tool) {
    tool_ = tool;
    if (transform_) {
        transform_->setTool(tool);
    }
}

void SceneView::focusOnSelection() {
    if (!world_ || !world_->alive(selected_)) {
        return;
    }
    const auto* transform = world_->tryGet<Transform>(selected_);
    if (!transform) {
        return;
    }
    const auto& scale = transform->scale;
    float radius = std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z), 1.0f});
    if (const auto* collider = world_->tryGet<Collider>(selected_)) {
        const Vec3 half = collider->size * .5f;
        radius = std::max(radius, std::sqrt(dot(half, half)));
    }
    const QSize picture = pictureSize();
    camera().focus(transform->position,
                  radius,
                  float(std::max(1, picture.width())) / std::max(1, picture.height()));
}

void SceneView::paintEvent(QPaintEvent* event) {
    Viewport::paintEvent(event);
    if (!world_ || !world_->alive(selected_) || playMode_ && playMode_->active()) {
        return;
    }
    const auto* transform = world_->tryGet<Transform>(selected_);
    if (!transform) {
        return;
    }
    const QRect target = targetRect();
    if (target.isEmpty()) {
        return;
    }
    // The gizmo works in the picture's pixels; the scene camera maps them back
    // into the world exactly as it did when the picture filled the window.
    const float aspect = float(target.width()) / std::max(1, target.height());
    const EditorRect viewport{float(target.x()),
                              float(target.y()),
                              float(target.width()),
                              float(target.height())};

    // Pivot: the object's origin, or the collider's centre when the toolbar's
    // "Centre" pivot mode is on, which matches what the old toolbar did.
    Vec3 pivot = transform->position;
    if (const auto* collider = world_->tryGet<Collider>(selected_);
        collider && transform_ && transform_->centerPivot()) {
        const Vec3 offset = collider->center * transform->scale;
        if (dot(offset, offset) > 0) {
            pivot = pivot + rotateVector(transform->rotation, offset);
        }
    }

    QPainter painter(this);
    Gizmo::Input input;
    input.mouse = {float(mousePosition_.x()), float(mousePosition_.y())};
    input.hovered = underMouse();
    input.pressed = mousePressed_;
    input.down = mousedown_;
    input.viewport = viewport;
    const auto result = Gizmo::manipulate(painter,
                                          input,
                                          camera().viewProjection(aspect),
                                          float(devicePixelRatioF()),
                                          pivot,
                                          transform->rotation,
                                          transform->scale,
                                          tool_,
                                          transform_ && transform_->localSpace(),
                                          snapEnabled_,
                                          transform_ ? transform_->snapStep() : .5f);
    painter.end();
    mousePressed_ = false;
    if (result.changed) {
        if (auto* live = world_->tryGet<Transform>(selected_)) {
            applyGizmoMatrix(*live, result.matrix, tool_, false);
        }
    }
    if (result.usingHandle) {
        if (!dragging_) {
            dragging_ = true;
            pivot_ = pivot;
            if (history_) {
                history_->commit(*world_, selected_);
            }
        }
    } else if (dragging_) {
        // One undo entry per gesture: the world is updated while dragging and
        // the commit happens once, on release.
        dragging_ = false;
        Gizmo::cancel();
        if (transform_) {
            transform_->finish(*world_);
        }
        if (history_) {
            history_->commit(*world_, selected_);
        }
        update();
    }
}

void SceneView::mousePressEvent(QMouseEvent* event) {
    setFocus(Qt::MouseFocusReason);
    mousePosition_ = event->position().toPoint();
    if (event->button() == Qt::LeftButton) {
        mousePressed_ = true;
        mousedown_ = true;
        update();
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton) {
        rotatingView_ = event->modifiers().testFlag(Qt::AltModifier);
        orbiting_ = !rotatingView_;
        lastMouse_ = mousePosition_;
        event->accept();
        return;
    }
    if (event->button() == Qt::MiddleButton) {
        panning_ = true;
        lastMouse_ = mousePosition_;
        event->accept();
        return;
    }
    Viewport::mousePressEvent(event);
}

void SceneView::mouseMoveEvent(QMouseEvent* event) {
    mousePosition_ = event->position().toPoint();
    if (orbiting_ || rotatingView_) {
        const QPoint delta = mousePosition_ - lastMouse_;
        if (rotatingView_) {
            camera().look(float(delta.x()), float(-delta.y()));
        } else {
            camera().orbit(camera().target, float(delta.x()), float(-delta.y()));
        }
        lastMouse_ = mousePosition_;
        event->accept();
        return;
    }
    if (panning_) {
        const QPoint delta = mousePosition_ - lastMouse_;
        camera().pan(float(-delta.x()) * .05f);
        camera().move(float(-delta.y()) * .02f, 0, 0, 1.0f);
        lastMouse_ = mousePosition_;
        event->accept();
        return;
    }
    update();
    Q_EMIT mouseMovedQt(mousePosition_,
                      int(event->buttons()),
                      event->modifiers());
}

void SceneView::mouseReleaseEvent(QMouseEvent* event) {
    mousePosition_ = event->position().toPoint();
    if (event->button() == Qt::LeftButton) {
        mousedown_ = false;
        mousePressed_ = false;
        update();
        Q_EMIT mouseReleasedQt(mousePosition_, int(Qt::LeftButton), event->modifiers());
        return;
    }
    if (event->button() == Qt::RightButton) {
        orbiting_ = rotatingView_ = false;
    }
    if (event->button() == Qt::MiddleButton) {
        panning_ = false;
    }
    Viewport::mouseReleaseEvent(event);
}

void SceneView::wheelEvent(QWheelEvent* event) {
    const float wheel = float(event->angleDelta().y()) / 120.0f;
    if (wheel != 0.0f) {
        camera().zoom(wheel);
        event->accept();
        return;
    }
    Viewport::wheelEvent(event);
}

void SceneView::leaveEvent(QEvent* event) {
    mousePosition_ = QPoint(-100000, -100000);
    Viewport::leaveEvent(event);
    update();
}

// ---------------------------------------------------------------------------
// GameView
// ---------------------------------------------------------------------------

GameView::GameView(QWidget* parent) : Viewport(QStringLiteral("Game"), parent) {
    setToolTip(tr("Game view: what the active camera sees."));
}

void GameView::bind(World& world0, Renderer& renderer0, PlayMode& playMode0) {
    world_ = &world0;
    renderer_ = &renderer0;
    playMode_ = &playMode0;
}

void GameView::refresh() {
    setPlayState(playMode_ && playMode_->active());
    if (!world_) {
        return;
    }
    bool active = false;
    world_->each<Camera>([&](Entity, const Camera& camera) { active |= camera.active; });
    setOverlayText(active ? QString() : tr("No active camera available"));
}

} // namespace qt
} // namespace ui
