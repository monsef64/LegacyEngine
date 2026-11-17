#include "ui/qt/qt_panels.hpp"

#include "assets/assets.hpp"
#include "audio/audio_preview.hpp"
#include "editor/asset_browser.hpp"
#include "editor/editor_history.hpp"
#include "project/rendering_settings.hpp"
#include "terrain/terrain_editor.hpp"
#include "terrain/terrain_tools.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QPainter>
#include <QPainterPath>
#include <QTableWidget>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <stdexcept>

namespace ui {
namespace qt {

// ---------------------------------------------------------------------------
// InputSettingsDock
// ---------------------------------------------------------------------------

namespace {

QString bindingLabel(const InputState& input, const std::string& action, int slot) {
    // InputSettings::label() already knows mouse, gamepad and scancode names.
    return QString::fromStdString(InputSettings::label(input.binding(action, slot)));
}

} // namespace

InputSettingsDock::InputSettingsDock(QWidget* parent)
    : QDockWidget(tr("Input Settings"), parent) {
    setObjectName(QStringLiteral("InputSettingsDock"));
    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(metrics::panelPadding, 6, metrics::panelPadding, 6);
    layout->setSpacing(metrics::spacing);

    auto* hint = new QLabel(
        tr("Each action has three alternative bindings. Click a slot, then press a key, mouse "
           "button or gamepad button. Escape cancels; right-click a slot to clear it."),
        content);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #8fa3bb;"));
    layout->addWidget(hint);

    auto* addRow = new QHBoxLayout();
    newName_ = new QLineEdit(content);
    newName_->setPlaceholderText(tr("New button name"));
    addRow->addWidget(newName_, 1);
    auto* add = new QPushButton(tr("Add mapping"), content);
    addRow->addWidget(add);
    layout->addLayout(addRow);

    table_ = new QTableWidget(content);
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels(
        {tr("Action"), tr("Binding 1"), tr("Binding 2"), tr("Binding 3"), tr("")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < 4; ++column) {
        table_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    layout->addWidget(table_, 1);

    status_ = new QLabel(content);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    setWidget(content);

    connect(add, &QPushButton::clicked, this, [this]() {
        const QString name = newName_->text().trimmed();
        if (name.isEmpty() || !settings_ || !browser_) {
            return;
        }
        if (!input_) {
            return;
        }
        try {
            auto& input = *input_;
            if (input.buttons.count(name.toStdString())) {
                status_->setText(tr("A mapping with that name already exists"));
                return;
            }
            input.buttons[name.toStdString()] = 0;
            settings_->save(browser_->assets, input);
            newName_->clear();
            status_->clear();
            rebuild();
            Q_EMIT bindingsChanged();
        } catch (const std::exception& e) {
            status_->setText(QString::fromUtf8(e.what()));
        }
    });
    connect(newName_, &QLineEdit::returnPressed, this, [add]() { add->click(); });
}

void InputSettingsDock::bind(World& world0,
                             AssetBrowser& browser0,
                             InputSettings& settings0,
                             EditorHistory& history0) {
    world_ = &world0;
    browser_ = &browser0;
    settings_ = &settings0;
    history_ = &history0;
    rebuild();
}

void InputSettingsDock::closeEvent(QCloseEvent* event) {
    Q_EMIT closed();
    QDockWidget::closeEvent(event);
}

void InputSettingsDock::bindInput(InputState& input0) {
    input_ = &input0;
}

void InputSettingsDock::refresh() {
    rebuild();
}

void InputSettingsDock::rebuild() {
    if (!settings_ || !browser_ || !input_) {
        return;
    }
    capturing_ = false;
    settings_->waiting.clear();
    auto& input = *input_;
    table_->setRowCount(0);
    configLocked_ = true;
    for (const auto& [name, code] : input.buttons) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        auto* label = new QLineEdit(QString::fromStdString(name), table_);
        label->setPlaceholderText(tr("Action name"));
        const std::string mapping = name;
        connect(label, &QLineEdit::editingFinished, this, [this, mapping, label]() {
            const std::string renamed = label->text().trimmed().toStdString();
            if (renamed.empty() || renamed == mapping || !settings_ || !browser_) {
                return;
            }
            auto& state = *input_;
            if (state.buttons.count(renamed)) {
                status_->setText(tr("A mapping with that name already exists"));
                label->setText(QString::fromStdString(mapping));
                return;
            }
            try {
                const std::array<int, 2> alternates = state.alternatives[mapping];
                state.buttons[renamed] = state.buttons[mapping];
                state.alternatives[renamed] = alternates;
                state.buttons.erase(mapping);
                state.alternatives.erase(mapping);
                settings_->save(browser_->assets, state);
                status_->clear();
                rebuild();
                Q_EMIT bindingsChanged();
            } catch (const std::exception& e) {
                status_->setText(QString::fromUtf8(e.what()));
            }
        });
        table_->setCellWidget(row, 0, label);
        for (int slot = 0; slot < 3; ++slot) {
            auto* button = new QPushButton(bindingLabel(input, mapping, slot), table_);
            button->setContextMenuPolicy(Qt::CustomContextMenu);
            connect(button, &QPushButton::clicked, this, [this, mapping, slot]() {
                if (!settings_ || !browser_) {
                    return;
                }
                settings_->waiting = mapping;
                settings_->waitingSlot = slot;
                capturing_ = true;
                status_->setText(tr("Press a key, mouse or gamepad button for '%1', slot %2... "
                                    "(Escape cancels)")
                                     .arg(QString::fromStdString(mapping))
                                     .arg(slot + 1));
                setFocus(Qt::OtherFocusReason);
                grabKeyboard();
            });
            connect(button, &QWidget::customContextMenuRequested, this,
                    [this, mapping, slot, button](const QPoint& point) {
                        QMenu menu(this);
                        auto* clear = menu.addAction(tr("Clear binding"));
                        if (menu.exec(button->mapToGlobal(point)) == clear && settings_ &&
                            browser_) {
                            auto& state = *input_;
                            state.bind(mapping, slot, 0);
                            settings_->save(browser_->assets, state);
                            rebuild();
                            Q_EMIT bindingsChanged();
                        }
                    });
            table_->setCellWidget(row, 1 + slot, button);
        }
        auto* remove = new QPushButton(tr("Remove"), table_);
        connect(remove, &QPushButton::clicked, this, [this, mapping]() {
            if (!settings_ || !browser_) {
                return;
            }
            auto& state = *input_;
            state.buttons.erase(mapping);
            state.alternatives.erase(mapping);
            settings_->save(browser_->assets, state);
            rebuild();
            Q_EMIT bindingsChanged();
        });
        table_->setCellWidget(row, 4, remove);
    }
    configLocked_ = false;
}

void InputSettingsDock::keyPressEvent(QKeyEvent* event) {
    if (!capturing_ || !settings_) {
        QDockWidget::keyPressEvent(event);
        return;
    }
    if (event->key() == Qt::Key_Escape) {
        capturing_ = false;
        settings_->waiting.clear();
        releaseKeyboard();
        status_->clear();
        event->accept();
        return;
    }
    const int code = sdlCodeForQtKey(event);
    if (code <= 0) {
        event->ignore();
        return;
    }
    applyCapturedBinding(code);
}

void InputSettingsDock::applyCapturedBinding(int code) {
    if (!settings_ || !browser_ || settings_->waiting.empty()) {
        return;
    }
    auto& state = *input_;
    if (!InputState::validBinding(code)) {
        status_->setText(tr("That input cannot be bound"));
        return;
    }
    try {
        state.bind(settings_->waiting, settings_->waitingSlot, code);
        settings_->save(browser_->assets, state);
        settings_->waiting.clear();
        capturing_ = false;
        releaseKeyboard();
        status_->clear();
        rebuild();
        Q_EMIT bindingsChanged();
    } catch (const std::exception& e) {
        status_->setText(QString::fromUtf8(e.what()));
    }
}

void InputSettingsDock::mousePressEvent(QMouseEvent* event) {
    // Binding a mouse button is part of the same capture gesture as keys.
    if (capturing_ && settings_) {
        const int code = int(SDL_BUTTON_LEFT) + (int(event->button()) - int(Qt::LeftButton));
        if (code > 0) {
            applyCapturedBinding(code);
            event->accept();
            return;
        }
    }
    QDockWidget::mousePressEvent(event);
}

int sdlCodeForQtKey(QKeyEvent* event) {
    // Qt key codes and SDL scancodes differ. Map the printable range directly
    // and the navigation/function keys explicitly; anything else is refused
    // rather than silently bound to the wrong action.
    const int key = event->key();
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return SDL_SCANCODE_A + (key - Qt::Key_A);
    }
    switch (key) {
    case Qt::Key_Space:
        return SDL_SCANCODE_SPACE;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return SDL_SCANCODE_RETURN;
    case Qt::Key_Escape:
        return SDL_SCANCODE_ESCAPE;
    case Qt::Key_Tab:
        return SDL_SCANCODE_TAB;
    case Qt::Key_Backspace:
        return SDL_SCANCODE_BACKSPACE;
    case Qt::Key_Delete:
        return SDL_SCANCODE_DELETE;
    case Qt::Key_Left:
        return SDL_SCANCODE_LEFT;
    case Qt::Key_Right:
        return SDL_SCANCODE_RIGHT;
    case Qt::Key_Up:
        return SDL_SCANCODE_UP;
    case Qt::Key_Down:
        return SDL_SCANCODE_DOWN;
    case Qt::Key_Shift:
        return SDL_SCANCODE_LSHIFT;
    case Qt::Key_Control:
        return SDL_SCANCODE_LCTRL;
    case Qt::Key_Alt:
        return SDL_SCANCODE_LALT;
    default:
        break;
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
        return SDL_SCANCODE_F1 + (key - Qt::Key_F1);
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return SDL_SCANCODE_0 + (key - Qt::Key_0);
    }
    return -1;
}

// ---------------------------------------------------------------------------
// RenderingSettingsDock
// ---------------------------------------------------------------------------

RenderingSettingsDock::RenderingSettingsDock(QWidget* parent)
    : QDockWidget(tr("Rendering"), parent) {
    setObjectName(QStringLiteral("RenderingDock"));
    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(metrics::panelPadding, 6, metrics::panelPadding, 6);
    layout->setSpacing(metrics::spacing);

    vsync_ = new QCheckBox(tr("Vertical sync"), content);
    layout->addWidget(vsync_);

    auto* form = new QFormLayout();
    shadowMode_ = new QComboBox(content);
    shadowMode_->addItems({tr("Off"), tr("Hard"), tr("Soft")});
    form->addRow(tr("Shadow mode"), shadowMode_);
    filtering_ = new QComboBox(content);
    filtering_->addItems({tr("Hard edges"), tr("Filtered (PCF)")});
    form->addRow(tr("Shadow filtering"), filtering_);
    resolution_ = new QComboBox(content);
    for (int size : {256, 512, 1024, 2048, 4096}) {
        resolution_->addItem(QStringLiteral("%1 x %1").arg(size), size);
    }
    form->addRow(tr("Shadow resolution"), resolution_);
    layout->addLayout(form);

    driver_ = new QLabel(content);
    driver_->setWordWrap(true);
    layout->addWidget(driver_);
    error_ = new QLabel(content);
    error_->setWordWrap(true);
    error_->setStyleSheet(QStringLiteral("color: #e07b7b;"));
    layout->addWidget(error_);
    layout->addStretch(1);
    setWidget(content);

    auto changed = [this]() { apply(); };
    connect(vsync_, &QCheckBox::toggled, this, changed);
    connect(shadowMode_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, changed);
    connect(filtering_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, changed);
    connect(resolution_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, changed);
}

void RenderingSettingsDock::bind(World& world0,
                                 AssetBrowser& browser0,
                                 EditorSettings& editorSettings0,
                                 EditorHistory& history0,
                                 RenderingSettings& settings0) {
    world_ = &world0;
    browser_ = &browser0;
    editorSettings_ = &editorSettings0;
    history_ = &history0;
    settings_ = &settings0;
    refresh();
}

void RenderingSettingsDock::refresh() {
    if (!browser_) {
        return;
    }
    if (!settings_) {
        return;
    }
    settings_->load(browser_->assets);
    loading_ = true;
    const QSignalBlocker a(vsync_);
    const QSignalBlocker b(shadowMode_);
    const QSignalBlocker c(filtering_);
    const QSignalBlocker d(resolution_);
    vsync_->setChecked(settings_->vsync);
    shadowMode_->setCurrentIndex(std::clamp(settings_->shadows.mode, 0, 2));
    filtering_->setCurrentIndex(settings_->shadows.soft ? 1 : 0);
    const int index = resolution_->findData(settings_->shadows.resolution);
    resolution_->setCurrentIndex(index >= 0 ? index : 2);
    const char* video = SDL_GetCurrentVideoDriver();
    const char* audio = SDL_GetCurrentAudioDriver();
    driver_->setText(QStringLiteral("Video: %1\nAudio: %2")
                         .arg(QString::fromUtf8(video ? video : "unknown"),
                              QString::fromUtf8(audio ? audio : "none")));
    error_->setText(QString::fromStdString(settings_->error));
    loading_ = false;
}

void RenderingSettingsDock::apply() {
    if (loading_ || !browser_ || !settings_) {
        return;
    }
    settings_->vsync = vsync_->isChecked();
    settings_->shadows.mode = shadowMode_->currentIndex();
    settings_->shadows.soft = filtering_->currentIndex() == 1;
    settings_->shadows.resolution = resolution_->currentData().toInt();
    if (settings_->shadows.resolution <= 0) {
        settings_->shadows.resolution = 1024;
    }
    settings_->apply();
    try {
        settings_->save(browser_->assets);
        error_->clear();
    } catch (const std::exception& e) {
        settings_->error = e.what();
        error_->setText(QString::fromUtf8(e.what()));
    }
    Q_EMIT changed();
}

void RenderingSettingsDock::closeEvent(QCloseEvent* event) {
    Q_EMIT closed();
    QDockWidget::closeEvent(event);
}

// ---------------------------------------------------------------------------
// ProfilerDock
// ---------------------------------------------------------------------------

ProfilerDock::ProfilerDock(QWidget* parent) : QDockWidget(tr("Profiler"), parent) {
    setObjectName(QStringLiteral("ProfilerDock"));
    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(metrics::panelPadding, 6, metrics::panelPadding, 6);
    layout->setSpacing(metrics::spacing);
    stats_ = new QLabel(content);
    stats_->setWordWrap(true);
    layout->addWidget(stats_);
    chart_ = new FrameChart(content);
    chart_->setMinimumHeight(70);
    layout->addWidget(chart_);
    auto* table = new QTableWidget(content);
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({tr("Subsystem"), tr("Last ms"), tr("Avg ms")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setObjectName(QStringLiteral("ProfilerParts"));
    layout->addWidget(table, 1);
    setWidget(content);
    chart_->setObjectName(QStringLiteral("ProfilerChart"));
}

FrameChart::FrameChart(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(60);
    samples_.assign(120, 0.0);
}

void FrameChart::setFrameMs(double last, double average, float ceiling) {
    samples_.erase(samples_.begin());
    samples_.push_back(last);
    last_ = last;
    average_ = average;
    ceiling_ = std::max(5.0f, ceiling);
    update();
}

void FrameChart::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(24, 27, 32));
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(60, 68, 80), 1));
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
    if (samples_.empty()) {
        return;
    }
    const int count = int(samples_.size());
    const double scale = height() / double(std::max(1.0f, ceiling_));
    QPainterPath path;
    for (int i = 0; i < count; ++i) {
        const double x = width() * i / double(std::max(1, count - 1));
        const double y = height() - std::min(double(height()), samples_[std::size_t(i)] * scale);
        if (i == 0) {
            path.moveTo(x, y);
        } else {
            path.lineTo(x, y);
        }
    }
    painter.setPen(QPen(QColor(89, 166, 255), 1.5));
    painter.drawPath(path);
    if (average_ > 0) {
        const double y = height() - std::min(double(height()), average_ * scale);
        painter.setPen(QPen(QColor(255, 190, 90, 180), 1, Qt::DashLine));
        painter.drawLine(QPointF(0, y), QPointF(width(), y));
    }
}

void ProfilerDock::closeEvent(QCloseEvent* event) {
    Q_EMIT closed();
    QDockWidget::closeEvent(event);
}

void ProfilerDock::setData(const FrameProfiler& profiler) {
    const auto frame = profiler.stats();
    stats_->setText(tr("Frame: %1 ms | Average: %2 ms (%3 FPS) | Peak: %4 ms\n%5 / %6 samples")
                        .arg(frame.last, 0, 'f', 2)
                        .arg(frame.average, 0, 'f', 2)
                        .arg(frame.average > 0 ? 1000.0 / frame.average : 0.0, 0, 'f', 0)
                        .arg(frame.peak, 0, 'f', 2)
                        .arg(profiler.count)
                        .arg(profiler.capacity));
    chart_->setFrameMs(frame.last, frame.average, float(std::max(20.0, double(frame.peak))));
    auto* table = findChild<QTableWidget*>(QStringLiteral("ProfilerParts"));
    if (!table) {
        return;
    }
    table->setRowCount(int(FrameProfiler::parts));
    for (std::size_t i = 0; i < FrameProfiler::parts; ++i) {
        const auto stats = profiler.stats(int(i));
        table->setItem(int(i), 0, new QTableWidgetItem(QString::fromUtf8(profileNames[i])));
        table->setItem(int(i), 1, new QTableWidgetItem(QString::number(stats.last, 'f', 3)));
        table->setItem(int(i), 2, new QTableWidgetItem(QString::number(stats.average, 'f', 3)));
    }
}

} // namespace qt
} // namespace ui
