#include "ui/qt/qt_panels.hpp"

#include "assets/assets.hpp"
#include "editor/asset_browser.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace ui {
namespace qt {

namespace {
const char* const kThemeNames[]{"Legacy Dark", "Grey", "Unity 5 Classic", "Unity Dark Classic"};
}

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("SettingsDialog"));
    setWindowTitle(tr("Editor Settings"));
    setMinimumWidth(460);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(metrics::spacing);

    auto* appearance = new QGroupBox(tr("Appearance"), this);
    auto* appearanceForm = new QFormLayout(appearance);
    theme_ = new QComboBox(appearance);
    for (const char* name : kThemeNames) {
        theme_->addItem(QString::fromUtf8(name));
    }
    appearanceForm->addRow(tr("Theme"), theme_);
    colliders_ = new QCheckBox(tr("Always show colliders"), appearance);
    appearanceForm->addRow(QString(), colliders_);
    raycast_ = new FloatEdit(tr("Raycast line thickness"), appearance);
    raycast_->setRange(1.0f, 8.0f);
    raycast_->setStep(0.1f);
    raycast_->setSuffix(tr(" px"));
    appearanceForm->addRow(raycast_);
    layout->addWidget(appearance);

    auto* performance = new QGroupBox(tr("Performance"), this);
    auto* performanceForm = new QFormLayout(performance);
    fps_ = new IntEdit(tr("FPS limit (VSync off)"), performance);
    fps_->setRange(0, 240);
    performanceForm->addRow(fps_);
    auto* fpsHint =
        new QLabel(tr("VSync takes priority. Limit 0 = uncapped; background target = 30 FPS."),
                   performance);
    fpsHint->setWordWrap(true);
    fpsHint->setStyleSheet(QStringLiteral("color: #8fa3bb;"));
    performanceForm->addRow(fpsHint);
    autosave_ = new IntEdit(tr("Autosave delay"), performance);
    autosave_->setRange(0, 3600);
    performanceForm->addRow(autosave_);
    auto* autosaveHint =
        new QLabel(tr("Seconds. 0 disables autosave; only saved projects are autosaved."),
                   performance);
    autosaveHint->setWordWrap(true);
    autosaveHint->setStyleSheet(QStringLiteral("color: #8fa3bb;"));
    performanceForm->addRow(autosaveHint);
    layout->addWidget(performance);

    auto* gridBox = new QGroupBox(tr("Scene Grid"), this);
    auto* gridForm = new QFormLayout(gridBox);
    gridVisible_ = new QCheckBox(tr("Show grid"), gridBox);
    gridForm->addRow(QString(), gridVisible_);
    gridPlane_ = new QComboBox(gridBox);
    gridPlane_->addItems({tr("XZ"), tr("XY"), tr("YZ")});
    gridForm->addRow(tr("Plane"), gridPlane_);
    gridCells_ = new IntEdit(tr("Cells"), gridBox);
    gridCells_->setRange(2, 512);
    gridForm->addRow(gridCells_);
    gridSpacing_ = new FloatEdit(tr("Spacing"), gridBox);
    gridSpacing_->setRange(0.01f, 1000.0f);
    gridSpacing_->setStep(0.05f);
    gridForm->addRow(gridSpacing_);
    gridPosition_ = new Vector3Edit(gridBox);
    gridForm->addRow(tr("Position"), gridPosition_);
    gridColour_ = new ColorEdit(tr("Colour"), gridBox);
    gridForm->addRow(gridColour_);
    layout->addWidget(gridBox);

    auto* editorBox = new QGroupBox(tr("Editor"), this);
    auto* editorForm = new QFormLayout(editorBox);
    codeEditor_ = new QComboBox(editorBox);
    codeEditor_->addItems({tr("Internal"), tr("External (VS Code)")});
    editorForm->addRow(tr("Code editor"), codeEditor_);
    auto* externalHint = new QLabel(
        tr("External mode opens the selected script in VS Code. Its 'code' command must be in "
           "PATH."),
        editorBox);
    externalHint->setWordWrap(true);
    externalHint->setStyleSheet(QStringLiteral("color: #8fa3bb;"));
    editorForm->addRow(externalHint);
    layout->addWidget(editorBox);

    error_ = new QLabel(this);
    error_->setWordWrap(true);
    error_->setStyleSheet(QStringLiteral("color: #e07b7b;"));
    layout->addWidget(error_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);

    auto save = [this]() {
        if (loading_ || !settings_) {
            return;
        }
        settings_->raycastThickness = raycast_->value();
        settings_->maxFps = fps_->value();
        settings_->autosaveSeconds = autosave_->value();
        settings_->alwaysShowColliders = colliders_->isChecked();
        settings_->external = codeEditor_->currentIndex() == 1;
        GridSettings grid = settings_->grid;
        grid.visible = gridVisible_->isChecked();
        grid.plane = gridPlane_->currentIndex();
        grid.cells = gridCells_->value();
        grid.spacing = gridSpacing_->value();
        grid.position = gridPosition_->value();
        grid.color = gridColour_->value();
        if (grid.valid()) {
            settings_->grid = grid;
        } else {
            error_->setText(tr("Grid values out of range; grid settings were not saved."));
        }
        settings_->save();
        Q_EMIT settingsChanged();
    };
    connect(raycast_, &FloatEdit::edited, this, [save](float) { save(); });
    connect(fps_, &IntEdit::edited, this, [save](int) { save(); });
    connect(autosave_, &IntEdit::edited, this, [save](int) { save(); });
    connect(colliders_, &QCheckBox::toggled, this, [save](bool) { save(); });
    connect(gridVisible_, &QCheckBox::toggled, this, [save](bool) { save(); });
    connect(gridPlane_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [save](int) { save(); });
    connect(gridCells_, &IntEdit::edited, this, [save](int) { save(); });
    connect(gridSpacing_, &FloatEdit::edited, this, [save](float) { save(); });
    connect(gridPosition_, &Vector3Edit::edited, this, [save](const Vec3&) { save(); });
    connect(gridColour_, &ColorEdit::edited, this, [save](const Vec3&) { save(); });
    connect(codeEditor_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this, save](int index) {
                save();
                if (index == 1 && settings_) {
                    // Switching to External opens the project straight away, as
                    // the immediate-mode dialog did.
                    settings_->openExternal(folder_);
                    error_->setText(QString::fromStdString(settings_->error));
                }
            });
    connect(theme_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (loading_ || !settings_) {
            return;
        }
        settings_->theme = std::clamp(index, 0, 3);
        settings_->save();
        Q_EMIT themeSelected(settings_->theme);
    });
    connect(this, &SettingsDialog::themeSelected, this, [this](int theme) {
        const QSignalBlocker block(theme_);
        theme_->setCurrentIndex(theme);
    });
}

void SettingsDialog::bind(EditorSettings& settings0,
                          const std::filesystem::path& projectFolder,
                          AssetBrowser& browser0) {
    settings_ = &settings0;
    browser_ = &browser0;
    folder_ = projectFolder;
    refresh();
}

void SettingsDialog::setThemeRequested(int theme) {
    if (!settings_ || loading_) {
        return;
    }
    settings_->theme = std::clamp(theme, 0, 3);
    settings_->save();
}

void SettingsDialog::refresh() {
    if (!settings_) {
        return;
    }
    loading_ = true;
    theme_->setCurrentIndex(std::clamp(settings_->theme, 0, 3));
    colliders_->setChecked(settings_->alwaysShowColliders);
    raycast_->set(settings_->raycastThickness);
    fps_->set(settings_->maxFps);
    autosave_->set(settings_->autosaveSeconds);
    gridVisible_->setChecked(settings_->grid.visible);
    gridPlane_->setCurrentIndex(std::clamp(settings_->grid.plane, 0, 2));
    gridCells_->set(settings_->grid.cells);
    gridSpacing_->set(settings_->grid.spacing);
    gridPosition_->set(settings_->grid.position);
    gridColour_->set(settings_->grid.color);
    codeEditor_->setCurrentIndex(settings_->external ? 1 : 0);
    error_->setText(QString::fromStdString(settings_->error));
    loading_ = false;
}

} // namespace qt
} // namespace ui
