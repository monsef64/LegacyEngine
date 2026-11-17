#pragma once

#include <QImage>
#include <QWidget>

struct OrbitCamera;

namespace ui {
namespace qt {

// Paints an engine framebuffer (as a QImage the engine copies out of its own
// GL context) and forwards the orbit/zoom gestures the preview windows used to
// read from ui::io(). No OpenGL call happens here.
class PreviewView : public QWidget {
    Q_OBJECT

  public:
    explicit PreviewView(QWidget* parent = nullptr);
    void setImage(const QImage& image);
    // When set, right-drag orbits and the wheel zooms this camera in place.
    void setOrbitCamera(OrbitCamera* camera);

  Q_SIGNALS:
    void cameraMoved();

  protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

  private:
    QRect target() const;
    QImage image_;
    OrbitCamera* camera_ = nullptr;
    QPoint lastMouse_;
    bool orbiting_ = false;
};

} // namespace qt
} // namespace ui
