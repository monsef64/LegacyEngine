#include "ui/qt/qt_panels.hpp"

#include "assets/assets.hpp"
#include "editor/asset_browser.hpp"
#include "editor/editor_history.hpp"
#include "editor/lightmapping_window.hpp"
#include "ui/qt/qt_widgets.hpp"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace ui {
namespace qt {

LightingDock::LightingDock(QWidget* parent) : QDockWidget(tr("Lightmapping"), parent) {
    setObjectName(QStringLiteral("LightingDock"));
    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    content_ = new QWidget(scroll_);
    scroll_->setWidget(content_);
    setWidget(scroll_);
    setMinimumWidth(320);

    auto* root = new QVBoxLayout(content_);
    root->setContentsMargins(metrics::panelPadding, 6, metrics::panelPadding, 8);
    root->setSpacing(metrics::spacing);

    auto* bakeRow = new QHBoxLayout();
    auto* bake = new QPushButton(tr("Bake selected / level"), content_);
    auto* cancel = new QPushButton(tr("Cancel"), content_);
    bakeRow->addWidget(bake);
    bakeRow->addWidget(cancel);
    root->addLayout(bakeRow);

    auto* qualityRow = new QHBoxLayout();
    qualityRow->addWidget(new QLabel(tr("Quality"), content_));
    quality_ = new QComboBox(content_);
    quality_->addItems({tr("Low"), tr("Medium"), tr("High")});
    qualityRow->addWidget(quality_, 1);
    root->addLayout(qualityRow);

    auto* clear = new QPushButton(tr("Clear assignments on all objects"), content_);
    root->addWidget(clear);

    progress_ = new QProgressBar(content_);
    progress_->setRange(0, 100);
    progress_->setTextVisible(true);
    root->addWidget(progress_);

    status_ = new QLabel(content_);
    status_->setWordWrap(true);
    root->addWidget(status_);

    auto* objectBox = new QGroupBox(tr("Objects"), content_);
    auto* objectLayout = new QVBoxLayout(objectBox);
    objects_ = new QTreeWidget(objectBox);
    objects_->setHeaderLabels({tr("Object"), tr("Baked")});
    objects_->setRootIsDecorated(false);
    objects_->setAlternatingRowColors(true);
    objects_->header()->setStretchLastSection(false);
    objects_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    objectLayout->addWidget(objects_);
    root->addWidget(objectBox, 1);

    auto* hint = new QLabel(
        tr("Bake writes a lightmap atlas and assigns it to static meshes and terrains. "
           "Quality trades texel size for bake time; bakes run in the background and can be "
           "cancelled without touching the current assignments."),
        content_);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #8fa3bb;"));
    root->addWidget(hint);

    connect(bake, &QPushButton::clicked, this, [this]() {
        if (!window_ || !world_ || !browser_) {
            return;
        }
        window_->bake(*world_, browser_->assets, *meshes_, level_.string());
        builtRevision_ = -1;
        refresh();
        Q_EMIT changed();
    });
    connect(cancel, &QPushButton::clicked, this, [this]() {
        if (window_) {
            window_->cancelBake();
            refresh();
        }
    });
    connect(clear, &QPushButton::clicked, this, [this]() {
        if (window_ && world_ && history_) {
            window_->clearAssignments(*world_, selected_, *history_);
            refresh();
            Q_EMIT changed();
        }
    });
    connect(quality_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) {
                if (!loading_ && window_) {
                    window_->setQuality(index);
                }
            });
}

void LightingDock::bind(World& world0,
                        AssetBrowser& browser0,
                        EditorHistory& history0,
                        LightmappingWindow& window0,
                        const ProjectIO::Meshes& meshes0,
                        const std::filesystem::path& level0) {
    world_ = &world0;
    browser_ = &browser0;
    history_ = &history0;
    window_ = &window0;
    meshes_ = &meshes0;
    level_ = level0;
    builtRevision_ = -1;
    refresh();
}

void LightingDock::setLevel(const std::filesystem::path& level0) {
    level_ = level0;
    builtRevision_ = -1;
}

void LightingDock::setSelection(Entity entity) {
    selected_ = entity;
}

void LightingDock::setEditable(bool editable) {
    editable_ = editable;
}

void LightingDock::refresh() {
    if (!window_ || !world_ || !browser_) {
        return;
    }
    const auto snapshot =
        window_->snapshot(*world_, browser_->assets, *meshes_, level_.string());
    loading_ = true;
    quality_->setCurrentIndex(window_->quality());
    loading_ = false;
    if (snapshot.busy) {
        progress_->setValue(int(snapshot.progress * 100.0f));
        progress_->setFormat(tr("%1% | %2 / %3 texels")
                                 .arg(QString::number(snapshot.progress * 100.0f, 'f', 0))
                                 .arg(snapshot.texelsDone)
                                 .arg(snapshot.texelsTotal));
        progress_->setVisible(true);
    } else if (snapshot.texelsTotal) {
        progress_->setValue(100);
        progress_->setFormat(tr("Done"));
    } else if (progress_->value() > 0) {
        progress_->setValue(0);
        progress_->setFormat(tr("Idle"));
    }
    QString status = QString::fromStdString(snapshot.status);
    status += tr("\n%1: %2 objects, %3 baked, %4 assigned, %5 static lights, %6 eligible")
                  .arg(QString::fromStdString(snapshot.level))
                  .arg(snapshot.totalObjects)
                  .arg(snapshot.baked)
                  .arg(snapshot.assigned)
                  .arg(int(snapshot.staticLights))
                  .arg(int(snapshot.eligibleObjects));
    if (snapshot.busy) {
        status += tr("\nBaking (phase %1, %2 s elapsed)")
                      .arg(snapshot.phase)
                      .arg(snapshot.elapsed, 0, 'f', 1);
    }
    status_->setText(status);

    if (int(snapshot.objects.size()) != builtRevision_) {
        builtRevision_ = int(snapshot.objects.size());
        objects_->clear();
        for (const auto& [name, baked] : snapshot.objects) {
            auto* item = new QTreeWidgetItem(objects_);
            item->setText(0, QString::fromStdString(name));
            item->setText(1, baked ? tr("Yes") : tr("No"));
            item->setForeground(1, baked ? QColor(120, 210, 140) : QColor(150, 164, 184));
        }
    }
}

void LightingDock::closeEvent(QCloseEvent* event) {
    Q_EMIT closed();
    QDockWidget::closeEvent(event);
}

} // namespace qt
} // namespace ui
