#include "ui/qt/qt_widgets.hpp"

#include "audio/audio_source.hpp"
#include "core/hierarchy.hpp"
#include "lighting/light.hpp"
#include "particles/particle_system.hpp"
#include "terrain/terrain.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSlider>
#include <QStyle>
#include <QStyleFactory>
#include <QToolButton>
#include <QVBoxLayout>
#include <QPalette>
#include <cmath>
#include <algorithm>

namespace ui {
namespace qt {

int activeTheme = 0;

namespace {

QString colourName(const QColor& colour) {
    return colour.name(QColor::HexArgb);
}

// The palette mirrors ui::style() so a theme change made through editor
// settings recolours both the scene overlay text and the Qt widgets.
struct Palette {
    QColor window, base, alternateBase, text, disabledText, button, buttonText, highlight,
        highlightedText, border, accent, titleBg;
};

Palette paletteForTheme(int theme) {
    Palette p;
    switch (theme) {
    case 2: // Unity 5 Classic: light grey editor
        p.window = QColor(195, 195, 195);
        p.base = QColor(214, 214, 214);
        p.alternateBase = QColor(206, 206, 206);
        p.text = QColor(30, 30, 30);
        p.disabledText = QColor(120, 120, 120);
        p.button = QColor(210, 210, 210);
        p.buttonText = QColor(30, 30, 30);
        p.highlight = QColor(58, 111, 165);
        p.highlightedText = QColor(255, 255, 255);
        p.border = QColor(150, 150, 150);
        p.accent = QColor(58, 111, 165);
        p.titleBg = QColor(180, 180, 180);
        break;
    case 1: // Grey
        p.window = QColor(54, 54, 54);
        p.base = QColor(66, 66, 66);
        p.alternateBase = QColor(60, 60, 60);
        p.text = QColor(220, 220, 220);
        p.disabledText = QColor(130, 130, 130);
        p.button = QColor(78, 78, 78);
        p.buttonText = QColor(225, 225, 225);
        p.highlight = QColor(96, 140, 190);
        p.highlightedText = QColor(255, 255, 255);
        p.border = QColor(36, 36, 36);
        p.accent = QColor(96, 140, 190);
        p.titleBg = QColor(46, 46, 46);
        break;
    case 3: // Unity Dark Classic
        p.window = QColor(46, 46, 46);
        p.base = QColor(38, 38, 38);
        p.alternateBase = QColor(42, 42, 42);
        p.text = QColor(210, 210, 210);
        p.disabledText = QColor(120, 120, 120);
        p.button = QColor(64, 64, 64);
        p.buttonText = QColor(220, 220, 220);
        p.highlight = QColor(61, 92, 133);
        p.highlightedText = QColor(255, 255, 255);
        p.border = QColor(28, 28, 28);
        p.accent = QColor(61, 110, 168);
        p.titleBg = QColor(56, 56, 56);
        break;
    default: // Legacy Dark, the historical editor default
        p.window = QColor(26, 31, 38);
        p.base = QColor(33, 39, 48);
        p.alternateBase = QColor(29, 34, 42);
        p.text = QColor(224, 232, 240);
        p.disabledText = QColor(122, 138, 158);
        p.button = QColor(46, 63, 86);
        p.buttonText = QColor(228, 236, 245);
        p.highlight = QColor(61, 122, 196);
        p.highlightedText = QColor(255, 255, 255);
        p.border = QColor(18, 22, 27);
        p.accent = QColor(97, 178, 255);
        p.titleBg = QColor(20, 25, 31);
        break;
    }
    return p;
}

} // namespace

QColor toColor(Vec3 colour, float alpha) {
    auto channel = [](float value) {
        return static_cast<int>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return QColor(channel(colour.x), channel(colour.y), channel(colour.z), channel(alpha));
}

Vec3 toVec3(const QColor& colour) {
    return {static_cast<float>(colour.redF()),
            static_cast<float>(colour.greenF()),
            static_cast<float>(colour.blueF())};
}

void applyStyle(QWidget* root, int theme) {
    activeTheme = std::clamp(theme, 0, 3);
    if (!root) {
        return;
    }
    const auto p = paletteForTheme(activeTheme);

    QPalette palette;
    palette.setColor(QPalette::Window, p.window);
    palette.setColor(QPalette::WindowText, p.text);
    palette.setColor(QPalette::Base, p.base);
    palette.setColor(QPalette::AlternateBase, p.alternateBase);
    palette.setColor(QPalette::ToolTipBase, p.base);
    palette.setColor(QPalette::ToolTipText, p.text);
    palette.setColor(QPalette::Text, p.text);
    palette.setColor(QPalette::PlaceholderText, p.disabledText);
    palette.setColor(QPalette::Button, p.button);
    palette.setColor(QPalette::ButtonText, p.buttonText);
    palette.setColor(QPalette::BrightText, QColor(255, 110, 110));
    palette.setColor(QPalette::Highlight, p.highlight);
    palette.setColor(QPalette::HighlightedText, p.highlightedText);
    palette.setColor(QPalette::Link, p.accent);
    palette.setColor(QPalette::Disabled, QPalette::Text, p.disabledText);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, p.disabledText);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, p.disabledText);
    palette.setColor(QPalette::Disabled, QPalette::Base, p.alternateBase);
    root->setPalette(palette);
    if (auto* app = qobject_cast<QApplication*>(QCoreApplication::instance())) {
        if (app->style()->objectName().compare(QStringLiteral("fusion"), Qt::CaseInsensitive) != 0) {
            app->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        }
        app->setPalette(palette);
    }

    const QString sheet = QStringLiteral(
                              "QWidget { color: %1; background-color: %2; }"
                              "QMainWindow::separator { background: %3; width: 3px; height: 3px; }"
                              "QMainWindow::separator:hover { background: %5; }"
                              "QDockWidget { color: %1; titlebar-close-icon: none; titlebar-normal-icon: none; }"
                              "QDockWidget::title { background: %6; padding: 4px 8px; text-align: left; "
                              "border: 1px solid %3; }"
                              "QTabBar::tab { background: %4; color: %1; padding: 5px 12px; border: 1px solid %3; "
                              "border-bottom: none; }"
                              "QTabBar::tab:selected { background: %2; color: %5; }"
                              "QTabBar::tab:hover { background: %6; }"
                              "QTabWidget::pane { border: 1px solid %3; }"
                              "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit, QTextEdit, QListView, "
                              "QTreeView, QTableView { background: %7; border: 1px solid %3; border-radius: 3px; "
                              "padding: 2px 4px; selection-background-color: %5; selection-color: %8; }"
                              "QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, "
                              "QPlainTextEdit:focus { border: 1px solid %5; }"
                              "QComboBox QAbstractItemView { background: %7; border: 1px solid %3; }"
                              "QPushButton, QToolButton { background: %4; border: 1px solid %3; border-radius: 3px; "
                              "padding: 3px 10px; }"
                              "QPushButton:hover, QToolButton:hover { background: %6; }"
                              "QPushButton:pressed, QToolButton:pressed, QPushButton:checked, QToolButton:checked { "
                              "background: %5; color: %8; }"
                              "QPushButton:disabled, QToolButton:disabled { color: %9; background: %2; }"
                              "QMenuBar { background: %6; }"
                              "QMenuBar::item { background: transparent; padding: 4px 9px; }"
                              "QMenuBar::item:selected { background: %5; color: %8; }"
                              "QMenu { background: %7; border: 1px solid %3; }"
                              "QMenu::item { padding: 4px 26px 4px 22px; }"
                              "QMenu::item:selected { background: %5; color: %8; }"
                              "QMenu::item:disabled { color: %9; }"
                              "QMenu::separator { height: 1px; background: %3; margin: 4px 8px; }"
                              "QHeaderView::section { background: %4; color: %1; border: none; "
                              "border-right: 1px solid %3; padding: 3px 6px; }"
                              "QTreeWidget, QTreeView { outline: none; }"
                              "QTreeWidget::item, QTreeView::item { padding: 1px 0px; }"
                              "QTreeWidget::item:selected, QTreeView::item:selected { background: %5; color: %8; }"
                              "QScrollBar:vertical { background: %2; width: 11px; margin: 0px; }"
                              "QScrollBar::handle:vertical { background: %3; min-height: 24px; border-radius: 5px; }"
                              "QScrollBar::handle:vertical:hover { background: %5; }"
                              "QScrollBar:horizontal { background: %2; height: 11px; margin: 0px; }"
                              "QScrollBar::handle:horizontal { background: %3; min-width: 24px; border-radius: 5px; }"
                              "QScrollBar::add-line, QScrollBar::sub-line { width: 0px; height: 0px; }"
                              "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }"
                              "QStatusBar { background: %6; border-top: 1px solid %3; }"
                              "QToolTip { background: %7; color: %1; border: 1px solid %3; padding: 3px; }"
                              "QCheckBox::indicator { width: 13px; height: 13px; border: 1px solid %3; "
                              "background: %7; border-radius: 2px; }"
                              "QCheckBox::indicator:checked { background: %5; }"
                              "QSplitter::handle { background: %3; }"
                              "QGroupBox { border: 1px solid %3; border-radius: 4px; margin-top: 14px; "
                              "padding-top: 6px; }"
                              "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; }")
                              .arg(colourName(p.text),
                                   colourName(p.window),
                                   colourName(p.border),
                                   colourName(p.button),
                                   colourName(p.highlight),
                                   colourName(p.titleBg),
                                   colourName(p.base),
                                   colourName(p.highlightedText),
                                   colourName(p.disabledText));
    root->setStyleSheet(sheet);
}

namespace {

// A spin box that behaves like the old drag-float: the mouse wheel and drag
// change the value, and the cursor changes to the horizontal resize shape.
class DragSpinBox : public QDoubleSpinBox {
  public:
    explicit DragSpinBox(QWidget* parent = nullptr) : QDoubleSpinBox(parent) {
        setButtonSymbols(QAbstractSpinBox::NoButtons);
        setKeyboardTracking(false);
        setAlignment(Qt::AlignRight);
    }

    void setDragSpeed(float speed) {
        speed_ = speed;
    }

  protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            dragging_ = true;
            dragStart_ = event->pos().x();
            dragValue_ = value();
            setCursor(Qt::SizeHorCursor);
            event->accept();
            return;
        }
        QDoubleSpinBox::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (dragging_) {
            const int delta = event->pos().x() - dragStart_;
            setValue(dragValue_ + delta * speed_);
            event->accept();
            return;
        }
        QDoubleSpinBox::mouseMoveEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (dragging_) {
            dragging_ = false;
            unsetCursor();
            event->accept();
            return;
        }
        QDoubleSpinBox::mouseReleaseEvent(event);
    }
    void focusInEvent(QFocusEvent* event) override {
        QDoubleSpinBox::focusInEvent(event);
        if (event->reason() == Qt::MouseFocusReason) {
            selectAll();
        }
    }

  private:
    float speed_ = 0.01f;
    bool dragging_ = false;
    int dragStart_ = 0;
    double dragValue_ = 0.0;
};

QDoubleSpinBox* makeSpin(float step) {
    auto* box = new DragSpinBox();
    box->setDecimals(3);
    box->setDragSpeed(step);
    box->setSingleStep(step);
    box->setDragSpeed(step);
    box->setMinimum(-1e9);
    box->setMaximum(1e9);
    box->setMinimumWidth(48);
    box->setMaximumWidth(QWIDGETSIZE_MAX);
    box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return box;
}

} // namespace

// ---------------------------------------------------------------------------
// Vector3Edit
// ---------------------------------------------------------------------------

Vector3Edit::Vector3Edit(QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    const QStringList axes{QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
    for (int i = 0; i < 3; ++i) {
        auto* axis = new QLabel(axes[i], this);
        axis->setFixedWidth(10);
        axis->setStyleSheet(QStringLiteral("color: %1;")
                                .arg(i == 0   ? QStringLiteral("#e06c6c")
                                     : i == 1 ? QStringLiteral("#7ec97e")
                                              : QStringLiteral("#6fa8dc")));
        layout->addWidget(axis);
        fields_[i] = makeSpin(0.05f);
        layout->addWidget(fields_[i], 1);
        connect(fields_[i],
                QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this,
                [this](double) {
                    const Vec3 now = value();
                    if (now.x == sent_.x && now.y == sent_.y && now.z == sent_.z) {
                        return;
                    }
                    sent_ = now;
                    Q_EMIT edited(now);
                });
    }
}

void Vector3Edit::set(const Vec3& value) {
    sent_ = value;
    for (int i = 0; i < 3; ++i) {
        QSignalBlocker block(fields_[i]);
        fields_[i]->setValue(i == 0 ? value.x : i == 1 ? value.y : value.z);
    }
}

Vec3 Vector3Edit::value() const {
    return {static_cast<float>(fields_[0]->value()),
            static_cast<float>(fields_[1]->value()),
            static_cast<float>(fields_[2]->value())};
}

void Vector3Edit::setRange(float low, float high) {
    for (auto* field : fields_) {
        field->setMinimum(low);
        field->setMaximum(high);
    }
}

void Vector3Edit::setStep(float step) {
    for (auto* field : fields_) {
        field->setSingleStep(step);
        static_cast<DragSpinBox*>(field)->setDragSpeed(step);
    }
    speed_ = step;
}

void Vector3Edit::setSpeed(float speed) {
    for (auto* field : fields_) {
        static_cast<DragSpinBox*>(field)->setDragSpeed(speed);
    }
    speed_ = speed;
}

void Vector3Edit::setLabels(const QString& x, const QString& y, const QString& z) {
    auto* layout = qobject_cast<QHBoxLayout*>(this->layout());
    if (!layout) {
        return;
    }
    const QStringList labels{x, y, z};
    int index = 0;
    for (int i = 0; i < layout->count() && index < 3; ++i) {
        if (auto* label = qobject_cast<QLabel*>(layout->itemAt(i)->widget())) {
            label->setText(labels[index++]);
        }
    }
}

// ---------------------------------------------------------------------------
// FloatEdit / IntEdit / SliderEdit
// ---------------------------------------------------------------------------

FloatEdit::FloatEdit(const QString& label, QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(metrics::spacing);
    label_ = new QLabel(label, this);
    label_->setMinimumWidth(96);
    layout->addWidget(label_, 1);
    field_ = makeSpin(0.05f);
    layout->addWidget(field_, 2);
    connect(field_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double value) {
        if (static_cast<float>(value) == sent_) {
            return;
        }
        sent_ = static_cast<float>(value);
        Q_EMIT edited(sent_);
    });
}

void FloatEdit::set(float value) {
    sent_ = value;
    QSignalBlocker block(field_);
    field_->setValue(value);
}

float FloatEdit::value() const {
    return static_cast<float>(field_->value());
}

void FloatEdit::setRange(float low, float high) {
    field_->setMinimum(low);
    field_->setMaximum(high);
}

void FloatEdit::setStep(float step) {
    field_->setSingleStep(step);
    static_cast<DragSpinBox*>(field_)->setDragSpeed(step);
}

void FloatEdit::setSuffix(const QString& suffix) {
    field_->setSuffix(suffix);
}

void FloatEdit::setLabelWidth(int width) {
    label_->setMinimumWidth(width);
}

IntEdit::IntEdit(const QString& label, QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(metrics::spacing);
    label_ = new QLabel(label, this);
    label_->setMinimumWidth(96);
    layout->addWidget(label_, 1);
    auto* box = new QSpinBox(this);
    box->setButtonSymbols(QAbstractSpinBox::NoButtons);
    box->setKeyboardTracking(false);
    box->setAlignment(Qt::AlignRight);
    box->setRange(-1000000, 1000000);
    field_ = box;
    layout->addWidget(field_, 2);
    connect(field_, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int value) {
        if (value == sent_) {
            return;
        }
        sent_ = value;
        Q_EMIT edited(sent_);
    });
}

void IntEdit::set(int value) {
    sent_ = value;
    QSignalBlocker block(field_);
    field_->setValue(value);
}

int IntEdit::value() const {
    return field_->value();
}

void IntEdit::setRange(int low, int high) {
    field_->setRange(low, high);
}

void IntEdit::setStep(int step) {
    field_->setSingleStep(step);
}

void IntEdit::setLabelWidth(int width) {
    label_->setMinimumWidth(width);
}

SliderEdit::SliderEdit(const QString& label, QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(metrics::spacing);
    label_ = new QLabel(label, this);
    label_->setMinimumWidth(96);
    layout->addWidget(label_, 1);
    slider_ = new QSlider(Qt::Horizontal, this);
    slider_->setRange(0, 1000);
    layout->addWidget(slider_, 2);
    field_ = makeSpin(0.1f);
    field_->setMaximumWidth(78);
    layout->addWidget(field_);
    auto apply = [this](double value) {
        if (static_cast<float>(value) == sent_) {
            return;
        }
        sent_ = static_cast<float>(value);
        QSignalBlocker block(field_);
        field_->setValue(sent_);
        Q_EMIT edited(sent_);
    };
    connect(slider_, &QSlider::valueChanged, this, [this, apply](int sliderValue) {
        const double low = field_->minimum(), high = field_->maximum();
        apply(low + (high - low) * (sliderValue / 1000.0));
    });
    connect(field_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this, apply](double value) {
        const double low = field_->minimum(), high = field_->maximum();
        QSignalBlocker block(slider_);
        if (high > low) {
            slider_->setValue(static_cast<int>(((value - low) / (high - low)) * 1000.0));
        }
        apply(value);
    });
}

void SliderEdit::set(float value) {
    sent_ = value;
    QSignalBlocker blockField(field_);
    QSignalBlocker blockSlider(slider_);
    field_->setValue(value);
    const double low = field_->minimum(), high = field_->maximum();
    if (high > low) {
        slider_->setValue(static_cast<int>(((value - low) / (high - low)) * 1000.0));
    }
}

float SliderEdit::value() const {
    return static_cast<float>(field_->value());
}

void SliderEdit::setRange(float low, float high) {
    field_->setMinimum(low);
    field_->setMaximum(high);
}

void SliderEdit::setStep(float step) {
    field_->setSingleStep(step);
    static_cast<DragSpinBox*>(field_)->setDragSpeed(step);
}

// ---------------------------------------------------------------------------
// CheckRow / ComboRow / TextRow
// ---------------------------------------------------------------------------

CheckRow::CheckRow(const QString& label, QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(metrics::spacing);
    box_ = new QCheckBox(this);
    layout->addWidget(box_);
    label_ = new QLabel(label, this);
    layout->addWidget(label_, 1);
    connect(box_, &QCheckBox::toggled, this, [this](bool checked) { Q_EMIT edited(checked); });
}

void CheckRow::set(bool value) {
    QSignalBlocker block(box_);
    box_->setChecked(value);
}

bool CheckRow::value() const {
    return box_->isChecked();
}

void CheckRow::setCheckbox(QCheckBox* box) {
    box_ = box;
}

ComboRow::ComboRow(const QString& label, QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(metrics::spacing);
    label_ = new QLabel(label, this);
    label_->setMinimumWidth(96);
    layout->addWidget(label_, 1);
    combo_ = new QComboBox(this);
    layout->addWidget(combo_, 2);
    connect(combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        Q_UNUSED(index);
        Q_EMIT edited(combo_->currentIndex());
    });
}

void ComboRow::setItems(const QStringList& items) {
    QSignalBlocker block(combo_);
    combo_->clear();
    combo_->addItems(items);
}

void ComboRow::setCurrent(int index) {
    QSignalBlocker block(combo_);
    combo_->setCurrentIndex(index);
}

int ComboRow::current() const {
    return combo_->currentIndex();
}

TextRow::TextRow(const QString& label, const QString& placeholder, QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(metrics::spacing);
    label_ = new QLabel(label, this);
    label_->setMinimumWidth(96);
    layout->addWidget(label_, 1);
    edit_ = new QLineEdit(this);
    edit_->setPlaceholderText(placeholder);
    layout->addWidget(edit_, 3);
    browse_ = new QToolButton(this);
    browse_->setText(QStringLiteral("..."));
    browse_->setVisible(false);
    layout->addWidget(browse_);
    connect(edit_, &QLineEdit::textEdited, this, [this](const QString& text) { Q_EMIT edited(text); });
    connect(browse_, &QToolButton::clicked, this, [this]() { Q_EMIT browseClicked(); });
}

void TextRow::set(const QString& text) {
    QSignalBlocker block(edit_);
    edit_->setText(text);
}

QString TextRow::text() const {
    return edit_->text();
}

void ColorEdit::setAlphaVisible(bool visible) {
    Q_UNUSED(visible);
}

void TextRow::setBrowse(bool visible) {
    browse_->setVisible(visible);
}

void TextRow::setReadOnly(bool readOnly) {
    edit_->setReadOnly(readOnly);
}

QLabel* sectionLabel(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    label->setContentsMargins(0, 6, 0, 2);
    return label;
}

void setEnabledRecursive(QWidget* widget, bool enabled) {
    if (!widget) {
        return;
    }
    widget->setEnabled(enabled);
    const auto children = widget->findChildren<QWidget*>();
    for (auto* child : children) {
        child->setEnabled(enabled);
    }
}

// ---------------------------------------------------------------------------
// Colour swatch
// ---------------------------------------------------------------------------

class SwatchButton : public QPushButton {
  public:
    explicit SwatchButton(QWidget* parent = nullptr) : QPushButton(parent) {
        setFixedHeight(20);
        setCursor(Qt::PointingHandCursor);
    }
    void setColour(const QColor& colour) {
        colour_ = colour;
        update();
    }
    QColor colour() const {
        return colour_;
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QRect box = rect().adjusted(1, 1, -1, -1);
        painter.fillRect(box, colour_);
        painter.setPen(QColor(0, 0, 0, 120));
        painter.drawRect(box);
    }

  private:
    QColor colour_{255, 255, 255};
};

ColorEdit::ColorEdit(const QString& label, QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(metrics::spacing);
    label_ = new QLabel(label, this);
    label_->setMinimumWidth(96);
    layout->addWidget(label_, 1);
    swatch_ = new SwatchButton(this);
    layout->addWidget(swatch_, 2);
    connect(swatch_, &QPushButton::clicked, this, [this]() {
        const QColor picked = QColorDialog::getColor(
            toColor(colour_), this, label_->text(), QColorDialog::ShowAlphaChannel);
        if (!picked.isValid()) {
            return;
        }
        colour_ = toVec3(picked);
        swatch_->setColour(picked);
        Q_EMIT edited(colour_);
    });
}

void ColorEdit::set(const Vec3& colour) {
    colour_ = colour;
    swatch_->setColour(toColor(colour, 1.0f));
}

Vec3 ColorEdit::value() const {
    return colour_;
}

// ---------------------------------------------------------------------------
// ImageThumb
// ---------------------------------------------------------------------------

ImageThumb::ImageThumb(QWidget* parent) : QLabel(parent) {
    setAlignment(Qt::AlignCenter);
    setMinimumSize(32, 32);
    setScaledContents(false);
}

void ImageThumb::setImage(const QImage& image) {
    if (image.isNull()) {
        setPlaceholder();
        return;
    }
    setPixmap(QPixmap::fromImage(image).scaled(size(),
                                               Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation));
}

void ImageThumb::setPlaceholder() {
    clear();
    setText(QStringLiteral("?"));
}

// ---------------------------------------------------------------------------
// Object naming and icons
// ---------------------------------------------------------------------------

QString objectLabel(World& world, Entity entity) {
    if (!world.alive(entity)) {
        return QStringLiteral("<missing>");
    }
    if (const auto* name = world.tryGet<ObjectName>(entity)) {
        if (!name->value.empty()) {
            return QString::fromStdString(name->value);
        }
    }
    return QStringLiteral("Object %1").arg(entity.index);
}

QString objectIcon(World& world, Entity entity) {
    if (!world.alive(entity)) {
        return QStringLiteral("x");
    }
    if (world.has<Camera>(entity)) {
        return QStringLiteral("cam");
    }
    if (world.has<DirectionalLight>(entity) || world.has<PointLight>(entity) ||
        world.has<SpotLight>(entity)) {
        return QStringLiteral("light");
    }
    if (world.has<AudioSource>(entity)) {
        return QStringLiteral("audio");
    }
    if (world.has<ParticleSystem>(entity)) {
        return QStringLiteral("fx");
    }
    if (world.has<Terrain>(entity)) {
        return QStringLiteral("terrain");
    }
    if (world.has<MeshRenderer>(entity)) {
        return QStringLiteral("mesh");
    }
    return QStringLiteral("obj");
}

const std::vector<CreateEntry>& createEntries(int* groupCount) {
    // IDs match createEditorObject() in editor/create_menu.hpp, which is the
    // single place that knows how each object type is assembled.
    static const std::vector<CreateEntry> entries{
        {"Empty GameObject", 0},
        {"Capsule", 1},
        {"Cone", 2},
        {"Cube", 3},
        {"Sphere", 4},
        {"Camera", 5},
        {"Audio Source", 6},
        {"Directional Light", 7},
        {"Point Light", 8},
        {"Spot Light", 9},
        {"Plane", 10},
        {"Skybox", 11},
        {"Character Controller", 12},
        {"FPS Camera", 13},
        {"Raycast", 14},
        {"Particle System", 15},
        {"Laser", 16},
        {"Light Shafts", 17},
        {"Terrain", 18},
    };
    if (groupCount) {
        *groupCount = static_cast<int>(entries.size());
    }
    return entries;
}

QString shortText(const QString& text, int characters) {
    if (text.size() <= characters) {
        return text;
    }
    return text.left(std::max(0, characters - 3)) + QStringLiteral("...");
}

QString durationText(float seconds) {
    if (!std::isfinite(seconds) || seconds < 0) {
        return QStringLiteral("0:00.0");
    }
    const int minutes = static_cast<int>(seconds) / 60;
    const float rest = seconds - minutes * 60;
    return QStringLiteral("%1:%2")
        .arg(minutes)
        .arg(rest, 4, 'f', 1, QLatin1Char('0'));
}

QString formatFloat(float value, const char* format) {
    if (!std::isfinite(value)) {
        return QStringLiteral("-");
    }
    return QString::asprintf(format, static_cast<double>(value));
}

QString projectDisplayName(const std::string& projectPath) {
    if (projectPath.empty()) {
        return QStringLiteral("Untitled project");
    }
    return QFileInfo(QString::fromStdString(projectPath)).fileName();
}

// ---------------------------------------------------------------------------
// ComponentSection
// ---------------------------------------------------------------------------

class SectionHeader : public QWidget {
  public:
    explicit SectionHeader(ComponentSection* owner) : QWidget(owner), owner_(owner) {
        setFixedHeight(24);
        setCursor(Qt::PointingHandCursor);
    }

  protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            owner_->setExpanded(!owner_->expanded());
            event->accept();
        }
    }

  private:
    ComponentSection* owner_;
};

ComponentSection::ComponentSection(const QString& title, QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 2, 0, 2);
    layout->setSpacing(2);
    header_ = new SectionHeader(this);
    auto* headerLayout = new QHBoxLayout(header_);
    headerLayout->setContentsMargins(4, 0, 4, 0);
    auto* label = new QLabel(title, header_);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    label->setObjectName(QStringLiteral("sectionTitle"));
    headerLayout->addWidget(label, 1);
    setTitle(title);
    layout->addWidget(header_);
    content_ = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(content_);
    contentLayout->setContentsMargins(14, 2, 4, 4);
    contentLayout->setSpacing(3);
    layout->addWidget(content_);
    header_->setStyleSheet(QStringLiteral("background: rgba(90,140,200,0.14); border-radius: 3px;"));
}

void ComponentSection::setTitle(const QString& title) {
    title_ = title;
    if (auto* label = header_->findChild<QLabel*>(QStringLiteral("sectionTitle"))) {
        label->setText(QStringLiteral("%1 %2").arg(content_->isVisible() ? QStringLiteral("v")
                                                                          : QStringLiteral(">"),
                                                    title));
    }
}

void ComponentSection::addRows(QWidget* content) {
    if (!content) {
        return;
    }
    if (auto* layout = qobject_cast<QVBoxLayout*>(content_->layout())) {
        layout->addWidget(content);
    }
}

void ComponentSection::setExpanded(bool expanded) {
    content_->setVisible(expanded);
    setTitle(title_);
}

bool ComponentSection::expanded() const {
    return content_->isVisible();
}

void ComponentSection::contextMenuEvent(QContextMenuEvent* event) {
    Q_EMIT contextMenuRequested(event->globalPos());
    event->accept();
}

#include "ui/qt/qt_widgets.moc"

} // namespace qt
} // namespace ui
