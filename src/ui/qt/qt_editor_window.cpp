#include "ui/qt/qt_editor_window.hpp"

#include "ui/qt/qt_panels.hpp"
#include "ui/qt/qt_viewport.hpp"

#include "editor/animation_window.hpp"
#include "editor/asset_browser.hpp"
#include "editor/asset_preview_renderer.hpp"
#include "core/hierarchy.hpp"
#include "scene/camera.hpp"
#include "editor/editor_history.hpp"
#include "editor/level_objects.hpp"
#include "editor/lightmapping_window.hpp"
#include "editor/model_preview_window.hpp"
#include "editor/play_mode.hpp"
#include "editor/script_editor.hpp"
#include "editor/transform_editor.hpp"
#include "rendering/renderer.hpp"
#include "terrain/terrain.hpp"
#include "terrain/terrain_editor.hpp"
#include "ui/qt/qt_widgets.hpp"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QGuiApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMainWindow>
#include <QCursor>
#include <QInputDialog>
#include <QMessageBox>
#include <QTreeWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyleFactory>
#include <QTabWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>

namespace ui {
namespace qt {

namespace {

// Layout state is one base64 blob next to the editor settings: QSettings is
// overkill for a single window geometry and pulls in a config backend.
QString layoutFile() {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty()) {
        return {};
    }
    QDir().mkpath(dir);
    return dir + QStringLiteral("/layout.dat");
}

QIcon icon(const char* name) {
    return QIcon::fromTheme(QString::fromUtf8(name));
}

// The dark palette used before the immediate-mode theme existed. Theme indexes
// match EditorSettings::theme: Legacy Dark, Grey, Unity 5 Classic, Unity Dark.
QPalette paletteFor(int theme) {
    QPalette palette;
    if (theme == 2) {
        palette.setColor(QPalette::Window, QColor(94, 94, 94));
        palette.setColor(QPalette::WindowText, QColor(235, 235, 235));
        palette.setColor(QPalette::Base, QColor(78, 78, 78));
        palette.setColor(QPalette::AlternateBase, QColor(88, 88, 88));
        palette.setColor(QPalette::Text, QColor(235, 235, 235));
        palette.setColor(QPalette::Button, QColor(104, 104, 104));
        palette.setColor(QPalette::ButtonText, QColor(235, 235, 235));
        palette.setColor(QPalette::Highlight, QColor(72, 132, 200));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        palette.setColor(QPalette::ToolTipBase, QColor(60, 60, 60));
        palette.setColor(QPalette::ToolTipText, QColor(235, 235, 235));
    } else if (theme == 3) {
        palette.setColor(QPalette::Window, QColor(56, 56, 56));
        palette.setColor(QPalette::WindowText, QColor(220, 220, 220));
        palette.setColor(QPalette::Base, QColor(40, 40, 40));
        palette.setColor(QPalette::AlternateBase, QColor(48, 48, 48));
        palette.setColor(QPalette::Text, QColor(220, 220, 220));
        palette.setColor(QPalette::Button, QColor(66, 66, 66));
        palette.setColor(QPalette::ButtonText, QColor(220, 220, 220));
        palette.setColor(QPalette::Highlight, QColor(58, 108, 168));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        palette.setColor(QPalette::ToolTipBase, QColor(48, 48, 48));
        palette.setColor(QPalette::ToolTipText, QColor(220, 220, 220));
    } else if (theme == 1) {
        palette.setColor(QPalette::Window, QColor(74, 77, 82));
        palette.setColor(QPalette::WindowText, QColor(226, 230, 236));
        palette.setColor(QPalette::Base, QColor(56, 59, 64));
        palette.setColor(QPalette::AlternateBase, QColor(64, 68, 74));
        palette.setColor(QPalette::Text, QColor(226, 230, 236));
        palette.setColor(QPalette::Button, QColor(84, 88, 94));
        palette.setColor(QPalette::ButtonText, QColor(226, 230, 236));
        palette.setColor(QPalette::Highlight, QColor(72, 132, 200));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        palette.setColor(QPalette::ToolTipBase, QColor(56, 59, 64));
        palette.setColor(QPalette::ToolTipText, QColor(226, 230, 236));
    } else {
        palette.setColor(QPalette::Window, QColor(38, 41, 46));
        palette.setColor(QPalette::WindowText, QColor(222, 227, 234));
        palette.setColor(QPalette::Base, QColor(27, 30, 34));
        palette.setColor(QPalette::AlternateBase, QColor(32, 36, 41));
        palette.setColor(QPalette::Text, QColor(222, 227, 234));
        palette.setColor(QPalette::Button, QColor(46, 51, 58));
        palette.setColor(QPalette::ButtonText, QColor(222, 227, 234));
        palette.setColor(QPalette::Highlight, QColor(64, 120, 190));
        palette.setColor(QPalette::HighlightedText, Qt::white);
        palette.setColor(QPalette::ToolTipBase, QColor(27, 30, 34));
        palette.setColor(QPalette::ToolTipText, QColor(222, 227, 234));
    }
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor(128, 134, 142));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(128, 134, 142));
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor(128, 134, 142));
    return palette;
}

} // namespace

EditorWindow::EditorWindow(SDL_Window* sdlWindow, QWidget* parent)
    : QMainWindow(parent), sdlWindow_(sdlWindow) {
    setObjectName(QStringLiteral("EditorWindow"));
    setWindowTitle(tr("LegacyEngine Editor"));
    setDockNestingEnabled(true);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks |
                   QMainWindow::GroupedDragging);
    setAcceptDrops(true);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    setStyleSheet(QStringLiteral(
        "QDockWidget::title { padding: 4px 6px; }"
        "QSplitter::handle { background: #1b1e22; }"
        "QTreeWidget, QListWidget, QTableWidget { alternate-background-color: #202429; }"
        "QToolTip { border: 1px solid #3c434c; }"));
    buildDocks();
    buildMenus();
    buildStatusBar();
    buildToolbar();
    applyTheme(0);
    // A first-run window with no saved layout would otherwise take its size
    // from the docks' size hints, which can be smaller than the 3D views need
    // to show anything. Start at a size that makes the Scene view useful.
    resize(1280, 800);
    restoreLayout();
}

EditorWindow::~EditorWindow() = default;

void EditorWindow::bind(World& world0,
                        AssetBrowser& browser0,
                        EditorHistory& history0,
                        Renderer& renderer0,
                        ScriptEditor& scripts0,
                        AnimationWindow& animation0,
                        LightmappingWindow& lighting0,
                        PlayMode& playMode0,
                        Terrains::Editor& terrain0,
                        EditorSettings& settings0,
                        InputSettings& inputSettings0,
                        InputState& input0,
                        RenderingSettings& renderingSettings0,
                        TransformEditor& transform0,
                        AssetPreviewRenderer& previews0,
                        ModelPreviewWindow& preview0,
                        const ProjectIO::Meshes& meshes0,
                        OrbitCamera& camera0) {
    world_ = &world0;
    browser_ = &browser0;
    history_ = &history0;
    renderer_ = &renderer0;
    scripts_ = &scripts0;
    animation_ = &animation0;
    lighting_ = &lighting0;
    playMode_ = &playMode0;
    terrainEditor_ = &terrain0;
    settings_ = &settings0;
    inputSettings_ = &inputSettings0;
    renderingSettings_ = &renderingSettings0;
    transform_ = &transform0;
    previews_ = &previews0;
    preview_ = &preview0;
    meshes_ = &meshes0;

    sceneView_->bind(world0, browser0, history0, renderer0, transform0, playMode0);
    // The engine renders the Scene picture and the widget navigates it, so both
    // have to read the same orbit camera or the view would drift from the
    // picture under it.
    sceneView_->useCamera(camera0);
    // The scene widget reports pointer state instead of selecting on its own:
    // picking stays engine-side, where the physics world and the ray live.
    connect(sceneView_, &Viewport::mousePressedQt, this,
            [this](const QPoint& point, int button, Qt::KeyboardModifiers modifiers) {
                updatePointer(scenePointer_, sceneView_, point, modifiers);
                scenePointer_.pressed = true;
                scenePointer_.down = true;
                scenePointer_.button = button;
            });
    connect(sceneView_, &Viewport::mouseMovedQt, this,
            [this](const QPoint& point, int buttons, Qt::KeyboardModifiers modifiers) {
                updatePointer(scenePointer_, sceneView_, point, modifiers);
                scenePointer_.down = (buttons & int(Qt::LeftButton)) != 0;
            });
    connect(sceneView_, &Viewport::mouseReleasedQt, this,
            [this](const QPoint& point, int button, Qt::KeyboardModifiers modifiers) {
                updatePointer(scenePointer_, sceneView_, point, modifiers);
                scenePointer_.released = true;
                scenePointer_.down = false;
                scenePointer_.button = button;
            });
    connect(sceneView_, &Viewport::wheelQt, this, [this](float wheel) {
        scenePointer_.wheel += wheel;
    });
    gameView_->bind(world0, renderer0, playMode0);
    hierarchy_->bind(world0, browser0, history0);
    inspector_->bind(world0, browser0, history0, renderer0, transform0);
    assets_->bind(world0, browser0, history0, renderer0);
    script_->bind(world0, browser0, scripts0, history0);
    console_->bind(scripts0);
    terrainPanel_->bind(world0, browser0, history0, terrain0);
    animationDock_->bind(world0, browser0, history0, animation0);
    lightingDock_->bind(world0, browser0, history0, lighting0, meshes0, {});
    inputDock_->bind(world0, browser0, inputSettings0, history0);
    layersDock_->bind(browser0);
    inputDock_->bindInput(input0);
    renderingDock_->bind(world0, browser0, settings0, history0, renderingSettings0);
    settingsDialog_->bind(settings0, {}, browser0);
    modelPreview_->bind(browser0.assets, renderer0, preview0);

    buildToolbar();
    applyTheme(settings0.theme);
    restoreDefaultLayout();
    refresh(0.0f);
}

void EditorWindow::buildToolbar() {
    if (!toolbar_) {
        toolbar_ = new Toolbar(this);
        connect(toolbar_, &Toolbar::createRequested, this, [this](int id) {
            actions.createObject = id;
            actions.createParent = selection();
        });
        connect(toolbar_, &Toolbar::toolChanged, this, [this](TransformTool tool) {
            sceneView_->setTool(tool);
            Gizmo::cancel();
        });
        connect(toolbar_, &Toolbar::localSpaceChanged, this, [this](bool) { Gizmo::cancel(); });
        connect(toolbar_, &Toolbar::playRequested, this, [this]() { actions.play = true; });
        connect(toolbar_, &Toolbar::pauseRequested, this, [this]() { actions.pause = true; });
        connect(toolbar_, &Toolbar::stopRequested, this, [this]() { actions.stop = true; });
        connect(toolbar_, &Toolbar::separateGameToggled, this,
                [this](bool) { actions.separateGameWindow = true; });
        connect(toolbar_, &Toolbar::fullscreenGameRequested, this,
                [this]() { actions.toggleGameFullscreen = true; });
    }
    auto* bar = new QToolBar(tr("Editor"), this);
    bar->setObjectName(QStringLiteral("EditorToolbar"));
    bar->setMovable(false);
    bar->setFloatable(false);
    bar->addWidget(toolbar_);
    addToolBar(Qt::TopToolBarArea, bar);
}

void EditorWindow::buildDocks() {
    sceneView_ = new SceneView(this);
    gameView_ = new GameView(this);
    hierarchy_ = new HierarchyPanel(this);
    inspector_ = new InspectorPanel(this);
    assets_ = new AssetPanel(this);
    script_ = new ScriptPanel(this);
    console_ = new ConsolePanel(this);
    terrainPanel_ = new TerrainPanel(this);

    auto makeDock = [this](const QString& title, const QString& name, QWidget* widget) {
        auto* dock = new QDockWidget(title, this);
        dock->setObjectName(name);
        dock->setWidget(widget);
        dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
                          QDockWidget::DockWidgetClosable);
        return dock;
    };

    gameDock_ = makeDock(tr("Game"), QStringLiteral("GameDock"), gameView_);
    hierarchyDock_ = makeDock(tr("Hierarchy"), QStringLiteral("HierarchyDock"), hierarchy_);
    inspectorDock_ = makeDock(tr("Inspector"), QStringLiteral("InspectorDock"), inspector_);
    assetsDock_ = makeDock(tr("Assets"), QStringLiteral("AssetsDock"), assets_);
    scriptDock_ = makeDock(tr("Script"), QStringLiteral("ScriptDock"), script_);
    consoleDock_ = makeDock(tr("Console"), QStringLiteral("ConsoleDock"), console_);
    setCentralWidget(sceneView_);
    addDockWidget(Qt::LeftDockWidgetArea, hierarchyDock_);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock_);
    addDockWidget(Qt::LeftDockWidgetArea, assetsDock_);
    tabifyDockWidget(hierarchyDock_, assetsDock_);
    hierarchyDock_->raise();
    addDockWidget(Qt::BottomDockWidgetArea, scriptDock_);
    addDockWidget(Qt::BottomDockWidgetArea, consoleDock_);
    tabifyDockWidget(scriptDock_, consoleDock_);
    scriptDock_->raise();
    addDockWidget(Qt::RightDockWidgetArea, gameDock_);
    splitDockWidget(inspectorDock_, gameDock_, Qt::Vertical);

    // Floating panels start hidden; menus and the toolbar open them.
    animationDock_ = new AnimationDock(this);
    addDockWidget(Qt::RightDockWidgetArea, animationDock_);
    animationDock_->hide();
    lightingDock_ = new LightingDock(this);
    addDockWidget(Qt::RightDockWidgetArea, lightingDock_);
    lightingDock_->hide();
    profilerDock_ = new ProfilerDock(this);
    addDockWidget(Qt::RightDockWidgetArea, profilerDock_);
    profilerDock_->hide();
    inputDock_ = new InputSettingsDock(this);
    addDockWidget(Qt::RightDockWidgetArea, inputDock_);
    inputDock_->hide();
    renderingDock_ = new RenderingSettingsDock(this);
    addDockWidget(Qt::RightDockWidgetArea, renderingDock_);
    renderingDock_->hide();
    terrainDock_ = makeDock(tr("Terrain"), QStringLiteral("TerrainDock"), terrainPanel_);
    addDockWidget(Qt::RightDockWidgetArea, terrainDock_);
    terrainDock_->hide();

    layersDock_ = new LayersDock(this);
    addDockWidget(Qt::RightDockWidgetArea, layersDock_);
    layersDock_->hide();
    prefabDock_ = new PrefabDock(this);
    addDockWidget(Qt::RightDockWidgetArea, prefabDock_);
    prefabDock_->hide();
    cubemapDialog_ = new CubemapDialog(this);
    connect(cubemapDialog_, &CubemapDialog::saved, this, [this](const QString& path) {
        cubemapSavedPath_ = path;
        Q_EMIT sceneEdited();
    });
    connect(layersDock_, &LayersDock::changed, this, [this]() { Q_EMIT sceneEdited(); });
    connect(layersDock_, &LayersDock::closed, this, [this]() { layersVisible_ = false; });
    connect(prefabDock_, &PrefabDock::saveRequested, this, [this]() { actions.prefabSave = true; });
    connect(prefabDock_, &PrefabDock::exitRequested, this, [this]() { actions.prefabExit = true; });

    settingsDialog_ = new SettingsDialog(this);
    modelPreview_ = new ModelPreviewDialog(this);

    connect(animationDock_, &AnimationDock::closed, this, [this]() {
        if (animation_) {
            animation_->visible = false;
        }
    });
    connect(animationDock_, &AnimationDock::changed, this, [this]() { Q_EMIT sceneEdited(); });
    connect(lightingDock_, &LightingDock::changed, this, [this]() { Q_EMIT sceneEdited(); });
    connect(terrainPanel_, &TerrainPanel::terrainEdited, this, [this]() {
        Q_EMIT sceneEdited();
    });
    connect(hierarchy_, &HierarchyPanel::hierarchyEdited, this, [this]() {
        Q_EMIT sceneEdited();
    });
    // Picking stays engine-side: the engine already owns the ray and the
    // object list, so the panels only report a chosen entity back to it.
    connect(hierarchy_, &HierarchyPanel::selectionChanged, this, [this](Entity entity) {
        setSelection(entity);
        Q_EMIT selectionChangedByUser(entity);
    });
    connect(hierarchy_, &HierarchyPanel::createRequested, this, [this](int id) {
        actions.createObject = id;
        actions.createParent = selection();
    });
    connect(hierarchy_, &HierarchyPanel::renameLevelRequested, this,
            [this]() { actions.renameLevel = true; });
    connect(inspector_, &InspectorPanel::inspectorEdited, this, [this]() {
        Q_EMIT sceneEdited();
    });
    connect(assets_, &AssetPanel::spawnModel, this, [this](const QString& path) {
        actions.spawnModel = path.toStdString();
    });
    connect(assets_, &AssetPanel::spawnPrefab, this, [this](const QString& path) {
        actions.spawnPrefab = path.toStdString();
    });
    connect(assets_, &AssetPanel::editPrefab, this, [this](const QString& path) {
        actions.editPrefab = path.toStdString();
    });
    connect(assets_, &AssetPanel::editCubemap, this, [this](const QString& path) {
        actions.editCubemap = path.toStdString();
    });
    connect(assets_, &AssetPanel::editAnimation, this, [this](const QString& path) {
        actions.editAnimation = path.toStdString();
        openAnimationEditor();
    });
    connect(assets_, &AssetPanel::previewModel, this, [this](const QString& path) {
        actions.previewModel = path.toStdString();
        openModelPreview(path);
    });
    connect(assets_, &AssetPanel::openScript, this, [this](const QString& path) {
        script_->openPath(path);
        if (scriptDock_->isHidden()) {
            scriptDock_->show();
            scriptDock_->raise();
        }
    });
    connect(assets_, &AssetPanel::importRequested, this, [this]() { actions.importFiles = true; });
    connect(script_, &ScriptPanel::scriptEdited, this, [this]() {});
    connect(modelPreview_, &ModelPreviewDialog::closed, this, [this]() {
        if (preview_) {
            preview_->visible = false;
        }
    });
    connect(modelPreview_, &ModelPreviewDialog::changed, this, [this]() {});
    connect(inputDock_, &InputSettingsDock::closed, this, [this]() {
        if (inputSettings_) {
            inputSettings_->visible = false;
        }
    });
    connect(renderingDock_, &RenderingSettingsDock::closed, this, [this]() {
        renderingDockVisible_ = false;
    });
    connect(renderingDock_, &RenderingSettingsDock::changed, this, [this]() {
        Q_EMIT sceneEdited();
    });
    connect(inputDock_, &InputSettingsDock::bindingsChanged, this, [this]() {
        Q_EMIT sceneEdited();
    });
    connect(profilerDock_, &ProfilerDock::closed, this, [this]() {
        profilerVisible_ = false;
        if (profilerAction_) {
            profilerAction_->setChecked(false);
        }
    });
    connect(settingsDialog_, &SettingsDialog::themeSelected, this,
            [this](int theme) { applyTheme(theme); });
    connect(settingsDialog_, &SettingsDialog::settingsChanged, this, [this]() {
        if (settings_) {
            settings_->save();
        }
        Q_EMIT sceneEdited();
    });
}

void EditorWindow::buildMenus() {
    auto addAction = [this](QMenu* menu,
                            const QString& text,
                            const QKeySequence& shortcut,
                            std::function<void()> slot) {
        auto* action = menu->addAction(text);
        if (!shortcut.isEmpty()) {
            action->setShortcut(shortcut);
            action->setShortcutContext(Qt::ApplicationShortcut);
        }
        connect(action, &QAction::triggered, this, std::move(slot));
        return action;
    };

    fileMenu_ = menuBar()->addMenu(tr("&File"));
    addAction(fileMenu_, tr("New Project..."), QKeySequence::New, [this]() {
        actions.newLevel = false;
        actions.openProject = true;
    });
    addAction(fileMenu_, tr("Open Project..."), QKeySequence::Open,
              [this]() { actions.openProject = true; });
    recentMenu_ = fileMenu_->addMenu(tr("Recent Projects"));
    fileMenu_->addSeparator();
    addAction(fileMenu_, tr("Save Project"), QKeySequence::Save,
              [this]() { actions.saveProject = true; });
    addAction(fileMenu_, tr("Save Project As..."), QKeySequence::SaveAs,
              [this]() { actions.saveProjectAs = true; });
    fileMenu_->addSeparator();
    addAction(fileMenu_, tr("New Level..."), QKeySequence(), [this]() { actions.newLevel = true; });
    addAction(fileMenu_, tr("Save Level"), QKeySequence(Qt::CTRL | Qt::Key_L), [this]() {
        actions.saveLevel = true;
    });
    fileMenu_->addSeparator();
    addAction(fileMenu_, tr("Import Assets..."), QKeySequence(), [this]() {
        actions.importFiles = true;
    });
    fileMenu_->addSeparator();
    addAction(fileMenu_, tr("Exit"), QKeySequence::Quit, [this]() { actions.exit = true; });

    editMenu_ = menuBar()->addMenu(tr("&Edit"));
    undoAction_ = addAction(editMenu_, tr("Undo"), QKeySequence::Undo,
                            [this]() { actions.undo = true; });
    redoAction_ = addAction(editMenu_, tr("Redo"), QKeySequence::Redo,
                            [this]() { actions.redo = true; });
    editMenu_->addSeparator();
    copyAction_ = addAction(editMenu_, tr("Copy"), QKeySequence::Copy,
                            [this]() { actions.copy = true; });
    pasteAction_ = addAction(editMenu_, tr("Paste"), QKeySequence::Paste,
                             [this]() { actions.paste = true; });
    duplicateAction_ = addAction(editMenu_, tr("Duplicate"), QKeySequence(Qt::CTRL | Qt::Key_D),
                                 [this]() { actions.duplicate = true; });
    parentAction_ = addAction(editMenu_, tr("Change Parent"), QKeySequence(Qt::CTRL | Qt::Key_P),
                              [this]() { actions.parentToggle = true; });
    editMenu_->addSeparator();
    addAction(editMenu_, tr("Backup Project"), QKeySequence(), [this]() { actions.backup = true; });
    addAction(editMenu_, tr("Fix Project Files"), QKeySequence(), [this]() {
        actions.fixProject = true;
    });

    projectMenu_ = menuBar()->addMenu(tr("&Play"));
    playAction_ = addAction(projectMenu_, tr("Play"), QKeySequence(Qt::Key_F5),
                            [this]() { actions.play = true; });
    pauseAction_ = addAction(projectMenu_, tr("Pause"), QKeySequence(Qt::Key_F6),
                             [this]() { actions.pause = true; });
    stopAction_ = addAction(projectMenu_, tr("Stop"), QKeySequence(Qt::Key_F7),
                            [this]() { actions.stop = true; });
    projectMenu_->addSeparator();
    separateGameAction_ = addAction(projectMenu_, tr("Separate Game Window"), QKeySequence(),
                                    [this]() { actions.separateGameWindow = true; });
    separateGameAction_->setCheckable(true);
    fullscreenGameAction_ = addAction(projectMenu_, tr("Toggle Game Fullscreen"),
                                      QKeySequence(Qt::Key_F11), [this]() {
                                          actions.toggleGameFullscreen = true;
                                      });
    fullscreenGameAction_->setEnabled(false);

    levelMenu_ = menuBar()->addMenu(tr("&Level"));
    Q_UNUSED(levelMenu_);

    windowMenu_ = menuBar()->addMenu(tr("&Window"));
    auto togglePanel = [this](QAction* action) {
        connect(action, &QAction::triggered, this, [this, action]() {
            if (auto* dock = dockForAction(action)) {
                dock->setVisible(action->isChecked());
                if (action->isChecked()) {
                    dock->raise();
                }
            }
        });
    };
    const struct {
        const char* label;
        QDockWidget* dock;
        QKeySequence shortcut;
    } panels[]{
        {"Game", gameDock_, QKeySequence(Qt::Key_F2)},
        {"Hierarchy", hierarchyDock_, QKeySequence()},
        {"Inspector", inspectorDock_, QKeySequence()},
        {"Assets", assetsDock_, QKeySequence()},
        {"Script Editor", scriptDock_, QKeySequence()},
        {"Console", consoleDock_, QKeySequence()},
        {"Animation", animationDock_, QKeySequence()},
        {"Lightmapping", lightingDock_, QKeySequence()},
        {"Terrain", terrainDock_, QKeySequence()},
        {"Layers", layersDock_, QKeySequence()},
        {"Prefab Mode", prefabDock_, QKeySequence()},
    };
    for (const auto& panel : panels) {
        auto* action = windowMenu_->addAction(tr(panel.label));
        action->setCheckable(true);
        action->setChecked(true);
        action->setShortcut(panel.shortcut);
        panelActions_.push_back(action);
        panelDockNames_.push_back(panel.dock->objectName());
        togglePanel(action);
    }
    windowMenu_->addSeparator();
    profilerAction_ = windowMenu_->addAction(tr("Profiler"));
    profilerAction_->setCheckable(true);
    connect(profilerAction_, &QAction::triggered, this,
            [this](bool visible) { setProfilerVisible(visible); });
    fpsAction_ = windowMenu_->addAction(tr("FPS Counter"));
    fpsAction_->setCheckable(true);
    connect(fpsAction_, &QAction::triggered, this,
            [this](bool visible) { setFpsOverlay(visible); });
    collidersAction_ = windowMenu_->addAction(tr("Show Colliders"));
    collidersAction_->setCheckable(true);
    connect(collidersAction_, &QAction::triggered, this,
            [this](bool visible) { setCollidersVisible(visible); });
    windowMenu_->addSeparator();
    addAction(windowMenu_, tr("Restore Default Layout"), QKeySequence(),
              [this]() { actions.resetLayout = true; });

    settingsMenu_ = menuBar()->addMenu(tr("&Settings"));
    addAction(settingsMenu_, tr("Editor Settings..."), QKeySequence(Qt::CTRL | Qt::Key_Comma),
              [this]() { openEditorSettings(); });
    addAction(settingsMenu_, tr("Input Bindings"), QKeySequence(), [this]() {
        openInputSettings();
    });
    addAction(settingsMenu_, tr("Rendering"), QKeySequence(), [this]() {
        openRenderingSettings();
    });
    settingsMenu_->addSeparator();
    auto* themeGroup = new QActionGroup(this);
    themeGroup->setExclusive(true);
    const char* const themes[]{"Legacy Dark", "Grey", "Unity 5 Classic", "Unity Dark"};
    for (int i = 0; i < 4; ++i) {
        auto* action = settingsMenu_->addAction(tr(themes[i]));
        action->setCheckable(true);
        action->setChecked(i == 0);
        action->setData(i);
        themeGroup->addAction(action);
        themeActions_.push_back(action);
        connect(action, &QAction::triggered, this, [this, i]() { actions.theme = i; });
    }
    addAction(settingsMenu_, tr("Open Settings Folder"), QKeySequence(), [this]() {
        actions.openSettingsFolder = true;
    });

    debugMenu_ = menuBar()->addMenu(tr("&Debug"));
    addAction(debugMenu_, tr("Open in VS Code"), QKeySequence(), [this]() {
        actions.openInVSCode = true;
    });
    addAction(debugMenu_, tr("Lightmapping"), QKeySequence(), [this]() { openLightingEditor(); });
    addAction(debugMenu_, tr("Animation"), QKeySequence(), [this]() { openAnimationEditor(); });

    helpMenu_ = menuBar()->addMenu(tr("&Help"));
    addAction(helpMenu_, tr("Engine Manual"), QKeySequence(Qt::Key_F1), [this]() {
        actions.openManual = 1;
    });
    addAction(helpMenu_, tr("Scripting Reference"), QKeySequence(), [this]() {
        actions.openManual = 2;
    });
    addAction(helpMenu_, tr("GitHub Repository"), QKeySequence(), [this]() {
        actions.openGitHub = true;
    });
    addAction(helpMenu_, tr("About LegacyEngine"), QKeySequence(), [this]() { actions.about = true; });
}

void EditorWindow::buildStatusBar() {
    statusLabel_ = new QLabel(this);
    detailLabel_ = new QLabel(this);
    detailLabel_->setStyleSheet(QStringLiteral("color: #8fa3bb;"));
    statusBar()->addWidget(statusLabel_, 1);
    statusBar()->addPermanentWidget(detailLabel_);
    statusBar()->setSizeGripEnabled(true);
}

void EditorWindow::restoreDefaultLayout() {
    addDockWidget(Qt::LeftDockWidgetArea, hierarchyDock_);
    addDockWidget(Qt::RightDockWidgetArea, inspectorDock_);
    addDockWidget(Qt::LeftDockWidgetArea, assetsDock_);
    tabifyDockWidget(hierarchyDock_, assetsDock_);
    hierarchyDock_->raise();
    addDockWidget(Qt::BottomDockWidgetArea, scriptDock_);
    addDockWidget(Qt::BottomDockWidgetArea, consoleDock_);
    tabifyDockWidget(scriptDock_, consoleDock_);
    scriptDock_->raise();
    addDockWidget(Qt::RightDockWidgetArea, gameDock_);
    splitDockWidget(inspectorDock_, gameDock_, Qt::Vertical);
    setCentralWidget(sceneView_);
    hierarchyDock_->show();
    inspectorDock_->show();
    assetsDock_->show();
    scriptDock_->show();
    consoleDock_->show();
    resizeDocks({hierarchyDock_, inspectorDock_}, {300, 360}, Qt::Horizontal);
    resizeDocks({hierarchyDock_, scriptDock_}, {600, 260}, Qt::Vertical);
    resizeDocks({inspectorDock_, gameDock_}, {500, 320}, Qt::Vertical);
    animationDock_->hide();
    lightingDock_->hide();
    profilerDock_->hide();
    inputDock_->hide();
    renderingDock_->hide();
    terrainDock_->hide();
    for (auto* action : panelActions_) {
        action->setChecked(true);
    }
}

void EditorWindow::rebuildRecentMenu() {
    recentMenu_->clear();
    if (recentPaths_.empty()) {
        auto* empty = recentMenu_->addAction(tr("(none)"));
        empty->setEnabled(false);
        return;
    }
    for (const auto& path : recentPaths_) {
        auto* action = recentMenu_->addAction(QString::fromStdString(path));
        connect(action, &QAction::triggered, this, [this, path]() {
            actions.openRecent = true;
            actions.recentPath = path;
        });
    }
}

void EditorWindow::rebuildLevelMenu() {
    levelMenu_->clear();
    for (const auto& name : levelNames_) {
        auto* action = levelMenu_->addAction(QString::fromStdString(name));
        action->setCheckable(true);
        action->setChecked(name == currentLevel_);
        connect(action, &QAction::triggered, this, [this, name]() {
            actions.levelName = name;
            actions.openLevel = true;
        });
    }
    if (!levelActionsEnabled_) {
        for (auto* action : levelMenu_->actions()) {
            action->setEnabled(false);
        }
    }
    levelMenu_->addSeparator();
    auto* newLevel = levelMenu_->addAction(tr("New Level..."));
    connect(newLevel, &QAction::triggered, this, [this]() { actions.newLevel = true; });
}

QWidget* EditorWindow::panel(const QString& name) const {
    // Docks are found by their title, so the engine can toggle a panel by the
    // name it shows in the View menu without knowing a pointer.
    for (auto* dock : findChildren<QDockWidget*>()) {
        if (dock->windowTitle() == name) {
            return dock->widget();
        }
    }
    return nullptr;
}

QDockWidget* EditorWindow::dockForAction(QAction* action) const {
    const int index = int(std::find(panelActions_.begin(), panelActions_.end(), action) -
                          panelActions_.begin());
    if (index < 0 || index >= int(panelDockNames_.size())) {
        return nullptr;
    }
    const QString name = panelDockNames_[std::size_t(index)];
    for (auto* dock : findChildren<QDockWidget*>()) {
        if (dock->objectName() == name) {
            return dock;
        }
    }
    return nullptr;
}

ModelPreviewDialog* EditorWindow::modelPreviewDialog() const {
    return modelPreview_;
}

void EditorWindow::updatePointer(PointerState& state,
                                 Viewport* view,
                                 const QPoint& point,
                                 Qt::KeyboardModifiers modifiers) {
    state.hovered = view->underMouse();
    state.focused = view->hasFocus();
    view->picturePosition(point, state.u, state.v);
    state.alt = modifiers.testFlag(Qt::AltModifier);
    state.shift = modifiers.testFlag(Qt::ShiftModifier);
    state.ctrl = modifiers.testFlag(Qt::ControlModifier);
}

QSize EditorWindow::scenePixelSize() const {
    if (!sceneView_) {
        return {};
    }
    const QSize picture = sceneView_->pictureSize();
    return picture.isEmpty() ? sceneView_->size() : picture;
}

QSize EditorWindow::gamePixelSize() const {
    if (!gameView_) {
        return {};
    }
    const QSize picture = gameView_->pictureSize();
    return picture.isEmpty() ? gameView_->size() : picture;
}

const EditorWindow::PointerState& EditorWindow::scenePointer() const {
    return scenePointer_;
}

const EditorWindow::PointerState& EditorWindow::gamePointer() const {
    return gamePointer_;
}

void EditorWindow::consumePointerEvents() {
    scenePointer_.pressed = scenePointer_.released = false;
    scenePointer_.wheel = 0.0f;
    gamePointer_.pressed = gamePointer_.released = false;
    gamePointer_.wheel = 0.0f;
}

void EditorWindow::setGamePointerLocked(bool locked) {
    gamePointerLocked_ = locked;
}

bool EditorWindow::gamePointerLocked() const {
    return gamePointerLocked_;
}

void EditorWindow::setPanelFocus(bool scene, bool hierarchy) {
    sceneFocused_ = scene;
    hierarchyFocused_ = hierarchy;
}

void EditorWindow::focusScene() {
    if (sceneView_) {
        sceneView_->setFocus(Qt::OtherFocusReason);
    }
}

void EditorWindow::setSelection(Entity entity) {
    selected_ = entity;
    if (sceneView_) {
        sceneView_->setSelectedEntity(entity);
    }
    if (inspector_) {
        inspector_->setSelection(entity);
    }
    if (hierarchy_) {
        hierarchy_->selectEntity(entity);
        hierarchySelection_ = hierarchy_->selection();
    }
    if (terrainPanel_) {
        terrainPanel_->setSelection(entity);
    }
    if (animationDock_) {
        animationDock_->setSelection(entity);
    }
    if (lightingDock_) {
        lightingDock_->setSelection(entity);
    }
    updateTerrainDock();
}

void EditorWindow::setHierarchySelection(const std::vector<Entity>& selection) {
    hierarchySelection_ = selection;
    if (hierarchy_) {
        hierarchy_->setSelection(selection);
    }
}

Entity EditorWindow::selection() const {
    return selected_;
}

void EditorWindow::updateTerrainDock() {
    if (!terrainDock_ || !world_) {
        return;
    }
    const bool show = terrainVisible_ && world_->alive(selected_) && world_->has<Terrain>(selected_);
    terrainDock_->setVisible(show);
}

void EditorWindow::setSelectionFromEngine(Entity entity) {
    if (entity == selected_) {
        return;
    }
    selected_ = entity;
    if (sceneView_) {
        sceneView_->setSelectedEntity(entity);
    }
    if (inspector_) {
        inspector_->setSelection(entity);
    }
    if (hierarchy_) {
        hierarchy_->refresh(entity);
    }
    if (terrainPanel_) {
        terrainPanel_->setSelection(entity);
    }
    if (animationDock_) {
        animationDock_->setSelection(entity);
    }
    if (lightingDock_) {
        lightingDock_->setSelection(entity);
    }
    updateTerrainDock();
}

void EditorWindow::setEditable(bool editAllowed) {
    editable_ = editAllowed;
    if (hierarchy_) {
        hierarchy_->setEditable(editAllowed);
    }
    if (inspector_) {
        inspector_->setEditable(editAllowed);
    }
    if (assets_) {
        assets_->setEditable(editAllowed);
    }
    if (terrainPanel_) {
        terrainPanel_->setEditable(editAllowed);
    }
    if (lightingDock_) {
        lightingDock_->setEditable(editAllowed);
    }
    if (undoAction_) {
        undoAction_->setEnabled(editAllowed);
    }
    if (redoAction_) {
        redoAction_->setEnabled(editAllowed);
    }
    if (copyAction_) {
        copyAction_->setEnabled(editAllowed);
    }
    if (pasteAction_) {
        pasteAction_->setEnabled(editAllowed);
    }
}

void EditorWindow::setPlayState(bool active, bool playing, bool paused) {
    playActive_ = active;
    if (toolbar_) {
        toolbar_->setPlayState(playing, paused);
        toolbar_->setPlayEnabled(!active || playing || paused);
    }
    if (playAction_) {
        playAction_->setEnabled(!active);
    }
    if (pauseAction_) {
        pauseAction_->setEnabled(active);
    }
    if (stopAction_) {
        stopAction_->setEnabled(active);
    }
    if (separateGameAction_) {
        separateGameAction_->setEnabled(!active);
        separateGameAction_->setChecked(separateGame_);
    }
    if (fullscreenGameAction_) {
        fullscreenGameAction_->setEnabled(active);
    }
    setToolbarsEnabled(!active);
}

void EditorWindow::setToolbarsEnabled(bool enabled) {
    if (toolbar_) {
        toolbar_->setPlayEnabled(true);
        toolbar_->setEnabled(enabled || playActive_);
    }
}

void EditorWindow::setGameWindowActive(bool active) {
    gameWindowActive_ = active;
    if (fullscreenGameAction_) {
        fullscreenGameAction_->setEnabled(active);
    }
    if (toolbar_) {
        toolbar_->setSeparateGame(separateGame_, active);
    }
}

bool EditorWindow::separateGameWindow() const {
    return toolbar_ && toolbar_->separateGame();
}

bool EditorWindow::gameWindowHovered() const {
    return gameView_ && gameView_->underMouse();
}

bool EditorWindow::gameWindowFocused() const {
    return gameWindowActive_ && gameWindowFocused_;
}

void EditorWindow::setGameWindowFocused(bool focused) {
    gameWindowFocused_ = focused;
}

void EditorWindow::setStatus(const QString& text, const QString& detail) {
    if (statusLabel_) {
        statusLabel_->setText(text);
    }
    if (detailLabel_) {
        detailLabel_->setText(detail);
    }
}

void EditorWindow::setLevelName(const QString& name) {
    levelName_ = name;
    if (hierarchy_) {
        hierarchy_->setLevelName(name);
    }
    updateWindowTitle();
}

void EditorWindow::setProjectName(const QString& name) {
    projectName_ = name;
    updateWindowTitle();
}

void EditorWindow::setSeparateGame(bool separate) {
    setSeparateGame(separate, separate);
}

void EditorWindow::setSeparateGame(bool separate, bool enabled) {
    separateGame_ = separate;
    if (toolbar_) {
        toolbar_->setSeparateGame(separate, gameWindowActive_);
    }
    if (separateGameAction_) {
        const QSignalBlocker block(separateGameAction_);
        separateGameAction_->setChecked(separate);
    }
    Q_UNUSED(enabled);
}

bool EditorWindow::snapEnabled() const {
    return QGuiApplication::keyboardModifiers().testFlag(Qt::ControlModifier);
}

void EditorWindow::setFrameInfo(float fps, float dt, double frameMs) {
    fps_ = fps;
    dt_ = dt;
    frameMs_ = frameMs;
    if (sceneView_) {
        sceneView_->setFps(fps);
    }
}

void EditorWindow::setProfilerData(const FrameProfiler& profiler) {
    if (profilerDock_) {
        profilerDock_->setData(profiler);
    }
}

void EditorWindow::setGameSurfaceImage(const QImage& image) {
    if (gameView_) {
        gameView_->setImage(image);
    }
}

void EditorWindow::setSceneSurfaceImage(const QImage& image) {
    if (sceneView_) {
        sceneView_->setImage(image);
    }
}

void EditorWindow::showError(const QString& title, const QString& message) {
    errorText_ = message;
    statusBar()->showMessage(tr("%1: %2").arg(title, message), 8000);
}

void EditorWindow::setFpsOverlay(bool visible) {
    fpsVisible_ = visible;
    if (sceneView_) {
        sceneView_->setFpsVisible(visible);
    }
    if (fpsAction_) {
        const QSignalBlocker block(fpsAction_);
        fpsAction_->setChecked(visible);
    }
}

void EditorWindow::setCollidersVisible(bool visible) {
    collidersVisible_ = visible;
    if (collidersAction_) {
        const QSignalBlocker block(collidersAction_);
        collidersAction_->setChecked(visible);
    }
}

void EditorWindow::setProfilerVisible(bool visible) {
    profilerVisible_ = visible;
    if (profilerDock_) {
        profilerDock_->setVisible(visible);
    }
    if (profilerAction_) {
        const QSignalBlocker block(profilerAction_);
        profilerAction_->setChecked(visible);
    }
}

void EditorWindow::applyTheme(int theme) {
    theme_ = std::clamp(theme, 0, 3);
    QApplication::setPalette(paletteFor(theme_));
    for (auto* action : themeActions_) {
        const QSignalBlocker block(action);
        action->setChecked(action->data().toInt() == theme_);
    }
    if (settings_) {
        settings_->theme = theme_;
    }
    if (settingsDialog_) {
        settingsDialog_->setThemeRequested(theme_);
    }
}

void EditorWindow::setRecentProjects(const std::vector<std::string>& paths) {
    recentPaths_ = paths;
    rebuildRecentMenu();
}

void EditorWindow::setLevelMenu(bool enabled,
                                const std::vector<std::string>& names,
                                const std::string& current) {
    levelActionsEnabled_ = enabled;
    levelNames_ = names;
    currentLevel_ = current;
    rebuildLevelMenu();
}

void EditorWindow::openAnimationEditor() {
    if (animationDock_) {
        animationDock_->show();
        animationDock_->raise();
        if (animation_) {
            animation_->visible = true;
        }
    }
}

void EditorWindow::openLightingEditor() {
    if (lightingDock_) {
        lightingDock_->show();
        lightingDock_->raise();
        lightingDock_->refresh();
        if (lighting_) {
            lighting_->visible = true;
        }
    }
}

void EditorWindow::openInputSettings() {
    if (inputDock_) {
        inputDock_->show();
        inputDock_->raise();
        inputDock_->refresh();
        if (inputSettings_) {
            inputSettings_->visible = true;
        }
    }
}

void EditorWindow::openRenderingSettings() {
    if (renderingDock_) {
        renderingDock_->show();
        renderingDock_->raise();
        renderingDock_->refresh();
        renderingDockVisible_ = true;
    }
}

void EditorWindow::openEditorSettings() {
    if (settingsDialog_) {
        settingsDialog_->show();
        settingsDialog_->raise();
        settingsDialog_->refresh();
    }
}

void EditorWindow::openModelPreview(const QString& path) {
    if (!modelPreview_) {
        return;
    }
    modelPreview_->openPath(path);
    if (preview_) {
        preview_->visible = true;
    }
}

void EditorWindow::openModelPreviewWindow(const QString& path) {
    openModelPreview(path);
}

void EditorWindow::openCubemapEditor(const QString& path) {
    if (cubemapDialog_ && browser_) {
        cubemapDialog_->openPath(browser_->assets, path);
    }
}

void EditorWindow::closeCubemapEditor() {
    if (cubemapDialog_) {
        cubemapDialog_->close();
    }
}

void EditorWindow::setPrefabMode(const QString& path) {
    if (!prefabDock_) {
        return;
    }
    prefabDock_->setPrefab(path);
    prefabDock_->show();
    prefabDock_->raise();
}

void EditorWindow::clearPrefabMode() {
    if (!prefabDock_) {
        return;
    }
    prefabDock_->clear();
    prefabDock_->hide();
}

void EditorWindow::openLayers() {
    if (!layersDock_) {
        return;
    }
    layersDock_->refresh();
    layersDock_->show();
    layersDock_->raise();
    layersVisible_ = true;
}

void EditorWindow::resetLayout() {
    restoreDefaultLayout();
    shellResetPending_ = false;
}

QString EditorWindow::askText(const QString& title, const QString& label, const QString& value) {
    bool accepted = false;
    const QString text =
        QInputDialog::getText(this, title, label, QLineEdit::Normal, value, &accepted);
    return accepted ? text.trimmed() : QString();
}

bool EditorWindow::confirm(const QString& title, const QString& message) {
    return QMessageBox::question(this,
                                 title,
                                 message,
                                 QMessageBox::Yes | QMessageBox::No,
                                 QMessageBox::No) == QMessageBox::Yes;
}

bool EditorWindow::takeExitConfirmed() {
    if (!exitConfirmed_) {
        return false;
    }
    exitConfirmed_ = false;
    return true;
}

void EditorWindow::consoleMessage(const QString& text) {
    if (console_) {
        console_->appendLine(text);
    }
}

void EditorWindow::focusGameView() {
    if (gameView_) {
        gameView_->show();
        gameDock_->show();
        gameDock_->raise();
        gameView_->setFocus(Qt::OtherFocusReason);
    }
}

void EditorWindow::showParentChooser(World& world) {
    if (!world.alive(selected_)) {
        return;
    }
    // One menu item per legal parent, built from the same levelObjects() list
    // the Hierarchy shows. Picking one records the request for the engine.
    QMenu menu(this);
    menu.setToolTipsVisible(true);
    for (const auto& object : levelObjects(world)) {
        if (!canParent(world, selected_, object.entity)) {
            continue;
        }
        auto* action = menu.addAction(QString::fromStdString(object.name));
        const Entity parent = object.entity;
        connect(action, &QAction::triggered, this, [this, parent]() {
            parentRequest_ = parent;
            parentRequested_ = true;
        });
    }
    if (menu.isEmpty()) {
        menu.addAction(tr("No legal parent"))->setEnabled(false);
    }
    if (auto* hierarchyItem = hierarchy_ ? hierarchy_->findChild<QTreeWidget*>() : nullptr) {
        menu.exec(QCursor::pos());
        (void)hierarchyItem;
    } else {
        menu.exec(QCursor::pos());
    }
}

void EditorWindow::showAbout() {
    // One-shot informational box; the menu item used to open an ImGui modal.
    QMessageBox::about(this,
                       tr("About LegacyEngine"),
                       tr("<b>LegacyEngine</b><br>3D scene editor with LegacyScript (Lua 5.4)"
                          "<br>SDL3 / OpenGL / Qt 6 / Jolt"));
}

void EditorWindow::refreshLayers() {
}

void EditorWindow::closeModelPreview() {
    if (modelPreview_) {
        modelPreview_->close();
    }
    if (preview_) {
        preview_->visible = false;
    }
}

void EditorWindow::setObjectActionsEnabled(bool enabled) {
    for (QAction* action : {copyAction_, duplicateAction_, parentAction_}) {
        if (action) {
            action->setEnabled(enabled);
        }
    }
}

void EditorWindow::setTerrainToolsVisible(bool visible) {
    terrainVisible_ = visible;
    updateTerrainDock();
}

void EditorWindow::updateWindowTitle() {
    QString title = QStringLiteral("LegacyEngine Editor");
    if (!levelName_.isEmpty()) {
        title += QStringLiteral(" - ") + levelName_;
    }
    if (projectName_.isEmpty() == false) {
        title += QStringLiteral(" [") + projectName_ + QStringLiteral("]");
    }
    if (title != lastTitle_) {
        lastTitle_ = title;
        setWindowTitle(title);
    }
}

void EditorWindow::refresh(float frameMs) {
    frameMs_ = double(frameMs);
    if (!world_) {
        return;
    }
    if (sceneView_) {
        sceneView_->refresh();
        sceneView_->setSnapEnabled(QGuiApplication::keyboardModifiers().testFlag(
            Qt::ControlModifier));
        sceneView_->setFpsVisible(fpsVisible_);
        sceneView_->setFps(fps_);
    }
    if (gameView_) {
        gameView_->refresh();
    }
    if (hierarchy_) {
        hierarchy_->refresh(selected_);
        hierarchySelection_ = hierarchy_->selection();
    }
    if (inspector_) {
        inspector_->refresh();
    }
    if (assets_) {
        assets_->refresh();
    }
    if (script_) {
        script_->refresh();
    }
    if (console_) {
        console_->refresh();
    }
    if (terrainPanel_ && terrainDock_ && terrainDock_->isVisible()) {
        terrainPanel_->refresh();
    }
    if (animationDock_ && animationDock_->isVisible()) {
        animationDock_->update(dt_);
        animationDock_->refresh();
    }
    if (lightingDock_ && lightingDock_->isVisible()) {
        lightingRefreshTimer_ += dt_;
        if (lightingRefreshTimer_ > .25f) {
            lightingRefreshTimer_ = 0;
            lightingDock_->refresh();
        }
    }
    if (modelPreview_ && modelPreview_->isVisible()) {
        modelPreview_->refresh(dt_);
    }
    if (toolbar_) {
        toolbar_->setTool(sceneView_ ? sceneView_->tool() : TransformTool::Move);
    }
    refreshLayers();
    updateTerrainDock();
    updateWindowTitle();
}

void EditorWindow::saveLayout() {
    const QString path = layoutFile();
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(saveState().toBase64());
    }
}

void EditorWindow::restoreLayout() {
    const QString path = layoutFile();
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QByteArray state = QByteArray::fromBase64(file.readAll());
    if (!state.isEmpty()) {
        restoreState(state);
    }
}

void EditorWindow::requestClose() {
    exitConfirmed_ = true;
    close();
}

void EditorWindow::closeEvent(QCloseEvent* event) {
    saveLayout();
    if (!exitConfirmed_) {
        // Closing the shell means "quit": the engine runs its own exit path,
        // which saves or prompts first. Qt must not tear the window down yet.
        actions.exit = true;
        event->ignore();
        return;
    }
    QMainWindow::closeEvent(event);
}

#ifndef QT_NO_DRAGANDDROP
void EditorWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void EditorWindow::dropEvent(QDropEvent* event) {
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile()) {
            paths.push_back(url.toLocalFile());
        }
    }
    if (!paths.isEmpty()) {
        Q_EMIT filesDropped(paths);
        event->acceptProposedAction();
    }
}
#endif

World& EditorWindow::world() {
    return *world_;
}

AssetBrowser& EditorWindow::browser() {
    return *browser_;
}

EditorHistory& EditorWindow::history() {
    return *history_;
}

Renderer& EditorWindow::renderer() {
    return *renderer_;
}

ScriptEditor& EditorWindow::scripts() {
    return *scripts_;
}

AnimationWindow& EditorWindow::animationWindow() {
    return *animation_;
}

LightmappingWindow& EditorWindow::lightingWindow() {
    return *lighting_;
}

PlayMode& EditorWindow::playMode() {
    return *playMode_;
}

EditorSettings& EditorWindow::settings() {
    return *settings_;
}

InputSettings& EditorWindow::inputSettings() {
    return *inputSettings_;
}

TransformEditor& EditorWindow::transformEditor() {
    return *transform_;
}

} // namespace qt
} // namespace ui
