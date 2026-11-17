#pragma once

#include "core/components.hpp"
#include "editor/transform_edit.hpp"
#include "ui/qt/qt_gizmo.hpp"
#include "scene/camera.hpp"
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QWidget>
#include <string>

class World;
class AssetBrowser;
class EditorHistory;
class Renderer;
class PlayMode;
class TransformEditor;

namespace ui {
namespace qt {

// The engine renders into its own OpenGL context (the SDL one) and hands Qt a
// finished QImage each frame with setImage(). No OpenGL call ever happens on
// the Qt side, so the editor has exactly one GL context and one place that
// talks to the driver.

// Displays the engine's Scene or Game framebuffer inside a dock. Mouse and
// wheel input are forwarded to the engine, which owns picking and the camera,
// because it already knows how the surface maps onto the window.
class Viewport : public QWidget {
    Q_OBJECT

  public:
    explicit Viewport(const QString& label, QWidget* parent = nullptr);
    void setImage(const QImage& image);
    void setOverlayText(const QString& text);
    void setPlayState(bool playing);
    void setFocused(bool focused);
    void setFpsVisible(bool visible);
    void setFps(float fps);
    // Normalized position (0..1 inside the picture) of a Qt widget point, the
    // way OrbitCamera::rayThrough() expects it.
    void picturePosition(const QPoint& point, float& x, float& y) const;
    QSize pictureSize() const;
    const QImage& image() const {
        return image_;
    }

  Q_SIGNALS:
    void mousePressedQt(const QPoint& point, int button, Qt::KeyboardModifiers modifiers);
    void mouseMovedQt(const QPoint& point, int buttons, Qt::KeyboardModifiers modifiers);
    void mouseReleasedQt(const QPoint& point, int button, Qt::KeyboardModifiers modifiers);
    void wheelQt(float wheel);
    void doubleClickedQt(const QPoint& point);
    void resized();
    void dragEntered(const QStringList& paths);
    void filesDropped(const QStringList& paths);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
#ifndef QT_NO_DRAGANDDROP
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
#endif

  protected:
    // The picture's rectangle inside the widget, in widget pixels. The gizmo
    // needs it to map its own coordinates back to the surface.
    QRect targetRect() const;

  private:
    QImage image_;
    QString label_;
    QString overlayText_;
    bool playing_ = false;
    bool focused_ = false;
    bool fpsVisible_ = false;
    bool hovered_ = false;
    float fps_ = 0.0f;
};

// The Scene view: orbit camera, selection and transform gizmo interaction.
class SceneView : public Viewport {
    Q_OBJECT

  public:
    explicit SceneView(QWidget* parent = nullptr);
    void bind(World& world0,
              AssetBrowser& browser0,
              EditorHistory& history0,
              Renderer& renderer0,
              TransformEditor& transform0,
              PlayMode& playMode0);
    void setSelectedEntity(Entity entity);
    Entity selectedEntity() const {
        return selected_;
    }
    void refresh();
    // The engine owns the editor camera: it renders the Scene picture with the
    // same orbit camera the widget navigates, so a second copy could drift.
    void useCamera(OrbitCamera& cameraRef) {
        engineCamera_ = &cameraRef;
    }
    OrbitCamera& camera() {
        return engineCamera_ ? *engineCamera_ : fallbackCamera_;
    }
    void focusOnSelection();
    bool interacting() const {
        return orbiting_ || panning_ || gizmoActive_;
    }
    void setTool(TransformTool tool);
    TransformTool tool() const {
        return tool_;
    }
    // Snap while dragging: the editor passes Ctrl-held here, as before.
    void setSnapEnabled(bool enabled) {
        snapEnabled_ = enabled;
    }
    // Set when a gizmo handle is being dragged. The engine skips its own
    // picking and undo commits while this is true, exactly like the old
    // TransformEditor::capturesMouse()/dragging() pair.
    bool draggingGizmo() const {
        return Gizmo::state().active;
    }
    bool dragging() const {
        return dragging_;
    }
    // Painting the picture also draws the gizmo on top of it, so gizmo drawing
    // and scene drawing happen in one place per frame.
    void paintEvent(QPaintEvent* event) override;

  protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

  private:
    World* world_ = nullptr;
    AssetBrowser* browser_ = nullptr;
    EditorHistory* history_ = nullptr;
    Renderer* renderer_ = nullptr;
    TransformEditor* transform_ = nullptr;
    PlayMode* playMode_ = nullptr;
    Entity selected_{};
    OrbitCamera* engineCamera_ = nullptr;
    OrbitCamera fallbackCamera_;
    TransformTool tool_ = TransformTool::Move;
    bool gizmoActive_ = false;
    bool orbiting_ = false;
    bool panning_ = false;
    bool rotatingView_ = false;
    bool snapEnabled_ = false;
    bool mousePressed_ = false;
    bool mousedown_ = false;
    bool dragging_ = false;
    Vec3 pivot_{};
    QPoint lastMouse_;
    QPoint mousePosition_{};
};

// The Game view: the player camera rendered by the engine.
class GameView : public Viewport {
    Q_OBJECT

  public:
    explicit GameView(QWidget* parent = nullptr);
    void bind(World& world0, Renderer& renderer0, PlayMode& playMode0);
    void refresh();

  private:
    World* world_ = nullptr;
    Renderer* renderer_ = nullptr;
    PlayMode* playMode_ = nullptr;
};

} // namespace qt
} // namespace ui
