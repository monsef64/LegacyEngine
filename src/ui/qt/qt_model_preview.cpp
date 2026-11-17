#include "ui/qt/qt_panels.hpp"

#include "core/ecs.hpp"
#include "editor/asset_browser.hpp"
#include "editor/model_preview_window.hpp"
#include "rendering/renderer.hpp"
#include "rendering/scene_surface.hpp"
#include "ui/qt/qt_framebuffer.hpp"
#include "ui/qt/qt_preview_view.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

namespace ui {
namespace qt {

ModelPreviewDialog::ModelPreviewDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("ModelPreviewDialog"));
    setWindowTitle(tr("Model Preview"));
    setAttribute(Qt::WA_DeleteOnClose, false);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(metrics::spacing);

    view_ = new PreviewView(this);
    view_->setMinimumSize(480, 270);
    layout->addWidget(view_, 1);

    auto* clipRow = new QHBoxLayout();
    clipRow->addWidget(new QLabel(tr("Animation"), this));
    clip_ = new QComboBox(this);
    clipRow->addWidget(clip_, 1);
    loop_ = new QCheckBox(tr("Loop"), this);
    clipRow->addWidget(loop_);
    blend_ = new QCheckBox(tr("Blend clip changes"), this);
    clipRow->addWidget(blend_);
    layout->addLayout(clipRow);

    fade_ = new FloatEdit(tr("Cross-fade"), this);
    fade_->setRange(0.0f, 5.0f);
    fade_->setStep(0.05f);
    fade_->setSuffix(tr(" s"));
    layout->addWidget(fade_);

    auto* timeRow = new QHBoxLayout();
    time_ = new QSlider(Qt::Horizontal, this);
    time_->setRange(0, 1000);
    timeRow->addWidget(time_, 1);
    duration_ = new QLabel(this);
    timeRow->addWidget(duration_);
    layout->addLayout(timeRow);

    auto* buttons = new QHBoxLayout();
    play_ = new QToolButton(this);
    play_->setText(tr("Play"));
    auto* pause = new QToolButton(this);
    pause->setText(tr("Pause"));
    auto* stop = new QToolButton(this);
    stop->setText(tr("Stop"));
    auto* reset = new QToolButton(this);
    reset->setText(tr("Reset"));
    for (auto* button : {play_, pause, stop, reset}) {
        buttons->addWidget(button);
    }
    buttons->addStretch(1);
    layout->addLayout(buttons);

    error_ = new QLabel(this);
    error_->setWordWrap(true);
    error_->setStyleSheet(QStringLiteral("color: #e07b7b;"));
    layout->addWidget(error_);
    auto* hint = new QLabel(tr("Right-drag to look, scroll to zoom. Reset restores the bind pose."),
                            this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color: #8fa3bb;"));
    layout->addWidget(hint);

    connect(play_, &QToolButton::clicked, this, [this]() {
        if (window_) {
            window_->play();
        }
    });
    connect(pause, &QToolButton::clicked, this, [this]() {
        if (window_) {
            window_->pause();
        }
    });
    connect(stop, &QToolButton::clicked, this, [this]() {
        if (window_) {
            window_->stop();
        }
    });
    connect(reset, &QToolButton::clicked, this, [this]() {
        if (window_) {
            window_->resetPose();
        }
    });
    connect(clip_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (loading_ || !window_) {
            return;
        }
        window_->setClip(index);
        sync();
    });
    connect(time_, &QSlider::valueChanged, this, [this](int value) {
        if (loading_ || !window_) {
            return;
        }
        window_->setTime(float(value) / 1000.0f * std::max(window_->clipDuration(window_->clip()),
                                                            .001f));
    });
    connect(loop_, &QCheckBox::toggled, this, [this](bool checked) {
        if (window_) {
            window_->setLoop(checked);
        }
    });
    connect(blend_, &QCheckBox::toggled, this, [this](bool checked) {
        if (window_) {
            window_->setBlendClips(checked);
        }
    });
    connect(fade_, &FloatEdit::edited, this, [this](float value) {
        if (window_) {
            window_->setFadeSeconds(value);
        }
    });
}

void ModelPreviewDialog::bind(AssetManager& assets0,
                              Renderer& renderer0,
                              ModelPreviewWindow& window0) {
    assets_ = &assets0;
    renderer_ = &renderer0;
    window_ = &window0;
}

void ModelPreviewDialog::openPath(const QString& path) {
    if (!window_ || !assets_ || !renderer_) {
        return;
    }
    window_->open(*assets_, path.toStdString(), *renderer_);
    setWindowTitle(tr("Model Preview - %1").arg(path));
    show();
    raise();
    sync();
}

void ModelPreviewDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    if (window_) {
        window_->visible = true;
    }
}

void ModelPreviewDialog::closeEvent(QCloseEvent* event) {
    if (window_) {
        window_->visible = false;
    }
    Q_EMIT closed();
    QDialog::closeEvent(event);
}

void ModelPreviewDialog::refresh(float dt) {
    if (!window_ || !assets_ || !renderer_ || !isVisible()) {
        return;
    }
    // The engine renders into its own framebuffer; copy it out only when the
    // preview actually redrew, so scrubbing does not read the GPU every frame.
    const unsigned before = window_->renderedRevision();
    window_->update(*assets_, *renderer_, dt);
    if (window_->renderedRevision() != before) {
        dirty_ = true;
    }
    if (!window_->visible) {
        hide();
        return;
    }
    if (!window_->loaded()) {
        view_->setImage(QImage());
    } else if (dirty_) {
        view_->setImage(readFramebuffer(window_->surface().framebuffer,
                                        window_->surface().width,
                                        window_->surface().height));
        dirty_ = false;
    }
    view_->setOrbitCamera(&window_->camera());
    sync();
}

void ModelPreviewDialog::sync() {
    if (!window_) {
        return;
    }
    loading_ = true;
    const int count = window_->clipCount();
    if (clip_->count() != count) {
        clip_->clear();
        for (int i = 0; i < count; ++i) {
            clip_->addItem(QString::fromStdString(window_->clipName(i)));
        }
    }
    if (count > 0) {
        clip_->setCurrentIndex(std::clamp(window_->clip(), 0, count - 1));
    }
    clip_->setEnabled(count > 0);
    play_->setEnabled(count > 0);
    const float duration = window_->clipDuration(window_->clip());
    time_->setEnabled(count > 0 && duration > 0);
    time_->setValue(duration > 0
                        ? int(std::clamp(window_->time() / duration, 0.0f, 1.0f) * 1000.0f)
                        : 0);
    duration_->setText(count > 0
                           ? tr("%1 / %2 s").arg(window_->time(), 0, 'f', 2).arg(duration, 0, 'f', 2)
                           : tr("No animations"));
    loop_->setChecked(window_->loop());
    blend_->setChecked(window_->blendClips());
    fade_->set(window_->fadeSeconds());
    error_->setText(QString::fromStdString(window_->error()));
    loading_ = false;
}

} // namespace qt
} // namespace ui
