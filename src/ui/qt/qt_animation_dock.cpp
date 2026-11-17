#include "ui/qt/qt_panels.hpp"

#include "assets/assets.hpp"
#include "animation/animation_property.hpp"
#include "editor/animation_window.hpp"
#include "editor/asset_browser.hpp"
#include "editor/editor_history.hpp"
#include "ui/qt/qt_framebuffer.hpp"
#include "ui/qt/qt_preview_view.hpp"
#include "ui/qt/qt_widgets.hpp"

#include <QCheckBox>
#include <QGroupBox>
#include <QMouseEvent>
#include <QSignalBlocker>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>

namespace ui {
namespace qt {

namespace {

constexpr int kRuler = 18;
} // namespace

// Declared in qt_panels.hpp as a plain QWidget*;
// Timeline: ruler + key diamonds. Click the ruler to scrub, drag a diamond to
// retime, double-click a lane to capture a key.
struct AnimationTimeline : public QWidget {
    explicit AnimationTimeline(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(110);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
    }

    std::function<void(float)> onScrub;
    std::function<void(int, int, float)> onRetime;
    std::function<void(int, int)> onSelectKey;
    std::function<void(int)> onCaptureKey;

    void setClip(const AnimationClip& clip, float time, int track, int key) {
        clip_ = clip;
        time_ = time;
        track_ = track;
        key_ = key;
        update();
    }

  protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

  private:
    float timeAt(int x) const {
        return std::clamp(float(x) / std::max(1, width() - 1), 0.0f, 1.0f) *
               std::max(clip_.duration, .001f);
    }
    int xAt(float seconds) const {
        return int(std::clamp(seconds / std::max(clip_.duration, .001f), 0.0f, 1.0f) *
                   float(width() - 1));
    }
    int laneAt(int y) const {
        const int lanes = std::max(1, int(clip_.tracks.size()));
        const int laneHeight = std::max(10, (height() - kRuler) / lanes);
        return y < kRuler ? -1 : std::min((y - kRuler) / laneHeight, lanes - 1);
    }
    AnimationClip clip_{};
    float time_ = 0;
    int track_ = -1, key_ = -1;
    int draggingTrack_ = -1, draggingKey_ = -1;
    bool scrubbing_ = false;
};

void AnimationTimeline::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(24, 27, 32));
    const int lanes = std::max(1, int(clip_.tracks.size()));
    const int laneHeight = std::max(10, (height() - kRuler) / lanes);
    painter.setPen(QColor(70, 78, 92));
    painter.drawLine(0, kRuler, width(), kRuler);
    painter.setPen(QColor(140, 154, 172));
    for (int i = 0; i <= 10; ++i) {
        const int x = i * (width() - 1) / 10;
        painter.drawLine(x, kRuler - 4, x, kRuler);
        painter.drawText(QRect(x - 20, 0, 40, kRuler - 4), Qt::AlignCenter,
                         QString::number(clip_.duration * i / 10.0, 'f', 2));
    }
    for (int lane = 0; lane < int(clip_.tracks.size()); ++lane) {
        const auto& track = clip_.tracks[std::size_t(lane)];
        const int y = kRuler + lane * laneHeight;
        if (lane == track_) {
            painter.fillRect(QRect(0, y, width(), laneHeight), QColor(52, 74, 104, 120));
        }
        painter.setPen(QColor(150, 164, 184));
        painter.drawText(QRect(2, y, 140, laneHeight), Qt::AlignVCenter | Qt::AlignLeft,
                         QString::fromStdString(track.property));
        for (int k = 0; k < int(track.keys.size()); ++k) {
            const int x = xAt(track.keys[std::size_t(k)].time);
            const int mid = y + laneHeight / 2;
            QPolygon diamond;
            diamond << QPoint(x, mid - 4) << QPoint(x + 4, mid) << QPoint(x, mid + 4)
                    << QPoint(x - 4, mid);
            painter.setPen(Qt::NoPen);
            painter.setBrush(lane == track_ && k == key_ ? QColor(255, 214, 96)
                                                         : QColor(140, 200, 255));
            painter.drawPolygon(diamond);
        }
    }
    if (clip_.tracks.empty()) {
        painter.setPen(QColor(122, 138, 158));
        painter.drawText(QRect(0, kRuler, width(), height() - kRuler), Qt::AlignCenter,
                         tr("Add a property track to start keying"));
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(255, 110, 96));
    painter.drawRect(QRect(xAt(time_), 0, 2, height()));
}

void AnimationTimeline::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QPoint pos = event->position().toPoint();
    const int lane = laneAt(pos.y());
    if (lane >= 0 && lane < int(clip_.tracks.size())) {
        const auto& keys = clip_.tracks[std::size_t(lane)].keys;
        for (int k = 0; k < int(keys.size()); ++k) {
            if (std::abs(xAt(keys[std::size_t(k)].time) - pos.x()) <= 5) {
                if (onSelectKey) {
                    onSelectKey(lane, k);
                }
                draggingTrack_ = lane;
                draggingKey_ = k;
                return;
            }
        }
    }
    scrubbing_ = true;
    if (onScrub) {
        onScrub(timeAt(pos.x()));
    }
}

void AnimationTimeline::mouseMoveEvent(QMouseEvent* event) {
    const QPoint pos = event->position().toPoint();
    if (draggingKey_ >= 0) {
        if (onRetime) {
            onRetime(draggingTrack_, draggingKey_, timeAt(pos.x()));
        }
        return;
    }
    if (scrubbing_ && onScrub) {
        onScrub(timeAt(pos.x()));
    }
    QWidget::mouseMoveEvent(event);
}

void AnimationTimeline::mouseReleaseEvent(QMouseEvent* event) {
    draggingTrack_ = draggingKey_ = -1;
    scrubbing_ = false;
    QWidget::mouseReleaseEvent(event);
}

void AnimationTimeline::mouseDoubleClickEvent(QMouseEvent* event) {
    const int lane = laneAt(event->position().toPoint().y());
    if (lane >= 0 && onCaptureKey) {
        onCaptureKey(lane);
    } else {
        QWidget::mouseDoubleClickEvent(event);
    }
}

AnimationDock::AnimationDock(QWidget* parent) : QDockWidget(tr("Animation"), parent) {
    setObjectName(QStringLiteral("AnimationDock"));
    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setFrameShape(QFrame::NoFrame);
    content_ = new QWidget(scroll_);
    scroll_->setWidget(content_);
    setWidget(scroll_);
    setMinimumWidth(340);
    build();
}

void AnimationDock::bind(World& world0,
                         AssetBrowser& browser0,
                         EditorHistory& history0,
                         AnimationWindow& window0) {
    world_ = &world0;
    browser_ = &browser0;
    history_ = &history0;
    window_ = &window0;
    builtRevision_ = ~0u;
    refresh();
}

void AnimationDock::setSelection(Entity entity) {
    if (entity.index != selected_.index || entity.generation != selected_.generation) {
        selected_ = entity;
        builtRevision_ = ~0u;
    }
}

void AnimationDock::build() {
    auto* root = new QVBoxLayout(content_);
    root->setContentsMargins(metrics::panelPadding, 6, metrics::panelPadding, 8);
    root->setSpacing(metrics::spacing);

    pathLabel_ = new QLabel(tr("No clip open"), content_);
    pathLabel_->setWordWrap(true);
    root->addWidget(pathLabel_);
    errorLabel_ = new QLabel(content_);
    errorLabel_->setWordWrap(true);
    errorLabel_->setStyleSheet(QStringLiteral("color: #e07b7b;"));
    root->addWidget(errorLabel_);

    preview_ = new qt::PreviewView(content_);
    preview_->setMinimumHeight(150);
    root->addWidget(preview_);

    auto* buttonRow = new QHBoxLayout();
    auto makeButton = [this, buttonRow](const QString& text, auto slot) {
        auto* button = new QToolButton(content_);
        button->setText(text);
        connect(button, &QToolButton::clicked, this, slot);
        buttonRow->addWidget(button);
        return button;
    };
    auto* back = makeButton(QStringLiteral("<|"), [this]() {
        const float step = sampleStep();
        window_->setTime(window_->time() - step);
        window_->setPlaying(false);
        window_->setResetPose(false);
        syncTimeline();
    });
    Q_UNUSED(back);
    auto* playPause = makeButton(tr("Play"), [this]() {
        window_->setPlaying(!window_->playing());
        window_->setResetPose(false);
    });
    connect(playPause, &QToolButton::clicked, this, [this, playPause]() {
        playPause->setText(window_->playing() ? tr("Pause") : tr("Play"));
    });
    makeButton(QStringLiteral("|>"), [this]() {
        window_->setTime(window_->time() + sampleStep());
        window_->setPlaying(false);
        window_->setResetPose(false);
        syncTimeline();
    });
    makeButton(QStringLiteral(">|"), [this]() {
        window_->setTime(window_->clip().duration);
        window_->setPlaying(false);
        window_->setResetPose(false);
        syncTimeline();
    });
    makeButton(tr("Stop"), [this]() {
        window_->setPlaying(false);
        window_->setTime(0);
        window_->setResetPose(false);
        syncTimeline();
    });
    makeButton(tr("Reset"), [this]() {
        window_->setPlaying(false);
        window_->setTime(0);
        window_->setResetPose(true);
        syncTimeline();
    });
    makeButton(tr("Apply"), [this]() {
        if (world_ && history_) {
            window_->applyCurrentFrame(*world_, selected_, *history_);
            Q_EMIT changed();
        }
    });
    root->addLayout(buttonRow);

    auto* timeRow = new QHBoxLayout();
    loop_ = new QCheckBox(tr("Loop"), content_);
    connect(loop_, &QCheckBox::toggled, this, [this](bool checked) {
        if (!loading_ && window_) {
            window_->setLoop(checked);
        }
    });
    timeRow->addWidget(loop_);
    samples_ = new IntEdit(tr("Samples"), content_);
    samples_->setRange(1, 240);
    connect(samples_, &IntEdit::edited, this, [this](int value) {
        if (!loading_ && window_) {
            window_->setSamples(value);
            syncTimeline();
        }
    });
    timeRow->addWidget(samples_);
    timeLabel_ = new QLabel(content_);
    timeLabel_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    timeRow->addWidget(timeLabel_, 1);
    root->addLayout(timeRow);

    timelineHost_ = new QWidget(content_);
    auto* timelineLayout = new QVBoxLayout(timelineHost_);
    timelineLayout->setContentsMargins(0, 0, 0, 0);
    timeline_ = new AnimationTimeline(timelineHost_);
    timelineLayout->addWidget(timeline_);
    root->addWidget(timelineHost_);
    auto* timeline = static_cast<AnimationTimeline*>(timeline_);
    connect(timeline, &QWidget::destroyed, this, [this]() { timeline_ = nullptr; });
    timeline->onScrub = [this](float time) {
        window_->setTime(time);
        window_->setPlaying(false);
        window_->setResetPose(false);
        syncTimeline();
    };
    timeline->onSelectKey = [this](int track, int key) {
        window_->selectTrack(track);
        window_->selectKey(key);
        syncTimeline();
    };
    timeline->onRetime = [this](int track, int key, float time) {
        if (!world_) {
            return;
        }
        window_->selectTrack(track);
        window_->selectKey(key);
        // Retiming is one undo step per drag, not per mouse move.
        history_->commit(*world_, selected_);
        window_->setKeyTime(key, time);
        syncTimeline();
        Q_EMIT changed();
    };
    timeline->onCaptureKey = [this](int track) {
        window_->captureKeyAt(*world_, selected_, track);
        syncTimeline();
        Q_EMIT changed();
    };

    auto* editRow = new QHBoxLayout();
    auto* copy = new QToolButton(content_);
    copy->setText(tr("Copy keys"));
    auto* paste = new QToolButton(content_);
    paste->setText(tr("Paste keys"));
    auto* del = new QToolButton(content_);
    del->setText(tr("Delete key"));
    for (auto* button : {copy, paste, del}) {
        editRow->addWidget(button);
    }
    root->addLayout(editRow);
    connect(copy, &QToolButton::clicked, this, [this]() {
        window_->copyKeysPublic(false);
        syncTimeline();
    });
    connect(paste, &QToolButton::clicked, this, [this]() {
        window_->pasteKeysPublic();
        syncTimeline();
        Q_EMIT changed();
    });
    connect(del, &QToolButton::clicked, this, [this]() {
        window_->deleteKey(window_->key());
        syncTimeline();
        Q_EMIT changed();
    });

    auto* addRow = new QHBoxLayout();
    target_ = new QLineEdit(content_);
    target_->setPlaceholderText(tr("Child path (empty = self)"));
    addRow->addWidget(target_, 1);
    property_ = new QComboBox(content_);
    for (int i = 0; i < int(std::size(animationProperties)); ++i) {
        property_->addItem(QString::fromUtf8(animationProperties[i].name));
    }
    addRow->addWidget(property_);
    auto* add = new QToolButton(content_);
    add->setText(tr("Add track"));
    addRow->addWidget(add);
    root->addLayout(addRow);
    connect(target_, &QLineEdit::textEdited, this, [this](const QString& text) {
        window_->setDraftTarget(text.toStdString());
    });
    connect(property_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (window_) {
            window_->setPropertyIndex(index);
        }
    });
    connect(add, &QToolButton::clicked, this, [this]() {
        const std::string target = target_->text().toStdString();
        const std::string property = animationProperties[property_->currentIndex()].name;
        for (const auto& track : window_->clip().tracks) {
            if (track.target == target && track.property == property) {
                errorLabel_->setText(tr("That property already has a track."));
                return;
            }
        }
        if (window_->clip().tracks.size() >= 64) {
            errorLabel_->setText(tr("The clip already has 64 tracks."));
            return;
        }
        window_->addTrack(AnimationTrack{target, property, {}});
        syncTimeline();
        Q_EMIT changed();
    });

    auto* clipBox = new QGroupBox(tr("Clip"), content_);
    auto* clipLayout = new QVBoxLayout(clipBox);
    clipPath_ = new QComboBox(clipBox);
    connect(clipPath_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (!loading_ && index >= 0) {
            openClip(clipPath_->itemData(index).toString());
        }
    });
    clipLayout->addWidget(clipPath_);
    auto* newClip = new QPushButton(tr("New clip"), clipBox);
    connect(newClip, &QPushButton::clicked, this, [this]() {
        if (window_->hasUnsavedChanges()) {
            errorLabel_->setText(tr("Save or discard edits first."));
            return;
        }
        window_->newClip();
        syncTimeline();
        Q_EMIT changed();
    });
    clipLayout->addWidget(newClip);
    draftPath_ = new QLineEdit(QString::fromUtf8(window_->draftPath()), clipBox);
    connect(draftPath_, &QLineEdit::textEdited, this, [this](const QString& text) {
        window_->setDraftPath(text.toStdString());
    });
    clipLayout->addWidget(draftPath_);
    auto* save = new QPushButton(tr("Save clip"), clipBox);
    connect(save, &QPushButton::clicked, this, [this]() {
        if (window_->saveClip(browser_->assets)) {
            window_->setPath(window_->path());
            errorLabel_->setText(QString::fromStdString(window_->error()));
            Q_EMIT changed();
        }
        syncTimeline();
    });
    clipLayout->addWidget(save);
    auto* discard = new QPushButton(tr("Discard edits"), clipBox);
    connect(discard, &QPushButton::clicked, this, [this]() {
        window_->discardEdits(browser_->assets);
        syncTimeline();
        Q_EMIT changed();
    });
    clipLayout->addWidget(discard);
    duration_ = new FloatEdit(tr("Duration"), clipBox);
    duration_->setRange(0.001f, 86400.0f);
    duration_->setStep(0.1f);
    connect(duration_, &FloatEdit::edited, this, [this](float value) {
        if (!loading_ && window_->setDuration(value)) {
            syncTimeline();
            Q_EMIT changed();
        }
    });
    clipLayout->addWidget(duration_);
    auto* assign = new QPushButton(tr("Assign to selected object"), clipBox);
    connect(assign, &QPushButton::clicked, this, [this]() {
        if (!world_ || window_->path().empty()) {
            return;
        }
        if (!world_->alive(selected_)) {
            return;
        }
        history_->commit(*world_, selected_);
        if (!world_->has<Animation>(selected_)) {
            world_->add(selected_, Animation{});
        }
        auto& animation = world_->get<Animation>(selected_);
        animation.clip = window_->path();
        animation.loop = window_->loop();
        history_->commit(*world_, selected_);
        Q_EMIT changed();
    });
    clipLayout->addWidget(assign);
    root->addWidget(clipBox);

    root->addStretch(1);
}

void AnimationDock::refresh() {
    if (!window_) {
        return;
    }
    const unsigned revision = 1u + selected_.index;
    if (revision != builtRevision_) {
        builtRevision_ = revision;
        window_->syncSheet(browser_->assets);
        window_->resetPreview();
        // Offer every clip in the project so the dock can open one directly.
        loading_ = true;
        clipPath_->clear();
        for (const auto& entry : browser_->assets.files()) {
            if (entry.size() > 7 && entry.compare(entry.size() - 7, 7, ".leanim") == 0) {
                clipPath_->addItem(QString::fromStdString(entry), QString::fromStdString(entry));
            }
        }
        const int index = clipPath_->findData(QString::fromStdString(window_->path()));
        clipPath_->setCurrentIndex(index);
        loading_ = false;
        syncTimeline();
    }
}

void AnimationDock::update(float dt) {
    if (!window_) {
        return;
    }
    const bool wasPlaying = window_->playing();
    window_->advance(dt);
    if (wasPlaying) {
        syncTimeline();
    }
    if (window_->renderedRevision() != renderedRevision_) {
        renderedRevision_ = window_->renderedRevision();
        const auto& surface = window_->surface();
        preview_->setImage(readFramebuffer(surface.framebuffer, surface.width, surface.height));
        preview_->setOrbitCamera(&window_->cameraRef());
    }
    if (pathLabel_) {
        pathLabel_->setText(window_->path().empty()
                                ? tr("Unsaved animation")
                                : QString::fromStdString(window_->path()));
        errorLabel_->setText(QString::fromStdString(window_->error()));
    }
}

void AnimationDock::syncTimeline() {
    if (!window_ || !timeline_) {
        return;
    }
    const auto& clip = window_->clip();
    loading_ = true;
    static_cast<AnimationTimeline*>(timeline_)->setClip(clip, window_->time(), window_->track(), window_->key());
    timeLabel_->setText(tr("%1 / %2 s").arg(window_->time(), 0, 'f', 2).arg(clip.duration, 0, 'f', 2));
    loop_->setChecked(window_->loop());
    samples_->set(window_->samples());
    duration_->set(clip.duration);
    property_->setCurrentIndex(std::clamp(window_->propertyIndex(), 0,
                                          int(std::size(animationProperties)) - 1));
    target_->setText(QString::fromUtf8(window_->draftTarget()));
    loading_ = false;
    update(0.0f);
}

void AnimationDock::openClip(const QString& path) {
    if (!window_ || !browser_) {
        return;
    }
    window_->open(browser_->assets, path.toStdString());
    window_->resetPreview();
    syncTimeline();
    Q_EMIT changed();
}

void AnimationDock::closeEvent(QCloseEvent* event) {
    Q_EMIT closed();
    QDockWidget::closeEvent(event);
}

float AnimationDock::sampleStep() const {
    return 1.0f / float(std::max(1, window_->samples()));
}

} // namespace qt
} // namespace ui
