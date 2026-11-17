#include "ui/qt/qt_panels.hpp"

#include "assets/assets.hpp"
#include "assets/cubemap.hpp"
#include "assets/texture_preview.hpp"
#include "editor/asset_browser.hpp"
#include "editor/asset_preview_ui.hpp"
#include "editor/editor_history.hpp"
#include "materials/material_io.hpp"
#include "models/model_asset.hpp"
#include "prefabs/prefab.hpp"

#include <QComboBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>
#include <QSplitter>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>

namespace ui {
namespace qt {

namespace {

QString categoryOf(const std::string& path) {
    const auto ext = AssetManager::extension(path);
    if (AssetManager::script(ext) || ext == ".ls" || ext == ".lua") {
        return QStringLiteral("Scripts");
    }
    if (modelAsset(path) || ext == ".lemesh") {
        return QStringLiteral("Models");
    }
    if (textureAsset(path)) {
        return QStringLiteral("Textures");
    }
    if (materialAsset(path)) {
        return QStringLiteral("Materials");
    }
    if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") {
        return QStringLiteral("Audio");
    }
    if (ext == ".prefab") {
        return QStringLiteral("Prefabs");
    }
    if (ext == ".terrain") {
        return QStringLiteral("Terrains");
    }
    if (ext == ".leanim") {
        return QStringLiteral("Animations");
    }
    if (ext == ".cubemap") {
        return QStringLiteral("Skyboxes");
    }
    return QStringLiteral("Other");
}

} // namespace

AssetPanel::AssetPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(metrics::panelPadding, 6, metrics::panelPadding, 6);
    layout->setSpacing(metrics::spacing);

    auto* bar = new QHBoxLayout();
    bar->setSpacing(metrics::spacing);
    auto* up = new QToolButton(this);
    up->setText(QStringLiteral(".."));
    up->setToolTip(tr("Parent folder"));
    connect(up, &QToolButton::clicked, this, [this]() {
        if (currentFolder_.isEmpty() || !browser_) {
            return;
        }
        const auto parent = std::filesystem::path(currentFolder_.toStdString()).parent_path();
        currentFolder_ = QString::fromStdString(
            parent == browser_->assets.root ? std::string() : parent.generic_string());
        builtRevision_ = ~0u;
        rebuildTree();
    });
    bar->addWidget(up);
    pathEdit_ = new QLineEdit(this);
    pathEdit_->setReadOnly(true);
    pathEdit_->setPlaceholderText(tr("Assets"));
    bar->addWidget(pathEdit_, 1);
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Search (name or ext)"));
    search_->setClearButtonEnabled(true);
    connect(search_, &QLineEdit::textChanged, this, [this]() {
        builtRevision_ = ~0u;
        rebuildTree();
    });
    bar->addWidget(search_, 1);
    auto* import = new QToolButton(this);
    import->setText(tr("Import"));
    import->setToolTip(tr("Import a model, texture, audio or script"));
    connect(import, &QToolButton::clicked, this, [this]() { Q_EMIT importRequested(); });
    bar->addWidget(import);
    gridButton_ = new QToolButton(this);
    gridButton_->setText(tr("Grid"));
    gridButton_->setCheckable(true);
    gridButton_->setChecked(false);
    connect(gridButton_, &QToolButton::clicked, this, [this](bool checked) {
        grid_ = checked;
        listButton_->setChecked(!checked);
        if (browser_) {
            browser_->setGridMode(checked);
        }
        builtRevision_ = ~0u;
        rebuildTree();
    });
    bar->addWidget(gridButton_);
    listButton_ = new QToolButton(this);
    listButton_->setText(tr("List"));
    listButton_->setCheckable(true);
    listButton_->setChecked(true);
    connect(listButton_, &QToolButton::clicked, this, [this](bool checked) {
        grid_ = !checked;
        gridButton_->setChecked(!checked);
        if (browser_) {
            browser_->setGridMode(!checked);
        }
        builtRevision_ = ~0u;
        rebuildTree();
    });
    bar->addWidget(listButton_);
    layout->addLayout(bar);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    files_ = new QTreeWidget(splitter);
    files_->setHeaderHidden(true);
    files_->setColumnCount(2);
    files_->setRootIsDecorated(false);
    files_->setUniformRowHeights(true);
    files_->setContextMenuPolicy(Qt::CustomContextMenu);
    files_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    files_->setIconSize(QSize(16, 16));
    splitter->addWidget(files_);

    auto* details = new QWidget(splitter);
    auto* detailsLayout = new QVBoxLayout(details);
    detailsLayout->setContentsMargins(6, 0, 0, 0);
    preview_ = new QLabel(details);
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setMinimumSize(120, 90);
    preview_->setText(tr("No preview"));
    detailsLayout->addWidget(preview_);
    details_ = new QLabel(details);
    details_->setWordWrap(true);
    details_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailsLayout->addWidget(details_, 1);
    splitter->addWidget(details);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    layout->addWidget(splitter, 1);

    connect(files_, &QTreeWidget::itemSelectionChanged, this, [this]() {
        const auto items = files_->selectedItems();
        if (items.isEmpty()) {
            if (browser_) {
                browser_->selected.clear();
            }
            return;
        }
        const QString path = items.front()->data(0, Qt::UserRole).toString();
        if (browser_) {
            browser_->selected = path.toStdString();
        }
        showDetails(path);
    });
    connect(files_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        if (item) {
            activate(item->data(0, Qt::UserRole).toString());
        }
    });
    connect(files_, &QWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        auto* item = files_->itemAt(point);
        if (!item) {
            return;
        }
        const QString path = item->data(0, Qt::UserRole).toString();
        const std::string std = path.toStdString();
        QMenu menu(this);
        auto* open = menu.addAction(tr("Open / Add to scene"));
        connect(open, &QAction::triggered, this, [this, path]() { activate(path); });
        if (modelAsset(std) || Prefabs::asset(std)) {
            auto* spawn = menu.addAction(tr("Spawn in scene"));
            connect(spawn, &QAction::triggered, this, [this, std]() {
                if (Prefabs::asset(std)) {
                    Q_EMIT spawnPrefab(QString::fromStdString(std));
                } else {
                    Q_EMIT spawnModel(QString::fromStdString(std));
                }
            });
        }
        if (Prefabs::asset(std)) {
            auto* edit = menu.addAction(tr("Edit prefab"));
            connect(edit, &QAction::triggered, this, [this, path]() { Q_EMIT editPrefab(path); });
        }
        menu.exec(files_->viewport()->mapToGlobal(point));
    });
}

void AssetPanel::bind(World& world0,
                      AssetBrowser& browser0,
                      EditorHistory& history0,
                      Renderer& renderer0) {
    world_ = &world0;
    browser_ = &browser0;
    history_ = &history0;
    renderer_ = &renderer0;
    grid_ = browser_->gridMode();
    gridButton_->setChecked(grid_);
    listButton_->setChecked(!grid_);
    builtRevision_ = ~0u;
    rebuildTree();
}

void AssetPanel::setEditable(bool editable) {
    editable_ = editable;
}

void AssetPanel::refresh() {
    if (!browser_) {
        return;
    }
    browser_->pollImport();
    const unsigned revision = modelAssetRevision;
    if (revision != builtRevision_) {
        rebuildTree();
    } else {
        // Keep the highlighted row in sync when the engine selects an asset.
        if (!browser_->selected.empty()) {
            selectPath(QString::fromStdString(browser_->selected));
        }
    }
}

void AssetPanel::rebuildTree() {
    if (!browser_) {
        return;
    }
    builtRevision_ = modelAssetRevision;
    const auto previous = files_->selectedItems().isEmpty()
                              ? browser_->selected
                              : files_->selectedItems().front()
                                    ->data(0, Qt::UserRole)
                                    .toString()
                                    .toStdString();
    files_->clear();
    const QString filter = search_->text().trimmed();
    const auto folder = currentFolder_.toStdString();
    pathEdit_->setText(QStringLiteral("Assets/%1").arg(currentFolder_));
    auto browseRoot = browser_->assets.root;
    const auto folderPath = folder.empty() ? browseRoot : browseRoot / folder;
    std::error_code ec;
    std::vector<std::pair<std::string, bool>> directories;
    for (const auto& entry :
         std::filesystem::directory_iterator(folderPath, std::filesystem::directory_options::skip_permission_denied, ec)) {
        const auto name = entry.path().filename().string();
        if (name.empty() || name.front() == '.') {
            continue;
        }
        if (entry.is_directory(ec)) {
            directories.emplace_back(name, true);
            continue;
        }
        directories.emplace_back(name, false);
    }
    std::sort(directories.begin(), directories.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) {
            return a.second;
        }
        return a.first < b.first;
    });
    for (const auto& [name, isFolder] : directories) {
        const auto relative = folder.empty() ? name : folder + "/" + name;
        if (!filter.isEmpty() &&
            !QString::fromStdString(name).contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        auto* item = new QTreeWidgetItem(files_);
        item->setText(0, QString::fromStdString(name));
        item->setData(0, Qt::UserRole, QString::fromStdString(relative));
        if (isFolder) {
            item->setText(1, tr("folder"));
        } else {
            item->setText(1, categoryOf(relative));
                if (textureAsset(relative)) {
                const auto image = thumbnailImage(relative);
                if (!image.isNull()) {
                    item->setIcon(0, QIcon(QPixmap::fromImage(image.scaled(
                                              24, 24, Qt::KeepAspectRatio,
                                              Qt::SmoothTransformation))));
                }
            }
        }
    }
    if (!previous.empty()) {
        selectPath(QString::fromStdString(previous));
    }
}

void AssetPanel::selectPath(const QString& path) {
    for (int i = 0; i < files_->topLevelItemCount(); ++i) {
        auto* item = files_->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toString() == path) {
            if (!item->isSelected()) {
                files_->setCurrentItem(item);
            }
            return;
        }
    }
}

void AssetPanel::showDetails(const QString& path) {
    if (!browser_) {
        return;
    }
    const auto absolute = browser_->assets.resolve(path.toStdString());
    std::error_code ec;
    const auto size = std::filesystem::is_regular_file(absolute, ec)
                          ? std::filesystem::file_size(absolute, ec)
                          : 0;
    const std::string std = path.toStdString();
    details_->setText(QStringLiteral("%1\n%2\n%3")
                          .arg(path,
                               categoryOf(std),
                               size ? QStringLiteral("%1 bytes").arg(qulonglong(size))
                                    : QStringLiteral("folder")));
    if (textureAsset(std)) {
        const auto image = thumbnailImage(std);
        if (!image.isNull()) {
            preview_->setPixmap(QPixmap::fromImage(image.scaled(
                160, 160, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
        } else {
            preview_->setText(tr("No preview"));
        }
    } else if (assetThumbnail) {
        const auto preview = assetThumbnail(browser_->assets, std);
        if (!preview.text.empty()) {
            details_->setText(QString::fromStdString(preview.text));
        }
        preview_->setText(preview.error.empty() ? tr("No preview image")
                                                : QString::fromStdString(preview.error));
    } else {
        preview_->setText(tr("No preview"));
    }
}

void AssetPanel::activate(const QString& path) {
    if (!browser_ || !editable_) {
        return;
    }
    const std::string std = path.toStdString();
    const auto ext = AssetManager::extension(std);
    if (std::filesystem::is_directory(browser_->assets.resolve(std))) {
        currentFolder_ = path;
        builtRevision_ = ~0u;
        rebuildTree();
        return;
    }
    if (AssetManager::script(ext) || ext == ".ls" || ext == ".lua") {
        browser_->openedScript = std;
        Q_EMIT openScript(path);
    } else if (Prefabs::asset(std)) {
        Q_EMIT spawnPrefab(path);
    } else if (modelAsset(std)) {
        Q_EMIT spawnModel(path);
    } else if (ext == ".leanim") {
        Q_EMIT editAnimation(path);
    } else if (ext == ".cubemap") {
        Q_EMIT editCubemap(path);
    } else if (ext == ".terrain") {
        browser_->spawnModelRequest.clear();
    } else {
        Q_EMIT previewModel(path);
    }
}

QImage AssetPanel::thumbnailImage(const std::string& path) const {
    if (!browser_) {
        return {};
    }
    // The ImGui browser cached GL thumbnails; Qt needs QImage data, so decode
    // straight from the shared disk cache instead of touching the GL context.
    const auto thumbnail = loadTextureThumbnail(static_cast<const AssetManager&>(browser_->assets), path);
    return QImage(thumbnail.pixels.data(),
                  TextureThumbnail::size,
                  TextureThumbnail::size,
                  QImage::Format_RGBA8888)
        .copy();
}

std::vector<std::string> AssetPanel::currentFiles() const {
    std::vector<std::string> result;
    for (int i = 0; i < files_->topLevelItemCount(); ++i) {
        result.push_back(files_->topLevelItem(i)->data(0, Qt::UserRole).toString().toStdString());
    }
    return result;
}

} // namespace qt
} // namespace ui
