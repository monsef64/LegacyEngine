#pragma once

#include "core/components.hpp"
#include "core/profiler.hpp"
#include "project/project_io.hpp"
#include "editor/editor_settings.hpp"
#include "editor/input_settings.hpp"
#include "editor/play_mode.hpp"
#include <QImage>
#include <QMainWindow>
#include <QString>
#include <QPoint>
#include <QSize>
#include <QStringList>
#include <optional>
#include <string>
#include <vector>

struct SDL_Window;
class QDockWidget;
class QLabel;
class QMenu;
class QAction;
class QPoint;
class QSize;

class World;
class AssetBrowser;
class EditorHistory;
class Renderer;
class ScriptEditor;
class AnimationWindow;
class LightmappingWindow;
class TransformEditor;
struct RenderingSettings;
struct InputState;
class FrameProfiler;
class AssetPreviewRenderer;
class ModelPreviewWindow;
struct OrbitCamera;


namespace Terrains {
class Editor;
}

namespace ui {
namespace qt {

class SceneView;
class GameView;
class Viewport;
class HierarchyPanel;
class InspectorPanel;
class AssetPanel;
class ConsolePanel;
class ScriptPanel;
class Toolbar;
class AnimationDock;
class LightingDock;
class ProfilerDock;
class InputSettingsDock;
class RenderingSettingsDock;
class SettingsDialog;
class LayersDock;
class PrefabDock;
class CubemapDialog;
class ModelPreviewDialog;
class TerrainPanel;

// Editor actions collected by Qt widgets during a frame. The engine loop
// consumes and clears them once per frame, so widget callbacks never mutate
// engine state in the middle of a Qt event.
struct PendingActions {
    std::optional<int> createObject;
    Entity createParent{};

    bool undo = false, redo = false, duplicate = false, copy = false, paste = false,
         parentToggle = false;
    bool openProject = false, openRecent = false, saveProject = false, saveProjectAs = false;
    std::string recentPath;
    bool saveLevel = false, newLevel = false, renameLevel = false;
    std::string levelName;
    bool prefabSave = false, prefabExit = false;
    bool exit = false;
    bool resetLayout = false;
    int openManual = 0; // 1 = index.html, 2 = scripting-reference.html
    bool openGitHub = false, about = false;
    bool play = false, pause = false, stop = false;
    int theme = -1;
    bool backup = false, fixProject = false, openInVSCode = false;
    bool openLevel = false, openSettingsFolder = false;
    bool importFiles = false;
    bool separateGameWindow = false, toggleGameFullscreen = false;

    // Asset browser operations that change the scene rather than files.
    std::string spawnModel, spawnPrefab, editPrefab, editCubemap, editAnimation,
        previewModel, openScript;
    Entity prefabCreation{};

    void clear() {
        *this = PendingActions{};
    }
};

class EditorWindow : public QMainWindow {
    Q_OBJECT

  Q_SIGNALS:
    // Panels report user edits here instead of mutating engine state inside a
    // Qt event. The engine loop treats them as one undo step and refreshes.
    void selectionChangedByUser(Entity entity);
    void sceneEdited();

  public:
    explicit EditorWindow(SDL_Window* sdlWindow, QWidget* parent = nullptr);
    ~EditorWindow() override;

    void bind(World& world0,
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
              OrbitCamera& camera0);

    void setGameWindowActive(bool active);
    bool separateGameWindow() const;
    bool gameWindowHovered() const;
    bool gameWindowFocused() const;
    void setGameWindowFocused(bool focused);

    // Fills every panel from engine state. Called once per frame.
    void refresh(float frameMs);
    void refreshSelectionFromEngine(Entity entity);
    void setPlayState(bool active, bool playing, bool paused);
    void setMenuState(bool projectEnabled,
                      bool editAllowed,
                      bool canUndo,
                      bool canRedo,
                      bool objectSelected,
                      bool clipboardAvailable,
                      bool playing,
                      bool paused,
                      const std::string& projectName,
                      const std::vector<std::string>& levelNames,
                      const std::string& currentLevel,
                      bool levelsEnabled);
    void setStatus(const QString& text, const QString& detail);
    void setLevelName(const QString& name);
    void setProjectName(const QString& name);
    void setEditable(bool editAllowed);
    void setProfilerData(const FrameProfiler& profiler);
    void setGameSurfaceImage(const QImage& image);
    void setSceneSurfaceImage(const QImage& image);
    ConsolePanel* consolePanel() const {
        return console_;
    }
    ScriptPanel* scriptPanel() const {
        return script_;
    }
    AssetPanel* assetPanel() const {
        return assets_;
    }
    Toolbar* toolbar() const {
        return toolbar_;
    }
    void openModelPreviewWindow(const QString& path);
    void setTerrainToolsVisible(bool visible);
    // Greys out Duplicate/Copy/Parent when nothing is selected, as the old
    // Edit menu did.
    void setObjectActionsEnabled(bool enabled);
    bool terrainToolsVisible() const {
        return terrainVisible_;
    }
    void showError(const QString& title, const QString& message);
    void setFpsOverlay(bool visible);
    // The engine has to know: the FPS readout and the collider overlay are
    // drawn into the engine's own framebuffer, not by a widget.
    bool fpsOverlayVisible() const {
        return fpsVisible_;
    }
    bool collidersVisible() const {
        return collidersVisible_;
    }
    void setCollidersVisible(bool visible);
    bool profilerVisible() const {
        return profilerVisible_;
    }
    void setProfilerVisible(bool visible);
    void applyTheme(int theme);
    void setRecentProjects(const std::vector<std::string>& paths);
    void setLevelMenu(bool enabled, const std::vector<std::string>& names, const std::string& current);
    void openAnimationEditor();
    void openLightingEditor();
    void openInputSettings();
    void openRenderingSettings();
    void openEditorSettings();
    void openModelPreview(const QString& path);
    void closeModelPreview();
    void openCubemapEditor(const QString& path);
    void closeCubemapEditor();
    void setPrefabMode(const QString& path);
    void clearPrefabMode();
    void openLayers();
    void refreshLayers();
    void resetLayout();
    void showAbout();
    void focusGameView();
    // Small modal helpers the engine's action handlers use instead of the old
    // ImGui popups. Both return an empty string / false when cancelled.
    QString askText(const QString& title, const QString& label, const QString& value);
    bool confirm(const QString& title, const QString& message);
    bool takeExitConfirmed();
    // A cubemap the user just saved, for the engine to report in the console.
    bool cubemapSaved() const {
        return !cubemapSavedPath_.isEmpty();
    }
    QString takeCubemapSaved() {
        QString path = cubemapSavedPath_;
        cubemapSavedPath_.clear();
        return path;
    }
    void consoleMessage(const QString& text);
    // Engine-confirmed shutdown: closes the window for real.
    void requestClose();
    // Parent picker: the old Ctrl+P popup, now a menu next to the hierarchy.
      void showParentChooser(World& world);
  public:
    // Set when the user picked a parent in showParentChooser(), valid for the
    // frame in which the engine consumes it.
    bool takeParentRequest(Entity& parent) {
        const bool requested = parentRequested_;
        parentRequested_ = false;
        parent = parentRequest_;
        parentRequest_ = {};
        return requested;
    }

    ModelPreviewDialog* modelPreviewDialog() const;

    Entity selection() const;
    void setSelection(Entity entity);
    // Engine-side selection changes (picking, level load, undo) bypass Qt
    // events, so the shell exposes them separately from user-driven changes.
    void setSelectionFromEngine(Entity entity);
    const std::vector<Entity>& hierarchySelection() const;
    void setHierarchySelection(const std::vector<Entity>& selection);

    World& world();
    AssetBrowser& browser();
    EditorHistory& history();
    Renderer& renderer();
    ScriptEditor& scripts();
    AnimationWindow& animationWindow();
    LightmappingWindow& lightingWindow();
    PlayMode& playMode();
    EditorSettings& settings();
    InputSettings& inputSettings();
    TransformEditor& transformEditor();
    SDL_Window* sdlWindow() const {
        return sdlWindow_;
    }

    SceneView* sceneView() const {
        return sceneView_;
    }
    GameView* gameView() const {
        return gameView_;
    }
    HierarchyPanel* hierarchyPanel() const {
        return hierarchy_;
    }
    InspectorPanel* inspectorPanel() const {
        return inspector_;
    }

    PendingActions actions;

    // Engine-side data the panels need while drawing, refreshed once a frame.
    void setFrameInfo(float fps, float dt, double frameMs);
    float frameRate() const {
        return fps_;
    }
    void setSeparateGame(bool separate);
    void setSeparateGame(bool separate, bool enabled);
    bool snapEnabled() const;
    bool playActive() const {
        return playActive_;
    }
    const QString& errorText() const {
        return errorText_;
    }

    // ---- Viewport plumbing ------------------------------------------------
    // The engine renders into its own framebuffer and then hands Qt a finished
    // picture, so the widget size decides the surface size and Qt decides
    // where the pointer is. These accessors translate between the two.
    struct PointerState {
        bool hovered = false, focused = false;
        bool pressed = false, down = false, released = false; // scene left button
        int button = 0;      // last button that changed, Qt::MouseButton value
        float u = 0.5f, v = 0.5f; // picture coordinates, 0..1 from the top left
        float wheel = 0.0f;       // accumulates until the frame consumes it
        bool alt = false, shift = false, ctrl = false;
    };
    QSize scenePixelSize() const;
    QSize gamePixelSize() const;
    const PointerState& scenePointer() const;
    const PointerState& gamePointer() const;
    // Clears the one-shot press/release/wheel flags after the engine used them.
    void consumePointerEvents();
    void setGamePointerLocked(bool locked);
    bool gamePointerLocked() const;
    void setPanelFocus(bool scene, bool hierarchy);
    bool scenePanelFocused() const {
        return sceneFocused_;
    }
    bool hierarchyPanelFocused() const {
        return hierarchyFocused_;
    }
    void focusScene();
    void confirmExit() {
        exitConfirmed_ = true;
    }

  Q_SIGNALS:
    void filesDropped(const QStringList& paths);

  protected:
    void closeEvent(QCloseEvent* event) override;
#ifndef QT_NO_DRAGANDDROP
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
#endif

  public:
    // The widget inside the dock whose title is `name`, or null. Used by the
    // engine to drive panels it does not own a pointer to.
    QWidget* panel(const QString& name) const;

  private:
    void buildDocks();
    void buildMenus();
    void buildStatusBar();
    void buildToolbar();
    QDockWidget* dockForAction(QAction* action) const;
    void restoreDefaultLayout();
    void refreshFrame(float frameMs);
    void updateTerrainDock();
    void setToolbarsEnabled(bool enabled);
    void updatePointer(PointerState& state,
                       Viewport* view,
                       const QPoint& point,
                       Qt::KeyboardModifiers modifiers);
    void rebuildRecentMenu();
    void rebuildLevelMenu();
    void updateWindowTitle();
    void saveLayout();
    void restoreLayout();

    SDL_Window* sdlWindow_ = nullptr;
    World* world_ = nullptr;
    AssetBrowser* browser_ = nullptr;
    EditorHistory* history_ = nullptr;
    Renderer* renderer_ = nullptr;
    ScriptEditor* scripts_ = nullptr;
    AnimationWindow* animation_ = nullptr;
    LightmappingWindow* lighting_ = nullptr;
    PlayMode* playMode_ = nullptr;
    Terrains::Editor* terrainEditor_ = nullptr;
    EditorSettings* settings_ = nullptr;
    InputSettings* inputSettings_ = nullptr;
    RenderingSettings* renderingSettings_ = nullptr;
    TransformEditor* transform_ = nullptr;

    SceneView* sceneView_ = nullptr;
    GameView* gameView_ = nullptr;
    HierarchyPanel* hierarchy_ = nullptr;
    InspectorPanel* inspector_ = nullptr;
    AssetPanel* assets_ = nullptr;
    ConsolePanel* console_ = nullptr;
    ScriptPanel* script_ = nullptr;
    Toolbar* toolbar_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* detailLabel_ = nullptr;

    QDockWidget* sceneDock_ = nullptr;
    QDockWidget* gameDock_ = nullptr;
    QDockWidget* hierarchyDock_ = nullptr;
    QDockWidget* inspectorDock_ = nullptr;
    QDockWidget* assetsDock_ = nullptr;
    QDockWidget* scriptDock_ = nullptr;
    QDockWidget* consoleDock_ = nullptr;
    AnimationDock* animationDock_ = nullptr;
    LightingDock* lightingDock_ = nullptr;
    ProfilerDock* profilerDock_ = nullptr;
    InputSettingsDock* inputDock_ = nullptr;
    RenderingSettingsDock* renderingDock_ = nullptr;
    SettingsDialog* settingsDialog_ = nullptr;
    LayersDock* layersDock_ = nullptr;
    PrefabDock* prefabDock_ = nullptr;
    CubemapDialog* cubemapDialog_ = nullptr;
    ModelPreviewDialog* modelPreview_ = nullptr;
    QDockWidget* terrainDock_ = nullptr;
    TerrainPanel* terrainPanel_ = nullptr;

    QMenu* fileMenu_ = nullptr;
    QMenu* recentMenu_ = nullptr;
    QMenu* editMenu_ = nullptr;
    QMenu* projectMenu_ = nullptr;
    QMenu* levelMenu_ = nullptr;
    QMenu* windowMenu_ = nullptr;
    QMenu* settingsMenu_ = nullptr;
    QMenu* debugMenu_ = nullptr;
    QMenu* helpMenu_ = nullptr;
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* copyAction_ = nullptr;
    QAction* duplicateAction_ = nullptr;
    QAction* parentAction_ = nullptr;
    QAction* pasteAction_ = nullptr;
    QAction* playAction_ = nullptr;
    QAction* pauseAction_ = nullptr;
    QAction* stopAction_ = nullptr;
    QAction* separateGameAction_ = nullptr;
    QAction* fullscreenGameAction_ = nullptr;
    QAction* fpsAction_ = nullptr;
    QAction* profilerAction_ = nullptr;
    QAction* collidersAction_ = nullptr;
    std::vector<QAction*> levelActions_;
    std::vector<QAction*> themeActions_;
    std::vector<QAction*> panelActions_;
    std::vector<QString> panelDockNames_;
    std::vector<std::string> levelNames_;
    std::string currentLevel_;
    bool levelActionsEnabled_ = true;
    bool renderingDockVisible_ = false;
    bool editable_ = true;
    bool playActive_ = false;
    bool separateGame_ = false;
    bool gameWindowFocused_ = false;
    bool terrainVisible_ = false;
    bool layersVisible_ = false;
    bool shellResetPending_ = false;
    bool parentRequested_ = false;
    QString cubemapSavedPath_;
    Entity parentRequest_{};
    PointerState scenePointer_;
    PointerState gamePointer_;
    bool gamePointerLocked_ = false;
    bool sceneFocused_ = false;
    bool hierarchyFocused_ = false;
    Entity selected_{};
    int theme_ = 0;
    float frameMs_ = 0.0f;
    float lightingRefreshTimer_ = 0.0f;
    QString levelName_;
    QString projectName_;
    QString errorText_;
    std::vector<Entity> hierarchySelection_;

    AssetPreviewRenderer* previews_ = nullptr;
    ModelPreviewWindow* preview_ = nullptr;
    const ProjectIO::Meshes* meshes_ = nullptr;
    bool fpsVisible_ = false, collidersVisible_ = false, profilerVisible_ = false;
    bool gameWindowActive_ = false;
    bool exitConfirmed_ = false;
    float fps_ = 60.0f;
    float dt_ = 1.0f / 60.0f;
    std::vector<std::string> recentPaths_;
    QString lastTitle_;
};

} // namespace qt
} // namespace ui
