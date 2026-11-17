#include "ui/qt/qt_panels.hpp"

#include "editor/transform_edit.hpp"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QStyle>
#include <QToolButton>

namespace ui {
namespace qt {

namespace {

QToolButton* makeToolButton(QWidget* parent, const QString& text, const QString& tooltip) {
    auto* button = new QToolButton(parent);
    button->setText(text);
    button->setToolTip(tooltip);
    button->setFocusPolicy(Qt::NoFocus);
    button->setAutoRaise(true);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    return button;
}

} // namespace

Toolbar::Toolbar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("EditorToolbar"));
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(metrics::panelPadding, 3, metrics::panelPadding, 3);
    layout->setSpacing(metrics::spacing);

    createButton_ = makeToolButton(this, tr("Create"), tr("Create a new object"));
    createButton_->setPopupMode(QToolButton::InstantPopup);
    auto* createMenu = new QMenu(createButton_);
    const auto& entries = createEntries();
    for (const auto& entry : entries) {
        auto* action = createMenu->addAction(QString::fromUtf8(entry.label));
        const int id = entry.id;
        connect(action, &QAction::triggered, this, [this, id]() { Q_EMIT createRequested(id); });
    }
    createButton_->setMenu(createMenu);
    layout->addWidget(createButton_);

    layout->addSpacing(8);
    moveButton_ = makeToolButton(this, tr("Move"), tr("Move tool (W)"));
    rotateButton_ = makeToolButton(this, tr("Rotate"), tr("Rotate tool (E)"));
    scaleButton_ = makeToolButton(this, tr("Scale"), tr("Scale tool (R)"));
    for (auto* button : {moveButton_, rotateButton_, scaleButton_}) {
        button->setCheckable(true);
        layout->addWidget(button);
    }
    connect(moveButton_, &QToolButton::clicked, this, [this]() {
        setTool(TransformTool::Move);
        Q_EMIT toolChanged(TransformTool::Move);
    });
    connect(rotateButton_, &QToolButton::clicked, this, [this]() {
        setTool(TransformTool::Rotate);
        Q_EMIT toolChanged(TransformTool::Rotate);
    });
    connect(scaleButton_, &QToolButton::clicked, this, [this]() {
        setTool(TransformTool::Scale);
        Q_EMIT toolChanged(TransformTool::Scale);
    });

    layout->addSpacing(8);
    worldButton_ = makeToolButton(this, tr("World"), tr("Transform along world axes"));
    localButton_ = makeToolButton(this, tr("Local"), tr("Transform along the object's axes"));
    worldButton_->setCheckable(true);
    localButton_->setCheckable(true);
    worldButton_->setChecked(true);
    layout->addWidget(worldButton_);
    layout->addWidget(localButton_);
    connect(worldButton_, &QToolButton::clicked, this, [this]() {
        setLocalSpace(false);
        Q_EMIT localSpaceChanged(false);
    });
    connect(localButton_, &QToolButton::clicked, this, [this]() {
        setLocalSpace(true);
        Q_EMIT localSpaceChanged(true);
    });

    layout->addSpacing(8);
    pivot_ = new QComboBox(this);
    pivot_->addItem(tr("Origin pivot"));
    pivot_->addItem(tr("Centre pivot"));
    pivot_->setToolTip(tr("Pivot used by the gizmo and by focus-on-selection"));
    layout->addWidget(pivot_);
    connect(pivot_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        const bool center = index == 1;
        if (centerPivot() == center) {
            return;
        }
        Q_EMIT centerPivotChanged(center);
    });

    layout->addWidget(new QLabel(tr("Snap"), this));
    snap_ = new QDoubleSpinBox(this);
    snap_->setRange(0.25, 10000.0);
    snap_->setDecimals(2);
    snap_->setSingleStep(0.25);
    snap_->setValue(0.5);
    snap_->setToolTip(tr("Snap step, applied while Ctrl is held"));
    snap_->setKeyboardTracking(false);
    layout->addWidget(snap_);
    connect(snap_,
            QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this,
            [this](double value) { Q_EMIT snapChanged(float(value)); });

    layout->addStretch(1);

    playButton_ = makeToolButton(this, tr("Play"), tr("Play (F5)"));
    pauseButton_ = makeToolButton(this, tr("Pause"), tr("Pause (F6)"));
    stopButton_ = makeToolButton(this, tr("Stop"), tr("Stop (F7)"));
    playButton_->setCheckable(true);
    pauseButton_->setCheckable(true);
    layout->addWidget(playButton_);
    layout->addWidget(pauseButton_);
    layout->addWidget(stopButton_);
    connect(playButton_, &QToolButton::clicked, this, [this]() { Q_EMIT playRequested(); });
    connect(pauseButton_, &QToolButton::clicked, this, [this]() { Q_EMIT pauseRequested(); });
    connect(stopButton_, &QToolButton::clicked, this, [this]() { Q_EMIT stopRequested(); });

    layout->addSpacing(8);
    separateGame_ = new QCheckBox(tr("Separate game window"), this);
    separateGame_->setToolTip(tr("Render the game into its own SDL window"));
    layout->addWidget(separateGame_);
    connect(separateGame_, &QCheckBox::toggled, this, [this](bool checked) {
        Q_EMIT separateGameToggled(checked);
    });
    fullscreenGame_ = makeToolButton(this, tr("Game fullscreen"), tr("Fullscreen game window (F11)"));
    layout->addWidget(fullscreenGame_);
    connect(fullscreenGame_, &QToolButton::clicked, this, [this]() { Q_EMIT fullscreenGameRequested(); });

    setTool(TransformTool::Move);
    setPlayState(false, false);
}

void Toolbar::setPlayState(bool playing, bool paused) {
    const QSignalBlocker playBlock(playButton_);
    const QSignalBlocker pauseBlock(pauseButton_);
    playButton_->setChecked(playing);
    pauseButton_->setChecked(paused);
    pauseButton_->setEnabled(playing || paused);
    stopButton_->setEnabled(playing || paused);
    separateGame_->setEnabled(playing || paused);
    fullscreenGame_->setEnabled(true);
}

void Toolbar::setPlayEnabled(bool enabled) {
    playButton_->setEnabled(enabled);
}

void Toolbar::setSeparateGame(bool enabled, bool active) {
    const QSignalBlocker block(separateGame_);
    separateGame_->setChecked(enabled);
    separateGame_->setEnabled(active || enabled);
}

TransformTool Toolbar::tool() const {
    if (rotateButton_->isChecked()) {
        return TransformTool::Rotate;
    }
    if (scaleButton_->isChecked()) {
        return TransformTool::Scale;
    }
    return TransformTool::Move;
}

bool Toolbar::localSpace() const {
    return localButton_->isChecked();
}

bool Toolbar::centerPivot() const {
    return pivot_->currentIndex() == 1;
}

float Toolbar::snapStep() const {
    return float(snap_->value());
}

bool Toolbar::separateGame() const {
    return separateGame_->isChecked();
}

void Toolbar::setTool(TransformTool tool) {
    moveButton_->setChecked(tool == TransformTool::Move);
    rotateButton_->setChecked(tool == TransformTool::Rotate);
    scaleButton_->setChecked(tool == TransformTool::Scale);
}

void Toolbar::setLocalSpace(bool local) {
    worldButton_->setChecked(!local);
    localButton_->setChecked(local);
}

void Toolbar::setCenterPivot(bool center) {
    const QSignalBlocker block(pivot_);
    pivot_->setCurrentIndex(center ? 1 : 0);
}

void Toolbar::fullscreenGame() {
    Q_EMIT fullscreenGameRequested();
}

void Toolbar::setSnapStep(float step) {
    const QSignalBlocker block(snap_);
    snap_->setValue(double(step));
}

} // namespace qt
} // namespace ui
