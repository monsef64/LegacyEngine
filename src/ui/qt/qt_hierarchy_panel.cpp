#include "ui/qt/qt_panels.hpp"

#include "core/ecs.hpp"
#include "core/hierarchy.hpp"
#include "editor/asset_browser.hpp"
#include "editor/editor_history.hpp"
#include "editor/level_objects.hpp"

#include <QAction>
#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QHeaderView>
#include <QMenu>
#include <QSignalBlocker>
#include <QToolButton>
#include <QAbstractItemView>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace ui {
namespace qt {

namespace {

constexpr int kEntityRole = Qt::UserRole + 1;

qulonglong packEntity(Entity entity) {
    return (qulonglong(entity.generation) << 32) | qulonglong(entity.index);
}

Entity unpackEntity(const QVariant& value) {
    const qulonglong packed = value.value<qulonglong>();
    Entity entity{};
    entity.index = std::uint32_t(packed & 0xffffffffull);
    entity.generation = std::uint32_t(packed >> 32);
    return entity;
}
constexpr int kDepthRole = Qt::UserRole + 2;

} // namespace

HierarchyPanel::HierarchyPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(metrics::panelPadding, 6, metrics::panelPadding, 6);
    layout->setSpacing(metrics::spacing);

    auto* header = new QHBoxLayout();
    header->setSpacing(metrics::spacing);
    createButton_ = new QToolButton(this);
    createButton_->setText(tr("Create"));
    createButton_->setPopupMode(QToolButton::InstantPopup);
    createButton_->setToolTip(tr("Create a new object"));
    auto* menu = new QMenu(createButton_);
    int count = 0;
    const auto& entries = createEntries(&count);
    for (const auto& entry : entries) {
        auto* action = menu->addAction(QString::fromUtf8(entry.label));
        const int id = entry.id;
        connect(action, &QAction::triggered, this, [this, id]() { createChild(id); });
    }
    createButton_->setMenu(menu);
    header->addWidget(createButton_);

    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Search objects"));
    search_->setClearButtonEnabled(true);
    connect(search_, &QLineEdit::textChanged, this, [this]() {
        builtRevision_ = ~0u;
        rebuildRows();
    });
    header->addWidget(search_, 1);

    auto* collapse = new QToolButton(this);
    collapse->setText(tr("Collapse"));
    collapse->setToolTip(tr("Collapse every branch"));
    connect(collapse, &QToolButton::clicked, this, [this]() { tree_->collapseAll(); });
    header->addWidget(collapse);
    layout->addLayout(header);

    levelLabel_ = new QLabel(tr("Untitled"), this);
    levelLabel_->setToolTip(tr("Current level. Right-click to rename it."));
    levelLabel_->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(levelLabel_);

    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("HierarchyTree"));
    tree_->setHeaderHidden(true);
    tree_->setColumnCount(1);
    tree_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tree_->setUniformRowHeights(true);
    tree_->setIndentation(14);
    tree_->setExpandsOnDoubleClick(false);
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
#ifndef QT_NO_DRAGANDDROP
    tree_->setDragDropMode(QAbstractItemView::InternalMove);
    tree_->setDragEnabled(true);
    tree_->setAcceptDrops(true);
    tree_->setDropIndicatorShown(true);
#endif
    layout->addWidget(tree_, 1);

    connect(levelLabel_, &QLabel::customContextMenuRequested, this, [this](const QPoint& point) {
        QMenu menu(this);
        auto* rename = menu.addAction(tr("Rename Level..."));
        if (menu.exec(levelLabel_->mapToGlobal(point)) == rename) {
            Q_EMIT renameLevelRequested();
        }
    });

    connect(tree_,
            &QTreeWidget::itemSelectionChanged,
            this,
            [this]() {
                if (updating_) {
                    return;
                }
                std::vector<Entity> picked;
                const auto items = tree_->selectedItems();
                for (auto* item : items) {
                    const Entity entity = unpackEntity(item->data(0, kEntityRole));
                    if (world_ && world_->alive(entity) &&
                        std::find_if(picked.begin(),
                                     picked.end(),
                                     [&](const Entity& other) {
                                         return other.index == entity.index &&
                                                other.generation == entity.generation;
                                     }) == picked.end()) {
                        picked.push_back(entity);
                    }
                }
                // Keep visual order, which is the order shift-selection spans.
                std::vector<Entity> ordered;
                for (const auto& node : rows_) {
                    for (const auto& entity : picked) {
                        if (entity == node.entity) {
                            ordered.push_back(entity);
                        }
                    }
                }
                selection_ = ordered;
                if (!ordered.empty()) {
                    selected_ = ordered.back();
                    Q_EMIT selectionChanged(selected_);
                } else {
                    selected_ = {};
                    Q_EMIT selectionChanged({});
                }
            });

    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int) {
        if (!editable_ || !item) {
            return;
        }
        selected_ = unpackEntity(item->data(0, kEntityRole));
        beginInlineRename(item);
    });
    connect(tree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* item, int column) {
        finishInlineRename(item, column);
    });
    connect(tree_,
            &QTreeWidget::customContextMenuRequested,
            this,
            [this](const QPoint& point) { tree_->setProperty("menuPos", point); });
}

void HierarchyPanel::bind(World& world0, AssetBrowser& browser0, EditorHistory& history0) {
    world_ = &world0;
    browser_ = &browser0;
    history_ = &history0;
    builtRevision_ = ~0u;
    rebuildRows();
}

unsigned HierarchyPanel::structureRevision() const {
    if (!world_) {
        return 0;
    }
    unsigned hash = 2166136261u;
    auto fold = [&hash](std::uint32_t value) {
        hash ^= value;
        hash *= 16777619u;
    };
    for (const auto& entity : world_->entities()) {
        fold(entity.index);
        fold(entity.generation);
        const auto parent = getParent(*world_, entity);
        fold(parent.index);
        fold(parent.generation);
        const auto* name = world_->tryGet<ObjectName>(entity);
        for (unsigned char c : name ? name->value : std::string()) {
            fold(c);
        }
    }
    return hash;
}

void HierarchyPanel::refresh(Entity selected) {
    if (!world_) {
        return;
    }
    if (selected != selected_ && selected != Entity{}) {
        selected_ = selected;
        if (std::find_if(selection_.begin(),
                         selection_.end(),
                         [&](const Entity& other) { return other == selected; }) ==
            selection_.end()) {
            selection_ = {selected};
        }
    }
    auto searchHash = [](const QString& text) {
        unsigned hash = 5381u;
        for (const auto ch : text) {
            hash = hash * 33u + static_cast<unsigned>(ch.unicode());
        }
        return hash;
    };
    const unsigned revision = structureRevision();
    const unsigned search = searchHash(search_->text());
    if (revision != builtRevision_ || search != builtSearch_) {
        rebuildRows();
    }
    applySelectionToTree();
}

void HierarchyPanel::setLevelName(const QString& name) {
    levelLabel_->setText(name.isEmpty() ? tr("Untitled") : name);
}

void HierarchyPanel::setEditable(bool editable) {
    editable_ = editable;
    createButton_->setEnabled(editable);
}

void HierarchyPanel::rebuildRows() {
    if (!world_) {
        return;
    }
    builtRevision_ = structureRevision();
    unsigned hash = 5381u;
    for (const auto ch : search_->text()) {
        hash = hash * 33u + static_cast<unsigned>(ch.unicode());
    }
    builtSearch_ = hash;

    // Preserve expansion state by entity so rebuilds do not collapse the tree.
    std::unordered_set<std::uint64_t> expanded;
    std::function<void(QTreeWidgetItem*)> collect = [&](QTreeWidgetItem* item) {
        if (item->isExpanded()) {
            expanded.insert(item->data(0, kEntityRole).value<qulonglong>());
        }
        for (int i = 0; i < item->childCount(); ++i) {
            collect(item->child(i));
        }
    };
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        collect(tree_->topLevelItem(i));
    }

    const QString filter = search_->text().trimmed();
    const auto objects = levelObjects(*world_);
    std::unordered_map<std::uint32_t, std::vector<const LevelObject*>> byParent;
    for (const auto& object : objects) {
        const auto parent = getParent(*world_, object.entity);
        byParent[parent.index == UINT32_MAX ? UINT32_MAX : parent.index].push_back(&object);
    }

    rows_.clear();
    std::vector<Entity> drawn;
    std::function<void(QTreeWidgetItem*, Entity, int)> addRow =
        [&](QTreeWidgetItem* parentItem, Entity entity, int depth) -> void {
        const auto found = byParent.find(entity.index == UINT32_MAX ? UINT32_MAX : entity.index);
        if (found == byParent.end()) {
            return;
        }
        for (const LevelObject* object : found->second) {
            const bool prefabAncestor = [&]() {
                for (Entity ancestor = object->entity; world_->alive(ancestor);
                     ancestor = getParent(*world_, ancestor)) {
                    if (world_->has<PrefabInstance>(ancestor)) {
                        return true;
                    }
                }
                return false;
            }();
            const bool inactive = !isActive(*world_, object->entity);

            bool visible =
        filter.isEmpty()
            ? true
            : QString::fromStdString(object->name).contains(filter, Qt::CaseInsensitive);

            std::function<bool(const LevelObject*)> matches = [&](const LevelObject* node) -> bool {
                const auto it = byParent.find(node->entity.index);
                if (it == byParent.end()) {
                    return false;
                }
                bool hit = false;
                for (const auto* child : it->second) {
                    hit = QString::fromStdString(child->name)
                              .contains(filter, Qt::CaseInsensitive) ||
                          matches(child) || hit;
                }
                return hit;
            };
            if (!filter.isEmpty()) {
                visible = visible || matches(object);
            }
            if (!filter.isEmpty() && !visible) {
                continue;
            }
            auto* item = parentItem ? new QTreeWidgetItem(parentItem) : new QTreeWidgetItem(tree_);
            item->setText(0, QString::fromStdString(object->name));
            item->setData(0, kEntityRole,
                          QVariant::fromValue<qulonglong>(packEntity(object->entity)));
            item->setData(0, kDepthRole, depth);
            item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled |
                           Qt::ItemIsDropEnabled | Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            auto font = item->font(0);
            font.setItalic(prefabAncestor);
            item->setFont(0, font);
            if (inactive) {
                item->setForeground(0, QBrush(QColor(122, 138, 158)));
            } else if (prefabAncestor) {
                item->setForeground(0, QBrush(QColor(89, 166, 255)));
            }
            item->setToolTip(
                0,
                QStringLiteral("%1\nLeft click: select  |  Double click: rename\n"
                               "Right click: object actions  |  Drag: reparent")
                    .arg(QString::fromStdString(object->name)));
            item->setIcon(0, QIcon());
            rows_.push_back({object->entity,
                             QString::fromStdString(object->name),
                             objectIcon(*world_, object->entity),
                             prefabAncestor,
                             inactive});
            drawn.push_back(object->entity);
            // Expand branches that contain matches while searching.
            const bool shouldExpand =
                !filter.isEmpty() ? visible : expanded.count(item->data(0, kEntityRole)
                                                                 .value<qulonglong>()) > 0;
            addRow(item, object->entity, depth + 1);
            item->setExpanded(shouldExpand || depth == 0);
        }
    };
    addRow(nullptr, Entity{}, 0);

    applySelectionToTree();
}

void HierarchyPanel::applySelectionToTree() {
    if (!world_) {
        return;
    }
    updating_ = true;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        std::function<void(QTreeWidgetItem*)> visit = [&](QTreeWidgetItem* item) {
            const auto entity =
                unpackEntity(item->data(0, kEntityRole));
            bool inSelection = false;
            for (const auto& selected : selection_) {
                if (selected == entity) {
                    inSelection = true;
                    break;
                }
            }
            item->setSelected(inSelection);
            for (int c = 0; c < item->childCount(); ++c) {
                visit(item->child(c));
            }
        };
        visit(tree_->topLevelItem(i));
    }
    updating_ = false;
}

void HierarchyPanel::selectEntity(Entity entity) {
    selected_ = entity;
    if (entity == Entity{}) {
        selection_.clear();
    } else {
        selection_ = {entity};
    }
    if (world_ && world_->alive(entity)) {
        selected_ = entity;
    }
    applySelectionToTree();
}

void HierarchyPanel::setSelection(const std::vector<Entity>& selection) {
    selection_ = selection;
    if (!selection.empty()) {
        selected_ = selection.back();
    }
    applySelectionToTree();
}

std::vector<Entity> HierarchyPanel::visibleOrder() const {
    std::vector<Entity> order;
    order.reserve(rows_.size());
    for (const auto& row : rows_) {
        order.push_back(row.entity);
    }
    return order;
}

void HierarchyPanel::createChild(int id) {
    if (!world_ || !editable_) {
        return;
    }
    Q_EMIT createRequested(id);
}

void HierarchyPanel::renameSelected() {
    if (!world_ || !world_->alive(selected_) || !editable_) {
        return;
    }
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        std::function<QTreeWidgetItem*(QTreeWidgetItem*)> find = [&](QTreeWidgetItem* item)
            -> QTreeWidgetItem* {
            if (unpackEntity(item->data(0, kEntityRole)) == selected_) {
                return item;
            }
            for (int c = 0; c < item->childCount(); ++c) {
                if (auto* found = find(item->child(c))) {
                    return found;
                }
            }
            return nullptr;
        };
        if (auto* item = find(tree_->topLevelItem(i))) {
            tree_->scrollToItem(item);
            beginInlineRename(item);
            return;
        }
    }
}

void HierarchyPanel::beginInlineRename(QTreeWidgetItem* item) {
    if (!item) {
        return;
    }
    renameItem_ = item;
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    tree_->editItem(item, 0);
}

void HierarchyPanel::finishInlineRename(QTreeWidgetItem* item, int column) {
    if (!item || column != 0 || !world_) {
        return;
    }
    const auto entity = unpackEntity(item->data(0, kEntityRole));
    if (!world_->alive(entity)) {
        return;
    }
    const std::string name = item->text(0).trimmed().toStdString();
    if (name.empty()) {
        const auto* previous = world_->tryGet<ObjectName>(entity);
        item->setText(0, previous ? QString::fromStdString(previous->value) : QStringLiteral("Object"));
        return;
    }
    const auto* previous = world_->tryGet<ObjectName>(entity);
    if (previous && previous->value == name) {
        return;
    }
    if (history_) {
        // Reuse the shared history so renames undo in order with other edits.
        const std::string before = previous ? previous->value : std::string("Object");
        history_->perform(*world_, selected_, [this, entity, before, name](bool forward) {
            world_->add(entity, ObjectName{forward ? name : before});
        });
    } else {
        world_->add(entity, ObjectName{name});
    }
    builtRevision_ = ~0u;
    Q_EMIT hierarchyEdited();
}

void HierarchyPanel::duplicateSelected() {
    if (!world_ || !world_->alive(selected_) || !editable_) {
        return;
    }
    Entity duplicate = duplicateLevelObject(*world_, selected_);
    selected_ = duplicate;
    selection_ = {duplicate};
    builtRevision_ = ~0u;
    rebuildRows();
    Q_EMIT selectionChanged(selected_);
    Q_EMIT hierarchyEdited();
}

void HierarchyPanel::deleteSelected() {
    if (!world_ || !editable_) {
        return;
    }
    auto batch = selection_;
    if (batch.empty() && selected_ != Entity{}) {
        batch = {selected_};
    }
    for (const auto& entity : batch) {
        if (world_->alive(entity)) {
            deleteLevelObject(*world_, entity);
        }
    }
    selected_ = {};
    selection_.clear();
    builtRevision_ = ~0u;
    rebuildRows();
    Q_EMIT selectionChanged({});
    Q_EMIT hierarchyEdited();
}

void HierarchyPanel::unparentSelected() {
    if (!world_ || !world_->alive(selected_) || !editable_) {
        return;
    }
    ::setParent(*world_, selected_, Entity{});
    builtRevision_ = ~0u;
    rebuildRows();
    Q_EMIT hierarchyEdited();
}

void HierarchyPanel::contextMenuEvent(QContextMenuEvent* event) {
    QMenu menu(this);
    auto* create = menu.addMenu(tr("Create child"));
    const auto& entries = createEntries();
    for (const auto& entry : entries) {
        auto* action = create->addAction(QString::fromUtf8(entry.label));
        const int id = entry.id;
        connect(action, &QAction::triggered, this, [this, id]() { createChild(id); });
    }
    menu.addSeparator();
    auto* rename = menu.addAction(tr("Rename..."));
    rename->setShortcut(QKeySequence(Qt::Key_F2));
    rename->setEnabled(world_ && world_->alive(selected_) && editable_);
    connect(rename, &QAction::triggered, this, [this]() { renameSelected(); });
    auto* duplicate = menu.addAction(tr("Duplicate"));
    duplicate->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    duplicate->setEnabled(world_ && world_->alive(selected_) && editable_);
    connect(duplicate, &QAction::triggered, this, [this]() { duplicateSelected(); });
    auto* unparent = menu.addAction(tr("Unparent"));
    unparent->setEnabled(world_ && world_->alive(selected_) &&
                         getParent(*world_, selected_) != Entity{} && editable_);
    connect(unparent, &QAction::triggered, this, [this]() { unparentSelected(); });
    auto* remove = menu.addAction(tr("Delete"));
    remove->setShortcut(QKeySequence::Delete);
    remove->setEnabled(world_ && world_->alive(selected_) && editable_);
    connect(remove, &QAction::triggered, this, [this]() { deleteSelected(); });
    menu.addSeparator();
    auto* renameLevel = menu.addAction(tr("Rename Level..."));
    connect(renameLevel, &QAction::triggered, this, [this]() { Q_EMIT renameLevelRequested(); });
    menu.exec(event->globalPos());
    event->accept();
}

} // namespace qt
} // namespace ui
