#include "ui/qt/qt_panels.hpp"

#include "assets/assets.hpp"
#include "assets/cubemap.hpp"
#include "materials/material_io.hpp"
#include "ui/qt/qt_widgets.hpp"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace ui {
namespace qt {

CubemapDialog::CubemapDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("CubemapDialog"));
    setWindowTitle(tr("Cubemap Editor"));
    setMinimumWidth(520);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(metrics::spacing);

    auto* path = new QLabel(this);
    path->setObjectName(QStringLiteral("CubemapPath"));
    path->setWordWrap(true);
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(path);

    auto* hint = new QLabel(
        tr("Assign six square textures of matching size. Front is -Z, back is +Z. Empty faces "
           "are cleared, not projected."),
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #8fa3bb;"));
    layout->addWidget(hint);

    for (int i = 0; i < 6; ++i) {
        auto* row = new TextRow(QString::fromUtf8(CubemapAsset::names[i]), tr("No texture"), this);
        row->setBrowse(true);
        faces_[std::size_t(i)] = row;
        layout->addWidget(row);
    }

    error_ = new QLabel(this);
    error_->setWordWrap(true);
    error_->setStyleSheet(QStringLiteral("color: #e07b7b;"));
    layout->addWidget(error_);

    auto* buttons = new QDialogButtonBox(this);
    auto* saveButton = buttons->addButton(tr("Save Cubemap"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Close);
    layout->addWidget(buttons);
    connect(saveButton, &QPushButton::clicked, this, &CubemapDialog::save);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
}

void CubemapDialog::openPath(AssetManager& assets0, const QString& path) {
    assets_ = &assets0;
    path_ = path;
    if (auto* label = findChild<QLabel*>(QStringLiteral("CubemapPath"))) {
        label->setText(path);
    }
    try {
        const auto draft = loadCubemap(assets0, path.toStdString());
        for (int i = 0; i < 6; ++i) {
            faces_[std::size_t(i)]->set(QString::fromStdString(draft.faces[std::size_t(i)]));
        }
        error_->clear();
    } catch (const std::exception& e) {
        error_->setText(QString::fromUtf8(e.what()));
    }
    show();
    raise();
}

void CubemapDialog::save() {
    if (!assets_ || path_.isEmpty()) {
        return;
    }
    CubemapAsset draft;
    for (int i = 0; i < 6; ++i) {
        draft.faces[std::size_t(i)] = faces_[std::size_t(i)]->text().toStdString();
        if (!draft.faces[std::size_t(i)].empty() &&
            !materialTexturePath(draft.faces[std::size_t(i)])) {
            error_->setText(
                tr("%1 is not a project-relative texture path.")
                    .arg(QString::fromStdString(draft.faces[std::size_t(i)])));
            return;
        }
    }
    try {
        saveCubemap(*assets_, path_.toStdString(), draft);
        error_->clear();
        Q_EMIT saved(path_);
    } catch (const std::exception& e) {
        error_->setText(QString::fromUtf8(e.what()));
    }
}

} // namespace qt
} // namespace ui
