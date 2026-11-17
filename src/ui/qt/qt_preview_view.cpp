#include "ui/qt/qt_preview_view.hpp"

#include "scene/camera.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

namespace ui {
namespace qt {

PreviewView::PreviewView(QWidget* parent) : QWidget(parent) {
    setMinimumSize(240, 135);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(false);
}

void PreviewView::setImage(const QImage& image) {
    if (image.isNull()) {
        return;
    }
    if (image.size() != image_.size() || image.constBits() != image_.constBits()) {
        image_ = image;
        update();
        return;
    }
    image_ = image;
    update();
}

void PreviewView::setOrbitCamera(OrbitCamera* camera) {
    camera_ = camera;
}

QRect PreviewView::target() const {
    if (image_.isNull()) {
        return rect();
    }
    const QSize scaled = image_.size().scaled(size(), Qt::KeepAspectRatio);
    return QRect(QPoint((width() - scaled.width()) / 2, (height() - scaled.height()) / 2), scaled);
}

void PreviewView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(20, 22, 26));
    if (image_.isNull()) {
        painter.setPen(QColor(122, 138, 158));
        painter.drawText(rect(), Qt::AlignCenter, tr("No preview"));
        return;
    }
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(target(), image_);
}

void PreviewView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        orbiting_ = true;
        lastMouse_ = event->position().toPoint();
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void PreviewView::mouseMoveEvent(QMouseEvent* event) {
    if (orbiting_ && camera_) {
        const QPoint delta = event->position().toPoint() - lastMouse_;
        camera_->orbit(camera_->target, float(delta.x()), float(delta.y()));
        lastMouse_ = event->position().toPoint();
        Q_EMIT cameraMoved();
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void PreviewView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        orbiting_ = false;
    }
    QWidget::mouseReleaseEvent(event);
}

void PreviewView::wheelEvent(QWheelEvent* event) {
    if (camera_) {
        const float wheel = float(event->angleDelta().y()) / 120.0f;
        if (wheel != 0.0f) {
            camera_->zoom(wheel);
            Q_EMIT cameraMoved();
            event->accept();
            return;
        }
    }
    QWidget::wheelEvent(event);
}

void PreviewView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    update();
}

} // namespace qt
} // namespace ui
