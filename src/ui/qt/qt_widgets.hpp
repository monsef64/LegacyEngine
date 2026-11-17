#pragma once

#include "core/components.hpp"
#include "editor/transform_edit.hpp"
#include "project/json_io.hpp"
#include <QColor>
#include <QFont>
#include <QString>
#include <QTreeWidget>
#include <QImage>
#include <QLabel>
#include <QUuid>
#include <QWidget>
#include <array>
#include <functional>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QToolButton;

namespace ui {
namespace qt {

// Style constants shared by every panel so the editor keeps one rhythm. The
// old immediate-mode layout ran on 18 px rows with 6/8 px spacing; DockWidget
// panels keep those numbers so editor screenshots still line up.
namespace metrics {
constexpr int row = 22;
constexpr int spacing = 6;
constexpr int panelPadding = 8;
constexpr int icon = 16;
} // namespace metrics

// Colour helpers bridging core math colours to Qt.
QColor toColor(Vec3 colour, float alpha = 1.0f);
Vec3 toVec3(const QColor& colour);

// The editor's stylesheet and palette. One call fully restyles every widget,
// including docks, tabs, menus and scrollbars, so nothing falls back to the
// host desktop theme (which was the point of the shared palette before).
void applyStyle(QWidget* root, int theme);
extern int activeTheme;

// A labelled row of three numeric fields used by Transform, collider and
// light editors. Values are only emitted as changes once editing ends, which
// keeps undo history to one entry per edit.
class Vector3Edit : public QWidget {
    Q_OBJECT

  public:
    explicit Vector3Edit(QWidget* parent = nullptr);
    void set(const Vec3& value);
    Vec3 value() const;
    void setRange(float low, float high);
    void setStep(float step);
    // Sensitivity for drag edits, matching the old drag-float speeds.
    void setSpeed(float speed);
    void setLabels(const QString& x, const QString& y, const QString& z);
    const std::array<QDoubleSpinBox*, 3>& fields() const {
        return fields_;
    }

  Q_SIGNALS:
    void edited(const Vec3& value);

  private:
    std::array<QDoubleSpinBox*, 3> fields_{};
    float speed_ = 0.01f;
    Vec3 sent_{};
};

// Single numeric row keeping the editor's drag-float feel: arrow keys and
// wheel step, double-click to type an exact number.
class FloatEdit : public QWidget {
    Q_OBJECT

  public:
    explicit FloatEdit(const QString& label, QWidget* parent = nullptr);
    void set(float value);
    float value() const;
    void setRange(float low, float high);
    void setStep(float step);
    void setSuffix(const QString& suffix);
    void setLabelWidth(int width);

  Q_SIGNALS:
    void edited(float value);

  private:
    QLabel* label_ = nullptr;
    QDoubleSpinBox* field_ = nullptr;
    float sent_ = 0.0f;
};

class IntEdit : public QWidget {
    Q_OBJECT

  public:
    explicit IntEdit(const QString& label, QWidget* parent = nullptr);
    void set(int value);
    int value() const;
    void setRange(int low, int high);
    void setStep(int step);
    void setLabelWidth(int width);

  Q_SIGNALS:
    void edited(int value);

  private:
    QLabel* label_ = nullptr;
    QSpinBox* field_ = nullptr;
    int sent_ = 0;
};

// Slider plus spin box, used for light intensity, audio volume and smoothing.
class SliderEdit : public QWidget {
    Q_OBJECT

  public:
    explicit SliderEdit(const QString& label, QWidget* parent = nullptr);
    void set(float value);
    float value() const;
    void setRange(float low, float high);
    void setStep(float step);

  Q_SIGNALS:
    void edited(float value);

  private:
    QLabel* label_ = nullptr;
    QSlider* slider_ = nullptr;
    QDoubleSpinBox* field_ = nullptr;
    float sent_ = 0.0f;
};

class CheckRow : public QWidget {
    Q_OBJECT

  public:
    explicit CheckRow(const QString& label, QWidget* parent = nullptr);
    void set(bool value);
    bool value() const;
    void setCheckbox(QCheckBox* box);

  Q_SIGNALS:
    void edited(bool value);

  private:
    QLabel* label_ = nullptr;
    QCheckBox* box_ = nullptr;
};

class ComboRow : public QWidget {
    Q_OBJECT

  public:
    explicit ComboRow(const QString& label, QWidget* parent = nullptr);
    void setItems(const QStringList& items);
    void setCurrent(int index);
    int current() const;
    QComboBox* combo() const {
        return combo_;
    }

  Q_SIGNALS:
    void edited(int index);

  private:
    QLabel* label_ = nullptr;
    QComboBox* combo_ = nullptr;
};

// Text field for names and paths, with optional browse and clear buttons.
class TextRow : public QWidget {
    Q_OBJECT

  public:
    explicit TextRow(const QString& label,
                     const QString& placeholder = QString(),
                     QWidget* parent = nullptr);
    void set(const QString& text);
    QString text() const;
    QLineEdit* lineEdit() const {
        return edit_;
    }
    void setBrowse(bool visible);
    void setReadOnly(bool readOnly);

  Q_SIGNALS:
    void edited(const QString& text);
    void browseClicked();

  private:
    QLabel* label_ = nullptr;
    QLineEdit* edit_ = nullptr;
    QToolButton* browse_ = nullptr;
};

// Thin wrapper for the labels used as section headers, mirroring the old
// separatorText() helper.
QLabel* sectionLabel(const QString& text, QWidget* parent = nullptr);

// Disabled sections in the old UI used beginDisabled/endDisabled; Qt uses
// setEnabled, which also greys the labels correctly.
void setEnabledRecursive(QWidget* widget, bool enabled);

// Colour swatch editor for editor/materials/lights. Emits on every pick.
class ColorEdit : public QWidget {
    Q_OBJECT

  public:
    explicit ColorEdit(const QString& label, QWidget* parent = nullptr);
    void set(const Vec3& colour);
    Vec3 value() const;
    void setAlphaVisible(bool visible);

  Q_SIGNALS:
    void edited(const Vec3& colour);

  private:
    QLabel* label_ = nullptr;
    class SwatchButton* swatch_ = nullptr;
    Vec3 colour_{1, 1, 1};
};

// Renders a preview image (texture, material, thumbnail) with a caption.
class ImageThumb : public QLabel {
    Q_OBJECT

  public:
    explicit ImageThumb(QWidget* parent = nullptr);
    void setImage(const QImage& image);
    void setPlaceholder();
};

// Converts an engine object's object name for display in lists and trees.
QString objectLabel(World& world, Entity entity);
QString objectIcon(World& world, Entity entity);

// Registry of object type -> create id, shared with the Create menu so the
// toolbar button and the hierarchy context menu stay in sync.
struct CreateEntry {
    const char* label;
    int id;
};
const std::vector<CreateEntry>& createEntries(int* groupCount = nullptr);

// A collapsible component section used by the Inspector. Each instance owns
// its widgets; refresh() copies live component data in only when the section
// did not just receive focus, so typing is never interrupted.
class ComponentSection : public QWidget {
    Q_OBJECT

  public:
    explicit ComponentSection(const QString& title, QWidget* parent = nullptr);
    void setTitle(const QString& title);
    void addRows(QWidget* content);
    void setExpanded(bool expanded);
    bool expanded() const;
    QString title() const {
        return title_;
    }

  Q_SIGNALS:
    void contextMenuRequested(const QPoint& globalPos);

  protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

  private:
    QString title_;
    class SectionHeader* header_ = nullptr;
    QWidget* content_ = nullptr;
};

QString shortText(const QString& text, int characters);
QString durationText(float seconds);
QString formatFloat(float value, const char* format = "%.3f");

// Splits a project-relative or absolute path into the project footer text.
QString projectDisplayName(const std::string& projectPath);

} // namespace qt
} // namespace ui
