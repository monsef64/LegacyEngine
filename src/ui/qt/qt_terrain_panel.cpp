#include "ui/qt/qt_panels.hpp"

#include "assets/assets.hpp"
#include "editor/asset_browser.hpp"
#include "editor/editor_history.hpp"
#include "terrain/terrain_editor.hpp"
#include "terrain/terrain_io.hpp"
#include "terrain/terrain_tools.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

namespace ui {
namespace qt {

namespace {

const char* const kToolNames[]{"Raise", "Lower", "Smooth", "Flatten", "Paint"};

QString sized(float metres) {
    return QStringLiteral("%1 m").arg(metres, 0, 'f', 1);
}

} // namespace

TerrainPanel::TerrainPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    content_ = new QWidget(scroll_);
    scroll_->setWidget(content_);
    layout->addWidget(scroll_);
}

void TerrainPanel::bind(World& world0,
                        AssetBrowser& browser0,
                        EditorHistory& history0,
                        Terrains::Editor& editor0) {
    world_ = &world0;
    browser_ = &browser0;
    history_ = &history0;
    editor_ = &editor0;
}

void TerrainPanel::setSelection(Entity entity) {
    if (entity.index != selected_.index || entity.generation != selected_.generation) {
        selected_ = entity;
        builtEntity_ = ~0u;
    }
}

void TerrainPanel::setEditable(bool editable) {
    editable_ = editable;
}

void TerrainPanel::refresh() {
    if (!world_ || !editor_) {
        return;
    }
    const unsigned revision = world_->alive(selected_) && world_->has<Terrain>(selected_)
                                  ? 1u + selected_.index
                                  : 0u;
    if (revision != builtEntity_) {
        builtEntity_ = revision;
        rebuild();
    }
}

void TerrainPanel::rebuild() {
    // Tear down the previous page. The panel is small enough that a full
    // rebuild on selection change is cheaper than tracking every widget.
    if (auto* old = content_->layout()) {
        QLayoutItem* item = nullptr;
        while ((item = old->takeAt(0)) != nullptr) {
            if (auto* widget = item->widget()) {
                widget->deleteLater();
            }
            delete item;
        }
        delete old;
    }
    auto* root = new QVBoxLayout(content_);
    root->setContentsMargins(metrics::panelPadding, 6, metrics::panelPadding, 8);
    root->setSpacing(metrics::spacing);

    if (!world_->alive(selected_) || !world_->has<Terrain>(selected_)) {
        auto* hint = new QLabel(tr("Select an object with a Terrain component to sculpt it."));
        hint->setWordWrap(true);
        hint->setStyleSheet(QStringLiteral("color: #8fa3bb;"));
        root->addWidget(hint);
        root->addStretch(1);
        return;
    }
    auto& terrain = world_->get<Terrain>(selected_);
    auto& editor = *editor_;

    auto* brush = new QCheckBox(tr("Enable Scene brush"), content_);
    brush->setChecked(editor.enabled());
    brush->setEnabled(editable_);
    connect(brush, &QCheckBox::toggled, this, [this](bool checked) {
        editor_->enabled() = checked;
        Q_EMIT terrainEdited();
    });
    root->addWidget(brush);

    auto* tool = new QComboBox(content_);
    for (const char* name : kToolNames) {
        tool->addItem(QString::fromUtf8(name));
    }
    tool->setCurrentIndex(std::clamp(editor.tool(), 0, 4));
    connect(tool, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        editor_->tool() = index;
        builtEntity_ = ~0u;
        rebuild();
    });
    auto* toolRow = new QHBoxLayout();
    toolRow->addWidget(new QLabel(tr("Tool"), content_));
    toolRow->addWidget(tool, 1);
    root->addLayout(toolRow);

    auto* shapes = new QGroupBox(tr("Brush shape"), content_);
    auto* shapeLayout = new QVBoxLayout(shapes);
    shapeLayout->setSpacing(2);
    const auto& names = Terrains::Editor::brushLabels();
    for (int i = 0; i < int(names.size()); ++i) {
        auto* button = new QToolButton(shapes);
        button->setText(QStringLiteral("%1. %2").arg(i + 1).arg(QString::fromUtf8(names[i])));
        button->setCheckable(true);
        button->setChecked(editor.brushShape() == i);
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        connect(button, &QToolButton::clicked, this, [this, i]() {
            editor_->brushShape() = i;
            Q_EMIT terrainEdited();
        });
        shapeLayout->addWidget(button);
    }
    root->addWidget(shapes);

    auto* radius = new FloatEdit(tr("Brush radius"), content_);
    radius->setRange(0.1f, 500.0f);
    radius->setStep(0.1f);
    radius->set(editor.radius());
    radius->setSuffix(QStringLiteral(" m"));
    connect(radius, &FloatEdit::edited, this, [this](float value) {
        editor_->radius() = value;
        Q_EMIT terrainEdited();
    });
    root->addWidget(radius);

    auto* strength = new SliderEdit(tr("Strength"), content_);
    strength->setRange(0.01f, 100.0f);
    strength->setStep(0.01f);
    strength->set(editor.strength());
    connect(strength, &SliderEdit::edited, this, [this](float value) {
        editor_->strength() = value;
        Q_EMIT terrainEdited();
    });
    root->addWidget(strength);

    auto* falloff = new SliderEdit(tr("Falloff"), content_);
    falloff->setRange(0.0f, 1.0f);
    falloff->setStep(0.01f);
    falloff->set(editor.falloff());
    connect(falloff, &SliderEdit::edited, this, [this](float value) {
        editor_->falloff() = value;
        Q_EMIT terrainEdited();
    });
    root->addWidget(falloff);

    if (editor.tool() == 3) {
        auto* flatten = new FloatEdit(tr("Flatten height"), content_);
        flatten->setRange(-10000.0f, 10000.0f);
        flatten->setStep(0.1f);
        flatten->set(editor.flatten());
        connect(flatten, &FloatEdit::edited, this, [this](float value) {
            editor_->flatten() = value;
        });
        root->addWidget(flatten);
    }
    if (editor.tool() == 4) {
        auto* layer = new IntEdit(tr("Paint layer"), content_);
        layer->setRange(0, 3);
        layer->set(editor.layer());
        connect(layer, &IntEdit::edited, this, [this](int value) { editor_->layer() = value; });
        root->addWidget(layer);
    }

    auto* hint = new QLabel(
        tr("Left-drag in the Scene view to sculpt or paint. Alt navigation and right-drag stay "
           "camera controls; release commits one undo step and updates collision."));
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #8fa3bb;"));
    root->addWidget(hint);

    if (terrain.data && !terrain.data->lightmap.empty()) {
        auto* lightmap = new QLabel(tr("Baked atlas: %1")
                                        .arg(QString::fromStdString(terrain.data->lightmap)),
                                    content_);
        lightmap->setWordWrap(true);
        root->addWidget(lightmap);
        auto* clear = new QPushButton(tr("Clear terrain bake"), content_);
        clear->setEnabled(editable_);
        connect(clear, &QPushButton::clicked, this, [this]() {
            auto& terrain = world_->get<Terrain>(selected_);
            history_->commit(*world_, selected_);
            Terrains::writable(terrain);
            terrain.data->lightmap.clear();
            terrain.data->revision = Terrains::nextRevision();
            history_->commit(*world_, selected_);
            builtEntity_ = ~0u;
            Q_EMIT terrainEdited();
        });
        root->addWidget(clear);
    }

    auto* save = new QPushButton(tr("Save .terrain asset"), content_);
    save->setEnabled(editable_);
    connect(save, &QPushButton::clicked, this, [this]() {
        auto& terrain = world_->get<Terrain>(selected_);
        try {
            history_->commit(*world_, selected_);
            Terrains::save(terrain, browser_->assets);
            history_->commit(*world_, selected_);
            editor_->error().clear();
        } catch (const std::exception& e) {
            editor_->error() = e.what();
        }
        builtEntity_ = ~0u;
        Q_EMIT terrainEdited();
    });
    root->addWidget(save);

    auto* assetRow = new TextRow(tr("Terrain asset"), tr("Select a .terrain file"), content_);
    assetRow->set(QString::fromStdString(editor.source()));
    connect(assetRow, &TextRow::edited, this, [this](const QString& text) {
        editor_->source() = text.toStdString();
    });
    root->addWidget(assetRow);

    auto* load = new QPushButton(tr("Load selected terrain"), content_);
    connect(load, &QPushButton::clicked, this, [this]() {
        if (editor_->source().empty()) {
            return;
        }
        auto& terrain = world_->get<Terrain>(selected_);
        try {
            history_->commit(*world_, selected_);
            Terrain loaded;
            loaded.asset = editor_->source();
            Terrains::load(loaded, browser_->assets);
            terrain = std::move(loaded);
            history_->commit(*world_, selected_);
            editor_->error().clear();
        } catch (const std::exception& e) {
            editor_->error() = e.what();
        }
        builtEntity_ = ~0u;
        Q_EMIT terrainEdited();
    });
    root->addWidget(load);

    if (terrain.data) {
        const auto& data = *terrain.data;
        root->addWidget(new QLabel(tr("%1 x %1 height samples | %2")
                                       .arg(data.resolution)
                                       .arg(sized(data.size)),
                                   content_));
    }

    for (int i = 0; i < 4; ++i) {
        auto* layerBox = new QGroupBox(tr("Material layer %1").arg(i), content_);
        auto* layerLayout = new QVBoxLayout(layerBox);
        auto* material = new TextRow(tr("Material"), tr("No material (.mat)"), layerBox);
        material->set(terrain.data ? QString::fromStdString(terrain.data->layers[i].material)
                                   : QString());
        connect(material, &TextRow::edited, this, [this, i](const QString& text) {
            auto& terrain = world_->get<Terrain>(selected_);
            if (!terrain.data) {
                return;
            }
            Terrains::writable(terrain);
            terrain.data->layers[std::size_t(i)].material = text.toStdString();
            terrain.data->revision = Terrains::nextRevision();
            Q_EMIT terrainEdited();
        });
        layerLayout->addWidget(material);
        auto* albedo = new TextRow(tr("Albedo"), tr("No texture"), layerBox);
        albedo->set(terrain.data ? QString::fromStdString(terrain.data->layers[i].texture)
                                 : QString());
        connect(albedo, &TextRow::edited, this, [this, i](const QString& text) {
            auto& terrain = world_->get<Terrain>(selected_);
            if (!terrain.data) {
                return;
            }
            Terrains::writable(terrain);
            terrain.data->layers[std::size_t(i)].texture = text.toStdString();
            terrain.data->revision = Terrains::nextRevision();
            Q_EMIT terrainEdited();
        });
        layerLayout->addWidget(albedo);
        auto* tint = new ColorEdit(tr("Tint"), layerBox);
        if (terrain.data) {
            tint->set(terrain.data->layers[i].color);
        }
        connect(tint, &ColorEdit::edited, this, [this, i](const Vec3& value) {
            auto& terrain = world_->get<Terrain>(selected_);
            if (!terrain.data) {
                return;
            }
            Terrains::writable(terrain);
            terrain.data->layers[std::size_t(i)].color = value;
            terrain.data->revision = Terrains::nextRevision();
            Q_EMIT terrainEdited();
        });
        layerLayout->addWidget(tint);
        auto* normal = new SliderEdit(tr("Normal strength"), layerBox);
        normal->setRange(0, 4);
        normal->setStep(0.01f);
        if (terrain.data) {
            normal->set(terrain.data->layers[i].normalStrength);
        }
        connect(normal, &SliderEdit::edited, this, [this, i](float value) {
            auto& terrain = world_->get<Terrain>(selected_);
            if (!terrain.data) {
                return;
            }
            Terrains::writable(terrain);
            terrain.data->layers[std::size_t(i)].normalStrength = value;
            terrain.data->revision = Terrains::nextRevision();
            Q_EMIT terrainEdited();
        });
        layerLayout->addWidget(normal);
        root->addWidget(layerBox);
    }

    auto* heightmapBox = new QGroupBox(tr("Heightmap import"), content_);
    auto* heightmapLayout = new QVBoxLayout(heightmapBox);
    auto* heightmap = new TextRow(tr("Heightmap"), tr("Project-relative image"), heightmapBox);
    heightmap->set(QString::fromStdString(editor.heightmap()));
    connect(heightmap, &TextRow::edited, this, [this](const QString& text) {
        editor_->heightmap() = text.toStdString();
    });
    heightmapLayout->addWidget(heightmap);
    auto* range = new FloatEdit(tr("Height range"), heightmapBox);
    range->setRange(0, 10000);
    range->setStep(0.5f);
    range->set(editor.importScale());
    connect(range, &FloatEdit::edited, this, [this](float value) {
        editor_->importScale() = value;
    });
    heightmapLayout->addWidget(range);
    auto* offset = new FloatEdit(tr("Height offset"), heightmapBox);
    offset->setRange(-10000, 10000);
    offset->setStep(0.1f);
    offset->set(editor.importOffset());
    connect(offset, &FloatEdit::edited, this, [this](float value) {
        editor_->importOffset() = value;
    });
    heightmapLayout->addWidget(offset);
    auto* import = new QPushButton(tr("Import heightmap"), heightmapBox);
    connect(import, &QPushButton::clicked, this, [this]() {
        if (editor_->heightmap().empty()) {
            return;
        }
        auto& terrain = world_->get<Terrain>(selected_);
        try {
            history_->commit(*world_, selected_);
            Terrains::importHeightmap(terrain,
                                      browser_->assets,
                                      editor_->heightmap(),
                                      editor_->importScale(),
                                      editor_->importOffset());
            history_->commit(*world_, selected_);
            editor_->error().clear();
        } catch (const std::exception& e) {
            editor_->error() = e.what();
        }
        builtEntity_ = ~0u;
        Q_EMIT terrainEdited();
    });
    heightmapLayout->addWidget(import);
    auto* importHint = new QLabel(
        tr("8/16-bit images, up to 4096 pixels, bilinearly resampled to this grid. "
           "The image top maps to terrain Z = 0."),
        heightmapBox);
    importHint->setWordWrap(true);
    importHint->setStyleSheet(QStringLiteral("color: #8fa3bb;"));
    heightmapLayout->addWidget(importHint);
    root->addWidget(heightmapBox);

    auto* flatBox = new QGroupBox(tr("New flat terrain"), content_);
    auto* flatLayout = new QVBoxLayout(flatBox);
    auto* resolution = new QComboBox(flatBox);
    resolution->addItems({QStringLiteral("65"), QStringLiteral("129"), QStringLiteral("257")});
    resolution->setCurrentIndex(std::clamp(editor.resolution(), 0, 2));
    connect(resolution, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int index) { editor_->resolution() = index; });
    flatLayout->addWidget(resolution);
    auto* size = new FloatEdit(tr("Terrain size"), flatBox);
    size->setRange(1, 10000);
    size->setStep(1.0f);
    size->set(editor.size());
    connect(size, &FloatEdit::edited, this, [this](float value) { editor_->size() = value; });
    flatLayout->addWidget(size);
    auto* replace = new QPushButton(tr("Replace with flat terrain"), flatBox);
    connect(replace, &QPushButton::clicked, this, [this]() {
        auto& terrain = world_->get<Terrain>(selected_);
        history_->commit(*world_, selected_);
        auto data = std::make_shared<Terrains::Data>();
        data->resolution = (1 << (editor_->resolution() + 6)) + 1;
        data->size = editor_->size();
        data->heights.assign(std::size_t(data->resolution) * data->resolution, 0);
        data->weights.assign(std::size_t(data->resolution) * data->resolution, {1, 0, 0, 0});
        Terrains::invalidate(*data);
        terrain.data = data;
        history_->commit(*world_, selected_);
        builtEntity_ = ~0u;
        Q_EMIT terrainEdited();
    });
    flatLayout->addWidget(replace);
    root->addWidget(flatBox);

    if (!editor.error().empty()) {
        auto* error = new QLabel(QString::fromStdString(editor.error()), content_);
        error->setWordWrap(true);
        error->setStyleSheet(QStringLiteral("color: #e07b7b;"));
        root->addWidget(error);
    }
    root->addStretch(1);
}

} // namespace qt
} // namespace ui
