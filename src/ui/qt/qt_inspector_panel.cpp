#include "ui/qt/qt_panels.hpp"
#include "ui/qt/qt_preview_view.hpp"

#include "animation/animation_component.hpp"
#include "audio/audio_source.hpp"
#include "core/components.hpp"
#include "core/hierarchy.hpp"
#include "editor/asset_browser.hpp"
#include "editor/editor_history.hpp"
#include "editor/rotation_editor.hpp"
#include "editor/transform_editor.hpp"
#include "lighting/light.hpp"
#include "particles/particle_system.hpp"
#include "project/layers.hpp"
#include "rendering/renderer.hpp"
#include "scene/scene_camera.hpp"
#include "scripting/script_components.hpp"
#include "terrain/terrain.hpp"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>
#include <algorithm>

namespace ui {
namespace qt {

namespace {

constexpr const char* kComponentNames[]{
    "Transform", "Mesh Renderer", "Collider", "Rigid Body", "Camera", "Audio Source",
    "Particle System", "Lights", "Terrain", "Scripts", "Animation",
};

QString lightKind(World& world, Entity entity) {
    if (world.has<DirectionalLight>(entity)) {
        return QStringLiteral("Directional Light");
    }
    if (world.has<PointLight>(entity)) {
        return QStringLiteral("Point Light");
    }
    if (world.has<SpotLight>(entity)) {
        return QStringLiteral("Spot Light");
    }
    return {};
}

// Applies a light edit to whichever light component the object carries. The
// three light types share LightSettings, so one code path covers them all.
template <class F>
void forEachLight(World& world, Entity entity, F&& apply) {
    if (auto* light = world.tryGet<DirectionalLight>(entity)) {
        apply(static_cast<LightSettings&>(*light));
    } else if (auto* light = world.tryGet<PointLight>(entity)) {
        apply(static_cast<LightSettings&>(*light));
    } else if (auto* light = world.tryGet<SpotLight>(entity)) {
        apply(static_cast<LightSettings&>(*light));
    }
}

} // namespace

// Tracks one continuous transform edit so a drag across three spin boxes ends
// in a single undo entry, exactly like the old gesture-based history.
class TransformUndo {
  public:
    void begin(World& world, Entity entity, EditorHistory& history) {
        active_ = world.alive(entity);
        entity_ = entity;
        if (active_) {
            before_ = world.get<Transform>(entity);
            history_ = &history;
        }
    }
    void end(World& world) {
        if (!active_) {
            return;
        }
        active_ = false;
        if (!world.alive(entity_) || !history_) {
            return;
        }
        const auto& after = world.get<Transform>(entity_);
        auto before = before_;
        if (std::memcmp(&after, &before, sizeof(Transform)) == 0) {
            return;
        }
        const Entity entity = entity_;
        Transform& live = world.get<Transform>(entity);
        live = before;
        history_->perform(world, entity, [&world, entity, before, after](bool forward) {
            world.add(entity, forward ? after : before);
        });
    }

  private:
    bool active_ = false;
    Entity entity_{};
    Transform before_{};
    EditorHistory* history_ = nullptr;
};

InspectorPanel::InspectorPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    header_ = new QLabel(tr("Nothing selected"), this);
    header_->setContentsMargins(metrics::panelPadding, 6, metrics::panelPadding, 4);
    layout->addWidget(header_);
    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    content_ = new QWidget(scroll_);
    scroll_->setWidget(content_);
    layout->addWidget(scroll_, 1);
    transformUndo_ = new TransformUndo();
}

void InspectorPanel::bind(World& world0,
                          AssetBrowser& browser0,
                          EditorHistory& history0,
                          Renderer& renderer0,
                          TransformEditor& transform0) {
    world_ = &world0;
    browser_ = &browser0;
    history_ = &history0;
    renderer_ = &renderer0;
    transform_ = &transform0;
    builtRevision_ = ~0u;
}

void InspectorPanel::setSelection(Entity entity) {
    if (entity.index != selected_.index || entity.generation != selected_.generation) {
        selected_ = entity;
        builtRevision_ = ~0u;
    }
}

void InspectorPanel::setEditable(bool editable) {
    editable_ = editable;
}

void InspectorPanel::clear() {
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
    position_ = rotation_ = scale_ = nullptr;
    nameEdit_ = nullptr;
    staticCheck_ = nullptr;
    activeCheck_ = nullptr;
}

void InspectorPanel::setCameraPreview(const QImage& image) {
    cameraPreviewImage_ = image;
    if (cameraPreview_ && !image.isNull()) {
        cameraPreview_->setImage(image);
    }
}

void InspectorPanel::refresh() {
    if (!world_) {
        return;
    }
    if (selected_ != Entity{} && !world_->alive(selected_)) {
        selected_ = {};
    }
    unsigned revision = 2166136261u;
    auto fold = [&revision](std::uint32_t value) {
        revision ^= value;
        revision *= 16777619u;
    };
    fold(selected_.index);
    fold(selected_.generation);
    if (world_->alive(selected_)) {
        // Structural revision: which components exist and their identity.
        fold(world_->has<MeshRenderer>(selected_) ? 1u : 0u);
        fold(world_->has<Collider>(selected_) ? 2u : 0u);
        fold(world_->has<RigidBody>(selected_) ? 3u : 0u);
        fold(world_->has<Camera>(selected_) ? 4u : 0u);
        fold(world_->has<AudioSource>(selected_) ? 5u : 0u);
        fold(world_->has<ParticleSystem>(selected_) ? 6u : 0u);
        fold(world_->has<Terrain>(selected_) ? 7u : 0u);
        fold(static_cast<unsigned>(lightKind(*world_, selected_).size()));
        fold(world_->has<LuaScript>(selected_) ? 9u : 0u);
        fold(world_->has<Animation>(selected_) ? 10u : 0u);
    } else {
        revision = 0;
    }
    if (revision != builtRevision_) {
        builtRevision_ = revision;
        rebuild();
    } else {
        refreshValues();
    }
}

void InspectorPanel::rebuild() {
    clear();
    auto* root = new QVBoxLayout(content_);
    root->setContentsMargins(metrics::panelPadding, 4, metrics::panelPadding, 8);
    root->setSpacing(4);
    if (!world_ || !world_->alive(selected_)) {
        header_->setText(tr("Nothing selected"));
        auto* hint = new QLabel(tr("Select an object in the Hierarchy or the Scene view."), content_);
        hint->setWordWrap(true);
        hint->setStyleSheet(QStringLiteral("color: #7f8ea0;"));
        root->addWidget(hint);
        root->addStretch(1);
        return;
    }
    header_->setText(QStringLiteral("%1  (#%2)")
                         .arg(objectLabel(*world_, selected_))
                         .arg(selected_.index));
    rebuildHeader(root);
    rebuildTransform(root);
    addComponentSections(root);
    root->addStretch(1);
}

void InspectorPanel::rebuildHeader(QVBoxLayout* layout) {
    auto* nameRow = new QWidget(content_);
    auto* nameLayout = new QHBoxLayout(nameRow);
    nameLayout->setContentsMargins(0, 0, 0, 0);
    nameLayout->addWidget(new QLabel(tr("Name"), nameRow));
    nameEdit_ = new QLineEdit(nameRow);
    const auto* name = world_->tryGet<ObjectName>(selected_);
    nameEdit_->setText(name ? QString::fromStdString(name->value) : QString());
    connect(nameEdit_, &QLineEdit::editingFinished, this, [this]() {
        if (!world_ || !world_->alive(selected_)) {
            return;
        }
        const std::string value = nameEdit_->text().trimmed().toStdString();
        if (value.empty()) {
            const auto* current = world_->tryGet<ObjectName>(selected_);
            nameEdit_->setText(current ? QString::fromStdString(current->value) : QString());
            return;
        }
        const auto* current = world_->tryGet<ObjectName>(selected_);
        if (current && current->value == value) {
            return;
        }
        const std::string before = current ? current->value : std::string();
        if (history_) {
            history_->perform(*world_, selected_, [this, before, value](bool forward) {
                world_->add(selected_, ObjectName{forward ? value : before});
            });
        } else {
            world_->add(selected_, ObjectName{value});
        }
        header_->setText(QStringLiteral("%1  (#%2)")
                             .arg(objectLabel(*world_, selected_))
                             .arg(selected_.index));
        Q_EMIT inspectorEdited();
    });
    nameLayout->addWidget(nameEdit_, 1);
    layout->addWidget(nameRow);

    auto* flags = new QWidget(content_);
    auto* flagLayout = new QHBoxLayout(flags);
    flagLayout->setContentsMargins(0, 0, 0, 0);
    activeCheck_ = new QCheckBox(tr("Active"), flags);
    const auto* activity = world_->tryGet<ObjectActivity>(selected_);
    activeCheck_->setChecked(!activity || activity->enabled);
    connect(activeCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        if (!world_ || !world_->alive(selected_) || updating_) {
            return;
        }
        const bool before = isActive(*world_, selected_);
        if (before == checked) {
            return;
        }
        if (history_) {
            history_->perform(*world_, selected_, [this, checked](bool forward) {
                ObjectActivity activity;
                activity.enabled = forward ? checked : !checked;
                world_->add(selected_, activity);
            });
        } else {
            world_->add(selected_, ObjectActivity{checked});
        }
        Q_EMIT inspectorEdited();
    });
    flagLayout->addWidget(activeCheck_);
    auto* layerLabel = new QLabel(tr("Layer"), flags);
    flagLayout->addWidget(layerLabel);
    auto* layer = new QComboBox(flags);
    layer->addItem(tr("Default"));
    for (int i = 1; i < 32; ++i) {
        if (!LayerSettings::names[i].empty()) {
            layer->addItem(QString::fromStdString(LayerSettings::names[i]), i);
        }
    }
    const auto* objectLayer = world_->tryGet<ObjectLayer>(selected_);
    if (objectLayer) {
        const int index = layer->findData(objectLayer->value);
        if (index >= 0) {
            layer->setCurrentIndex(index);
        }
    }
    connect(layer, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, layer](int) {
        if (!world_ || !world_->alive(selected_) || updating_) {
            return;
        }
        const int value = layer->currentData().toInt();
        const auto* current = world_->tryGet<ObjectLayer>(selected_);
        const int before = current ? current->value : 0;
        if (before == value) {
            return;
        }
        if (history_) {
            history_->perform(*world_, selected_, [this, before, value](bool forward) {
                world_->add(selected_, ObjectLayer{forward ? value : before});
            });
        } else {
            world_->add(selected_, ObjectLayer{value});
        }
        Q_EMIT inspectorEdited();
    });
    flagLayout->addWidget(layer, 1);
    layout->addWidget(flags);
    auto* separator = new QFrame(content_);
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet(QStringLiteral("color: rgba(255,255,255,0.12);"));
    layout->addWidget(separator);
}

void InspectorPanel::rebuildTransform(QVBoxLayout* layout) {
    auto* section = new ComponentSection(tr("Transform"), content_);
    auto* rows = new QWidget(section);
    auto* rowLayout = new QVBoxLayout(rows);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    rowLayout->setSpacing(2);

    auto& transform = world_->get<Transform>(selected_);
    auto* positionLabel = new QLabel(tr("Position"), rows);
    rowLayout->addWidget(positionLabel);
    position_ = new Vector3Edit(rows);
    position_->setSpeed(0.02f);
    position_->set(transform.position);
    rowLayout->addWidget(position_);
    connect(position_, &Vector3Edit::edited, this, [this](const Vec3& value) {
        if (!world_ || !world_->alive(selected_)) {
            return;
        }
        if (!transformUndo_) {
            return;
        }
        if (!transformUndoActive()) {
            transformUndo_->begin(*world_, selected_, *history_);
            setTransformUndoActive(true);
        }
        world_->get<Transform>(selected_).position = value;
        Q_EMIT inspectorEdited();
    });

    auto* rotationLabel = new QLabel(tr("Rotation (degrees)"), rows);
    rowLayout->addWidget(rotationLabel);
    rotation_ = new Vector3Edit(rows);
    rotation_->setSpeed(0.5f);
    rotation_->set(rotationToEuler(transform.rotation));
    rowLayout->addWidget(rotation_);
    connect(rotation_, &Vector3Edit::edited, this, [this](const Vec3& value) {
        if (!world_ || !world_->alive(selected_)) {
            return;
        }
        if (!transformUndoActive()) {
            transformUndo_->begin(*world_, selected_, *history_);
            setTransformUndoActive(true);
        }
        world_->get<Transform>(selected_).rotation = rotationFromEuler(value);
        Q_EMIT inspectorEdited();
    });

    auto* scaleLabel = new QLabel(tr("Scale"), rows);
    rowLayout->addWidget(scaleLabel);
    scale_ = new Vector3Edit(rows);
    scale_->setSpeed(0.01f);
    scale_->set(transform.scale);
    rowLayout->addWidget(scale_);
    connect(scale_, &Vector3Edit::edited, this, [this](const Vec3& value) {
        if (!world_ || !world_->alive(selected_)) {
            return;
        }
        if (!transformUndoActive()) {
            transformUndo_->begin(*world_, selected_, *history_);
            setTransformUndoActive(true);
        }
        world_->get<Transform>(selected_).scale = value;
        Q_EMIT inspectorEdited();
    });

    staticCheck_ = new QCheckBox(tr("Static (eligible for lightmapping)"), rows);
    staticCheck_->setChecked(transform.isStatic);
    connect(staticCheck_, &QCheckBox::toggled, this, [this](bool checked) {
        if (!world_ || !world_->alive(selected_) || updating_) {
            return;
        }
        auto& live = world_->get<Transform>(selected_);
        if (live.isStatic == checked) {
            return;
        }
        const bool before = live.isStatic;
        if (history_) {
            history_->perform(*world_, selected_, [this, before, checked](bool forward) {
                world_->get<Transform>(selected_).isStatic = forward ? checked : before;
            });
        } else {
            live.isStatic = checked;
        }
        Q_EMIT inspectorEdited();
    });
    rowLayout->addWidget(staticCheck_);
    section->addRows(rows);
    layout->addWidget(section);
}

void InspectorPanel::addComponentSections(QVBoxLayout* layout) {
    World& world = *world_;
    const Entity entity = selected_;

    // Mesh Renderer ---------------------------------------------------------
    if (auto* mesh = world.tryGet<MeshRenderer>(entity)) {
        auto* section = new ComponentSection(tr("Mesh Renderer"), content_);
        auto* rows = new QWidget(section);
        auto* grid = new QVBoxLayout(rows);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(2);
        auto* visible = new QCheckBox(tr("Visible content"), rows);
        visible->setChecked(true);
        grid->addWidget(visible);

        auto* color = new ColorEdit(tr("Colour"), rows);
        color->set(mesh->color);
        connect(color, &ColorEdit::edited, this, [this](const Vec3& value) {
            if (auto* live = world_ ? world_->tryGet<MeshRenderer>(selected_) : nullptr) {
                live->color = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(color);

        auto* specular = new SliderEdit(tr("Specular"), rows);
        specular->setRange(0, 2);
        specular->setStep(0.01f);
        specular->set(mesh->specular);
        connect(specular, &SliderEdit::edited, this, [this](float value) {
            if (auto* live = world_ ? world_->tryGet<MeshRenderer>(selected_) : nullptr) {
                live->specular = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(specular);

        if (auto* checker = new CheckRow(tr("Checkerboard"), rows)) {
            checker->set(mesh->checkerboard);
            connect(checker, &CheckRow::edited, this, [this](bool value) {
                if (auto* live = world_ ? world_->tryGet<MeshRenderer>(selected_) : nullptr) {
                    live->checkerboard = value;
                    Q_EMIT inspectorEdited();
                }
            });
            grid->addWidget(checker);
        }

        auto* model = new TextRow(tr("Model"), tr("Built-in primitive"), rows);
        model->set(QString::fromStdString(mesh->model));
        model->setBrowse(true);
        connect(model, &TextRow::edited, this, [this](const QString& text) {
            if (auto* live = world_ ? world_->tryGet<MeshRenderer>(selected_) : nullptr) {
                live->model = text.toStdString();
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(model);

        auto* material = new TextRow(tr("Material"), tr("No material (.mat)"), rows);
        material->set(QString::fromStdString(mesh->material));
        connect(material, &TextRow::edited, this, [this](const QString& text) {
            if (auto* live = world_ ? world_->tryGet<MeshRenderer>(selected_) : nullptr) {
                live->material = text.toStdString();
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(material);

        auto* remove = new QPushButton(tr("Remove Mesh Renderer"), rows);
        connect(remove, &QPushButton::clicked, this, [this]() {
            if (!world_ || !world_->alive(selected_)) {
                return;
            }
            if (history_) {
                history_->perform(*world_, selected_, [this](bool forward) {
                    if (forward) {
                        world_->remove<MeshRenderer>(selected_);
                    }
                });
            } else {
                world_->remove<MeshRenderer>(selected_);
            }
            builtRevision_ = ~0u;
            Q_EMIT inspectorEdited();
        });
        grid->addWidget(remove);
        section->addRows(rows);
        layout->addWidget(section);
    }

    // Collider --------------------------------------------------------------
    if (auto* collider = world.tryGet<Collider>(entity)) {
        auto* section = new ComponentSection(tr("Collider"), content_);
        auto* rows = new QWidget(section);
        auto* grid = new QVBoxLayout(rows);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(2);
        auto* shape = new ComboRow(tr("Shape"), rows);
        shape->setItems({tr("Box"), tr("Sphere"), tr("Capsule"), tr("Cone"), tr("Mesh")});
        shape->setCurrent(static_cast<int>(collider->shape));
        connect(shape, &ComboRow::edited, this, [this](int index) {
            if (auto* live = world_ ? world_->tryGet<Collider>(selected_) : nullptr) {
                live->shape = static_cast<ColliderShape>(index);
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(shape);
        auto* size = new Vector3Edit(rows);
        size->set(collider->size);
        connect(size, &Vector3Edit::edited, this, [this](const Vec3& value) {
            if (auto* live = world_ ? world_->tryGet<Collider>(selected_) : nullptr) {
                live->size = value;
                Q_EMIT inspectorEdited();
            }
        });
        auto* sizeLabel = new QLabel(tr("Size"), rows);
        grid->addWidget(sizeLabel);
        grid->addWidget(size);
        auto* center = new Vector3Edit(rows);
        center->set(collider->center);
        connect(center, &Vector3Edit::edited, this, [this](const Vec3& value) {
            if (auto* live = world_ ? world_->tryGet<Collider>(selected_) : nullptr) {
                live->center = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(new QLabel(tr("Centre"), rows));
        grid->addWidget(center);
        auto* friction = new SliderEdit(tr("Friction"), rows);
        friction->setRange(0, 1);
        friction->setStep(0.01f);
        friction->set(collider->friction);
        connect(friction, &SliderEdit::edited, this, [this](float value) {
            if (auto* live = world_ ? world_->tryGet<Collider>(selected_) : nullptr) {
                live->friction = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(friction);
        auto* bounciness = new SliderEdit(tr("Bounciness"), rows);
        bounciness->setRange(0, 1);
        bounciness->setStep(0.01f);
        bounciness->set(collider->bounciness);
        connect(bounciness, &SliderEdit::edited, this, [this](float value) {
            if (auto* live = world_ ? world_->tryGet<Collider>(selected_) : nullptr) {
                live->bounciness = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(bounciness);
        auto* trigger = new CheckRow(tr("Trigger (no collision response)"), rows);
        trigger->set(collider->isTrigger);
        connect(trigger, &CheckRow::edited, this, [this](bool value) {
            if (auto* live = world_ ? world_->tryGet<Collider>(selected_) : nullptr) {
                live->isTrigger = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(trigger);
        auto* remove = new QPushButton(tr("Remove Collider"), rows);
        connect(remove, &QPushButton::clicked, this, [this]() {
            if (!world_ || !world_->alive(selected_)) {
                return;
            }
            world_->remove<Collider>(selected_);
            builtRevision_ = ~0u;
            Q_EMIT inspectorEdited();
        });
        grid->addWidget(remove);
        section->addRows(rows);
        layout->addWidget(section);
    }

    // Rigid Body ------------------------------------------------------------
    if (auto* body = world.tryGet<RigidBody>(entity)) {
        auto* section = new ComponentSection(tr("Rigid Body"), content_);
        auto* rows = new QWidget(section);
        auto* grid = new QVBoxLayout(rows);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(2);
        auto* mass = new FloatEdit(tr("Mass (kg)"), rows);
        mass->setRange(0, 100000);
        mass->setStep(0.1f);
        mass->set(body->mass);
        connect(mass, &FloatEdit::edited, this, [this](float value) {
            if (auto* live = world_ ? world_->tryGet<RigidBody>(selected_) : nullptr) {
                live->mass = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(mass);
        auto* gravity = new CheckRow(tr("Use gravity"), rows);
        gravity->set(body->useGravity);
        connect(gravity, &CheckRow::edited, this, [this](bool value) {
            if (auto* live = world_ ? world_->tryGet<RigidBody>(selected_) : nullptr) {
                live->useGravity = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(gravity);
        auto* kinematic = new CheckRow(tr("Kinematic"), rows);
        kinematic->set(body->isKinematic);
        connect(kinematic, &CheckRow::edited, this, [this](bool value) {
            if (auto* live = world_ ? world_->tryGet<RigidBody>(selected_) : nullptr) {
                live->isKinematic = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(kinematic);
        auto* drag = new SliderEdit(tr("Drag"), rows);
        drag->setRange(0, 20);
        drag->setStep(0.05f);
        drag->set(body->drag);
        connect(drag, &SliderEdit::edited, this, [this](float value) {
            if (auto* live = world_ ? world_->tryGet<RigidBody>(selected_) : nullptr) {
                live->drag = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(drag);
        auto* angularDrag = new SliderEdit(tr("Angular drag"), rows);
        angularDrag->setRange(0, 20);
        angularDrag->setStep(0.05f);
        angularDrag->set(body->angularDrag);
        connect(angularDrag, &SliderEdit::edited, this, [this](float value) {
            if (auto* live = world_ ? world_->tryGet<RigidBody>(selected_) : nullptr) {
                live->angularDrag = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(angularDrag);
        auto* remove = new QPushButton(tr("Remove Rigid Body"), rows);
        connect(remove, &QPushButton::clicked, this, [this]() {
            if (!world_ || !world_->alive(selected_)) {
                return;
            }
            world_->remove<RigidBody>(selected_);
            builtRevision_ = ~0u;
            Q_EMIT inspectorEdited();
        });
        grid->addWidget(remove);
        section->addRows(rows);
        layout->addWidget(section);
    }

    // Camera ----------------------------------------------------------------
    if (auto* camera = world.tryGet<Camera>(entity)) {
        auto* section = new ComponentSection(tr("Camera"), content_);
        auto* rows = new QWidget(section);
        auto* grid = new QVBoxLayout(rows);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(2);
        auto* active = new QCheckBox(tr("Make this the active camera"), rows);
        active->setChecked(camera->active);
        connect(active, &QCheckBox::toggled, this, [this](bool checked) {
            if (!world_ || !world_->alive(selected_) || updating_) {
                return;
            }
            world_->each<Camera>([&](Entity other, Camera& otherCamera) {
                if (other != selected_) {
                    otherCamera.active = false;
                }
            });
            world_->get<Camera>(selected_).active = checked;
            Q_EMIT inspectorEdited();
        });
        grid->addWidget(active);
        auto* ortho = new CheckRow(tr("Orthographic"), rows);
        ortho->set(camera->orthographic);
        connect(ortho, &CheckRow::edited, this, [this](bool value) {
            if (auto* live = world_ ? world_->tryGet<Camera>(selected_) : nullptr) {
                live->orthographic = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(ortho);
        auto* fov = new SliderEdit(tr("Field of view"), rows);
        fov->setRange(5, 170);
        fov->setStep(0.5f);
        fov->set(camera->fieldOfView);
        connect(fov, &SliderEdit::edited, this, [this](float value) {
            if (auto* live = world_ ? world_->tryGet<Camera>(selected_) : nullptr) {
                live->fieldOfView = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(fov);
        auto* nearClip = new FloatEdit(tr("Near clip"), rows);
        nearClip->setRange(0.001f, 100);
        nearClip->setStep(0.01f);
        nearClip->set(camera->nearClip);
        connect(nearClip, &FloatEdit::edited, this, [this](float value) {
            if (auto* live = world_ ? world_->tryGet<Camera>(selected_) : nullptr) {
                live->nearClip = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(nearClip);
        // The engine renders what this camera sees; the panel only displays the
        // read-back picture, so nothing here touches the GL context.
        if (!cameraPreview_) {
            cameraPreview_ = new PreviewView(rows);
            cameraPreview_->setMinimumHeight(140);
            cameraPreview_->setToolTip(tr("What the selected camera sees"));
        }
        cameraPreview_->setImage(cameraPreviewImage_);
        grid->addWidget(cameraPreview_);
        auto* farClip = new FloatEdit(tr("Far clip"), rows);
        farClip->setRange(1, 100000);
        farClip->setStep(1.0f);
        farClip->set(camera->farClip);
        connect(farClip, &FloatEdit::edited, this, [this](float value) {
            if (auto* live = world_ ? world_->tryGet<Camera>(selected_) : nullptr) {
                live->farClip = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(farClip);
        auto* size = new FloatEdit(tr("Orthographic size"), rows);
        size->setRange(0.1f, 10000);
        size->setStep(0.1f);
        size->set(camera->orthographicSize);
        connect(size, &FloatEdit::edited, this, [this](float value) {
            if (auto* live = world_ ? world_->tryGet<Camera>(selected_) : nullptr) {
                live->orthographicSize = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(size);
        auto* shake = new SliderEdit(tr("Shake intensity"), rows);
        shake->setRange(0, 5);
        shake->setStep(0.01f);
        shake->set(camera->shakeIntensity);
        connect(shake, &SliderEdit::edited, this, [this](float value) {
            if (auto* live = world_ ? world_->tryGet<Camera>(selected_) : nullptr) {
                live->shakeIntensity = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(shake);
        section->addRows(rows);
        layout->addWidget(section);
    }

    // Audio Source ----------------------------------------------------------
    if (auto* source = world.tryGet<AudioSource>(entity)) {
        auto* section = new ComponentSection(tr("Audio Source"), content_);
        auto* rows = new QWidget(section);
        auto* grid = new QVBoxLayout(rows);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(2);
        auto* clip = new TextRow(tr("Clip"), tr("Project-relative audio asset"), rows);
        clip->set(QString::fromStdString(source->clip));
        clip->setBrowse(true);
        connect(clip, &TextRow::edited, this, [this](const QString& text) {
            if (auto* live = world_ ? world_->tryGet<AudioSource>(selected_) : nullptr) {
                live->clip = text.toStdString();
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(clip);
        auto* volume = new SliderEdit(tr("Volume"), rows);
        volume->setRange(0, 1);
        volume->setStep(0.01f);
        volume->set(source->volume);
        connect(volume, &SliderEdit::edited, this, [this](float value) {
            if (auto* live = world_ ? world_->tryGet<AudioSource>(selected_) : nullptr) {
                live->volume = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(volume);
        auto* loop = new CheckRow(tr("Loop"), rows);
        loop->set(source->looping);
        connect(loop, &CheckRow::edited, this, [this](bool value) {
            if (auto* live = world_ ? world_->tryGet<AudioSource>(selected_) : nullptr) {
                live->looping = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(loop);
        auto* playOnStart = new CheckRow(tr("Play on start"), rows);
        playOnStart->set(source->playOnStart);
        connect(playOnStart, &CheckRow::edited, this, [this](bool value) {
            if (auto* live = world_ ? world_->tryGet<AudioSource>(selected_) : nullptr) {
                live->playOnStart = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(playOnStart);
        auto* muted = new CheckRow(tr("Muted"), rows);
        muted->set(source->muted);
        connect(muted, &CheckRow::edited, this, [this](bool value) {
            if (auto* live = world_ ? world_->tryGet<AudioSource>(selected_) : nullptr) {
                live->muted = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(muted);
        auto* pitch = new SliderEdit(tr("Pitch"), rows);
        pitch->setRange(0.1f, 4);
        pitch->setStep(0.01f);
        pitch->set(source->pitch);
        connect(pitch, &SliderEdit::edited, this, [this](float value) {
            if (auto* live = world_ ? world_->tryGet<AudioSource>(selected_) : nullptr) {
                live->pitch = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(pitch);
        section->addRows(rows);
        layout->addWidget(section);
    }

    // Lights ----------------------------------------------------------------
    const QString kind = lightKind(world, entity);
    if (!kind.isEmpty()) {
        auto* settings = world.has<DirectionalLight>(entity)
                             ? static_cast<LightSettings*>(&world.get<DirectionalLight>(entity))
                         : world.has<PointLight>(entity)
                             ? static_cast<LightSettings*>(&world.get<PointLight>(entity))
                             : static_cast<LightSettings*>(&world.get<SpotLight>(entity));
        auto* section = new ComponentSection(kind, content_);
        auto* rows = new QWidget(section);
        auto* grid = new QVBoxLayout(rows);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(2);
        auto* colour = new ColorEdit(tr("Colour"), rows);
        colour->set(settings->color);
        connect(colour, &ColorEdit::edited, this, [this](const Vec3& value) {
            if (auto* live = world_ ? world_->tryGet<DirectionalLight>(selected_) : nullptr) {
                live->color = value;
                Q_EMIT inspectorEdited();
            } else if (auto* point = world_ ? world_->tryGet<PointLight>(selected_) : nullptr) {
                point->color = value;
                Q_EMIT inspectorEdited();
            } else if (auto* spot = world_ ? world_->tryGet<SpotLight>(selected_) : nullptr) {
                spot->color = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(colour);
        auto* intensity = new SliderEdit(tr("Intensity"), rows);
        intensity->setRange(0, 40);
        intensity->setStep(0.05f);
        intensity->set(settings->intensity);
        connect(intensity, &SliderEdit::edited, this, [this](float value) {
            forEachLight(*world_, selected_, [value](LightSettings& light) { light.intensity = value; });
            Q_EMIT inspectorEdited();
        });
        grid->addWidget(intensity);
        auto* range = new FloatEdit(tr("Range (point/spot)"), rows);
        range->setRange(0, 10000);
        range->setStep(0.25f);
        range->set(settings->range);
        connect(range, &FloatEdit::edited, this, [this](float value) {
            forEachLight(*world_, selected_, [value](LightSettings& light) { light.range = value; });
            Q_EMIT inspectorEdited();
        });
        grid->addWidget(range);
        auto* shadows = new CheckRow(tr("Cast shadows"), rows);
        shadows->set(settings->shadows);
        connect(shadows, &CheckRow::edited, this, [this](bool value) {
            forEachLight(*world_, selected_, [value](LightSettings& light) { light.shadows = value; });
            Q_EMIT inspectorEdited();
        });
        grid->addWidget(shadows);
        auto* softness = new SliderEdit(tr("Softness"), rows);
        softness->setRange(0, 10);
        softness->setStep(0.01f);
        softness->set(settings->softness);
        connect(softness, &SliderEdit::edited, this, [this](float value) {
            forEachLight(*world_, selected_, [value](LightSettings& light) { light.softness = value; });
            Q_EMIT inspectorEdited();
        });
        grid->addWidget(softness);
        section->addRows(rows);
        layout->addWidget(section);
    }

    // Particle System -------------------------------------------------------
    if (auto* particles = world.tryGet<ParticleSystem>(entity)) {
        auto* section = new ComponentSection(tr("Particle System"), content_);
        auto* rows = new QWidget(section);
        auto* grid = new QVBoxLayout(rows);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(2);
        auto* emit = new CheckRow(tr("Emitting"), rows);
        emit->set(particles->emitting);
        connect(emit, &CheckRow::edited, this, [this](bool value) {
            if (auto* live = world_ ? world_->tryGet<ParticleSystem>(selected_) : nullptr) {
                live->emitting = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(emit);
        auto* rate = new FloatEdit(tr("Emission rate"), rows);
        rate->setRange(0, 5000);
        rate->setStep(1.0f);
        rate->set(particles->rate);
        connect(rate, &FloatEdit::edited, this, [this](float value) {
            if (auto* live = world_ ? world_->tryGet<ParticleSystem>(selected_) : nullptr) {
                live->rate = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(rate);
        auto* lifetime = new FloatEdit(tr("Lifetime (s)"), rows);
        lifetime->setRange(0.01f, 120);
        lifetime->setStep(0.05f);
        lifetime->set(particles->lifetimeMin);
        connect(lifetime, &FloatEdit::edited, this, [this](float value) {
            if (auto* live = world_ ? world_->tryGet<ParticleSystem>(selected_) : nullptr) {
                live->lifetimeMin = value;
                live->lifetimeMax = std::max(live->lifetimeMax, value);
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(lifetime);
        auto* startColour = new ColorEdit(tr("Start colour"), rows);
        startColour->set(particles->color);
        connect(startColour, &ColorEdit::edited, this, [this](const Vec3& value) {
            if (auto* live = world_ ? world_->tryGet<ParticleSystem>(selected_) : nullptr) {
                live->color = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(startColour);
        auto* endColour = new ColorEdit(tr("End colour"), rows);
        endColour->set(particles->endColor);
        connect(endColour, &ColorEdit::edited, this, [this](const Vec3& value) {
            if (auto* live = world_ ? world_->tryGet<ParticleSystem>(selected_) : nullptr) {
                live->endColor = value;
                Q_EMIT inspectorEdited();
            }
        });
        grid->addWidget(endColour);
        section->addRows(rows);
        layout->addWidget(section);
    }

    // Scripts ---------------------------------------------------------------
    if (auto* scripts = world.tryGet<LuaScript>(entity)) {
        auto* section = new ComponentSection(tr("Scripts"), content_);
        auto* rows = new QWidget(section);
        auto* grid = new QVBoxLayout(rows);
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setSpacing(2);
        const auto attachments = scriptAttachments(*scripts);
        if (attachments.empty()) {
            auto* hint = new QLabel(tr("No scripts attached. Drop a .ls file here."), rows);
            hint->setStyleSheet(QStringLiteral("color: #7f8ea0;"));
            hint->setWordWrap(true);
            grid->addWidget(hint);
        }
        for (const auto& attachment : attachments) {
            auto* row = new QWidget(rows);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            auto* enabled = new QCheckBox(row);
            enabled->setChecked(attachment.enabled);
            rowLayout->addWidget(enabled);
            auto* path = new QLabel(QString::fromStdString(attachment.path), row);
            path->setToolTip(QString::fromStdString(attachment.path));
            rowLayout->addWidget(path, 1);
            auto* open = new QToolButton(row);
            open->setText(tr("Open"));
            const std::string scriptPath = attachment.path;
            connect(open, &QToolButton::clicked, this, [this, scriptPath]() {
                if (browser_) {
                    browser_->openedScript = scriptPath;
                }
            });
            rowLayout->addWidget(open);
            grid->addWidget(row);
        }
        section->addRows(rows);
        layout->addWidget(section);
    }
}

void InspectorPanel::refreshValues() {
    if (!world_ || !world_->alive(selected_)) {
        return;
    }
    updating_ = true;
    const auto& transform = world_->get<Transform>(selected_);
    if (position_ && !position_->hasFocus()) {
        position_->set(transform.position);
    }
    if (rotation_ && !rotation_->hasFocus()) {
        rotation_->set(rotationToEuler(transform.rotation));
    }
    if (scale_ && !scale_->hasFocus()) {
        scale_->set(transform.scale);
    }
    if (staticCheck_) {
        const QSignalBlocker block(staticCheck_);
        staticCheck_->setChecked(transform.isStatic);
    }
    updating_ = false;
    // A transform gesture ends when the pointer is released, so the history
    // entry is written once instead of on every frame of the drag.
    if (transformUndoActive() && !transform_->dragging() && !anySpinFocused()) {
        transformUndo_->end(*world_);
        setTransformUndoActive(false);
    }
}

bool InspectorPanel::anySpinFocused() const {
    for (auto* field : {position_, rotation_, scale_}) {
        if (!field) {
            continue;
        }
        for (auto* spin : field->fields()) {
            if (spin && spin->hasFocus()) {
                return true;
            }
        }
    }
    if (auto* focused = QApplication::focusWidget()) {
        return qobject_cast<QAbstractSpinBox*>(focused) != nullptr;
    }
    return false;
}

} // namespace qt
} // namespace ui
