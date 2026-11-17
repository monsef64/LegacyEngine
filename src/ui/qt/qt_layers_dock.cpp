#include "ui/qt/qt_panels.hpp"

#include "assets/assets.hpp"
#include "editor/asset_browser.hpp"
#include "project/layers.hpp"
#include "ui/qt/qt_widgets.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

namespace ui {
namespace qt {

LayersDock::LayersDock(QWidget* parent) : QDockWidget(tr("Layers"), parent) {
    setObjectName(QStringLiteral("LayersDock"));
    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(metrics::panelPadding, 6, metrics::panelPadding, 8);
    layout->setSpacing(metrics::spacing);

    auto* builtIn = new QLabel(tr("Default (built-in)"), content);
    builtIn->setStyleSheet(QStringLiteral("color: #8fa3bb;"));
    layout->addWidget(builtIn);

    list_ = new QListWidget(content);
    list_->setAlternatingRowColors(true);
    layout->addWidget(list_, 1);

    auto* row = new QHBoxLayout();
    name_ = new QLineEdit(content);
    name_->setPlaceholderText(tr("New layer name"));
    name_->setMaxLength(64);
    row->addWidget(name_, 1);
    auto* add = new QPushButton(tr("Add"), content);
    row->addWidget(add);
    layout->addLayout(row);

    error_ = new QLabel(content);
    error_->setWordWrap(true);
    error_->setStyleSheet(QStringLiteral("color: #e07b7b;"));
    layout->addWidget(error_);

    auto* hint = new QLabel(
        tr("Layers name groups of objects for raycasts and rendering. Removing a layer moves "
           "its objects back to Default and clears the matching raycast mask bits."),
        content);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #8fa3bb;"));
    layout->addWidget(hint);

    auto* remove = new QPushButton(tr("Remove selected layer"), content);
    layout->addWidget(remove);
    setWidget(content);
    setMinimumWidth(300);

    connect(add, &QPushButton::clicked, this, [this]() {
        const QString text = name_->text().trimmed();
        if (text.isEmpty() || !browser_) {
            return;
        }
        if (LayerSettings::find(text.toStdString()) >= 0) {
            error_->setText(tr("Layer already exists"));
            return;
        }
        int slot = 1;
        while (slot < 32 && !LayerSettings::names[std::size_t(slot)].empty()) {
            ++slot;
        }
        if (slot == 32) {
            error_->setText(tr("Maximum 32 layers"));
            return;
        }
        LayerSettings::names[std::size_t(slot)] = text.toStdString();
        LayerSettings settings;
        settings.save(browser_->assets);
        name_->clear();
        error_->clear();
        refresh();
        Q_EMIT changed();
    });
    connect(name_, &QLineEdit::returnPressed, add, &QPushButton::click);
    connect(remove, &QPushButton::clicked, this, [this]() {
        auto* item = list_->currentItem();
        if (!item || !browser_) {
            return;
        }
        const int slot = item->data(Qt::UserRole).toInt();
        if (slot <= 0 || slot >= 32) {
            return;
        }
        LayerSettings::names[std::size_t(slot)].clear();
        LayerSettings settings;
        settings.save(browser_->assets);
        refresh();
        Q_EMIT changed();
    });
}

void LayersDock::bind(AssetBrowser& browser0) {
    browser_ = &browser0;
    refresh();
}

void LayersDock::refresh() {
    if (!browser_ || !list_) {
        return;
    }
    loading_ = true;
    const int current = list_->currentItem() ? list_->currentItem()->data(Qt::UserRole).toInt() : -1;
    list_->clear();
    for (int i = 1; i < 32; ++i) {
        if (LayerSettings::names[std::size_t(i)].empty()) {
            continue;
        }
        auto* item = new QListWidgetItem(QString::fromStdString(LayerSettings::names[std::size_t(i)]),
                                         list_);
        item->setData(Qt::UserRole, i);
        if (i == current) {
            list_->setCurrentItem(item);
        }
    }
    loading_ = false;
}

void LayersDock::closeEvent(QCloseEvent* event) {
    Q_EMIT closed();
    QDockWidget::closeEvent(event);
}

PrefabDock::PrefabDock(QWidget* parent) : QDockWidget(tr("Prefab Mode"), parent) {
    setObjectName(QStringLiteral("PrefabDock"));
    auto* content = new QWidget(this);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(metrics::panelPadding, 6, metrics::panelPadding, 8);
    layout->setSpacing(metrics::spacing);
    path_ = new QLabel(tr("No prefab open"), content);
    path_->setWordWrap(true);
    layout->addWidget(path_);
    auto* hint = new QLabel(
        tr("Hierarchy and Scene contain only this prefab. Save updates linked instances; Exit "
           "restores the level. Unsaved edits are discarded on Exit."),
        content);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #8fa3bb;"));
    layout->addWidget(hint);
    auto* save = new QPushButton(tr("Save Prefab"), content);
    auto* exit = new QPushButton(tr("Exit Prefab Mode"), content);
    layout->addWidget(save);
    layout->addWidget(exit);
    layout->addStretch(1);
    setWidget(content);
    setMinimumWidth(300);
    connect(save, &QPushButton::clicked, this, [this]() { Q_EMIT saveRequested(); });
    connect(exit, &QPushButton::clicked, this, [this]() { Q_EMIT exitRequested(); });
}

void PrefabDock::setPrefab(const QString& path) {
    if (path_) {
        path_->setText(tr("Editing %1").arg(path));
    }
}

void PrefabDock::clear() {
    if (path_) {
        path_->setText(tr("No prefab open"));
    }
}

void PrefabDock::closeEvent(QCloseEvent* event) {
    Q_EMIT closed();
    QDockWidget::closeEvent(event);
}

} // namespace qt
} // namespace ui
