#pragma once

#include "core/components.hpp"
#include "core/profiler.hpp"
#include "project/project_io.hpp"

#include <array>
#include <filesystem>
#include "editor/editor_settings.hpp"
#include "editor/input_settings.hpp"
#include "ui/qt/qt_viewport.hpp"
#include "ui/qt/qt_widgets.hpp"
#include <QDialog>
#include <QDockWidget>
#include <QImage>
#include <QWidget>
#include <functional>
#include <set>
#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSplitter;
class QTabWidget;
class QTableWidget;
class QTextEdit;
class QPlainTextEdit;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;
class QListWidget;
class QListWidgetItem;
class QScrollArea;
class QVBoxLayout;
class QProgressBar;
class QCloseEvent;
class QKeyEvent;
class QMouseEvent;

class World;
class AssetManager;
class AssetBrowser;

class EditorHistory;
class Renderer;
class ScriptEditor;
class AnimationWindow;
class LightmappingWindow;
class TransformEditor;
struct RenderingSettings;

class ModelPreviewWindow;
struct InputState;
class FrameProfiler;

namespace Terrains {
class Editor;
}

namespace ui {
namespace qt {

class PreviewView;

class EditorWindow;

// The toolbar above the docks: creation menu, transform tool toggles, snapping
// and playback transport. It replaces the TransformEditor::drawToolbar() and
// playback buttons that used to live inside the Scene window.
class Toolbar : public QWidget {
    Q_OBJECT

  public:
    explicit Toolbar(QWidget* parent = nullptr);
    void setPlayState(bool playing, bool paused);
    void setTool(TransformTool tool);
    void setLocalSpace(bool local);
    void setCenterPivot(bool center);
    void setSnapStep(float step);
    void setPlayEnabled(bool enabled);
    void setSeparateGame(bool enabled, bool active);
    void fullscreenGame();
    TransformTool tool() const;
    bool localSpace() const;
    bool centerPivot() const;
    float snapStep() const;
    bool separateGame() const;

  Q_SIGNALS:
    void createRequested(int id);
    void toolChanged(TransformTool tool);
    void localSpaceChanged(bool local);
    void centerPivotChanged(bool center);
    void snapChanged(float step);
    void playRequested();
    void pauseRequested();
    void stopRequested();
    void separateGameToggled(bool enabled);
    void fullscreenGameRequested();

  private:
    QToolButton* createButton_ = nullptr;
    QToolButton* moveButton_ = nullptr;
    QToolButton* rotateButton_ = nullptr;
    QToolButton* scaleButton_ = nullptr;
    QToolButton* worldButton_ = nullptr;
    QToolButton* localButton_ = nullptr;
    QComboBox* pivot_ = nullptr;
    class QDoubleSpinBox* snap_ = nullptr;
    QToolButton* playButton_ = nullptr;
    QToolButton* pauseButton_ = nullptr;
    QToolButton* stopButton_ = nullptr;
    QCheckBox* separateGame_ = nullptr;
    QToolButton* fullscreenGame_ = nullptr;
};

class HierarchyPanel : public QWidget {
    Q_OBJECT

  public:
    explicit HierarchyPanel(QWidget* parent = nullptr);
    void bind(World& world0, AssetBrowser& browser0, EditorHistory& history0);
    // Rebuilds rows only when the object list, names or parents changed, so
    // selection and scroll state survive normal frames.
    void refresh(Entity selected);
    void selectEntity(Entity entity);
    const std::vector<Entity>& selection() const {
        return selection_;
    }
    void setSelection(const std::vector<Entity>& selection);
    void setLevelName(const QString& name);
    void setEditable(bool editable);

  Q_SIGNALS:
    void selectionChanged(Entity entity);
    void createRequested(int id);
    void renameLevelRequested();
    void hierarchyEdited();

  protected:
    void contextMenuEvent(QContextMenuEvent* event) override;

  private:
    struct Node {
        Entity entity{};
        QString label;
        QString icon;
        bool prefab = false;
        bool inactive = false;
    };
    void rebuildRows();
    void applySelectionToTree();
    void renameSelected();
    void duplicateSelected();
    void deleteSelected();
    void unparentSelected();
    void createChild(int id);
    void beginInlineRename(QTreeWidgetItem* item);
    void finishInlineRename(QTreeWidgetItem* item, int column);
    std::vector<Entity> visibleOrder() const;
    unsigned structureRevision() const;

    World* world_ = nullptr;
    AssetBrowser* browser_ = nullptr;
    EditorHistory* history_ = nullptr;
    QLineEdit* search_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QToolButton* createButton_ = nullptr;
    QLabel* levelLabel_ = nullptr;
    Entity selected_{};
    std::vector<Entity> selection_;
    std::vector<Node> rows_;
    unsigned builtRevision_ = ~1u;
    unsigned builtSearch_ = ~0u;
    bool editable_ = true;
    bool updating_ = false;
    QTreeWidgetItem* renameItem_ = nullptr;
};

class InspectorPanel : public QWidget {
    Q_OBJECT

  public:
    explicit InspectorPanel(QWidget* parent = nullptr);
    void bind(World& world0,
              AssetBrowser& browser0,
              EditorHistory& history0,
              Renderer& renderer0,
              TransformEditor& transform0);
    void setSelection(Entity entity);
    // The engine renders the selected camera's view into a texture; the panel
    // shows the read-back picture under the camera settings, as before.
    void setCameraPreview(const QImage& image);
    Entity selection() const {
        return selected_;
    }
    void refresh();
    void setEditable(bool editable);
    void clear();

  Q_SIGNALS:
    void selectionChanged(Entity entity);
    void inspectorEdited();

  private:
    void rebuild();
    void rebuildTransform(QVBoxLayout* layout);
    void rebuildHeader(QVBoxLayout* layout);
    void addComponentSections(QVBoxLayout* layout);
    void refreshValues();
    bool transformUndoActive() const { return transformUndoBusy_; }
    void setTransformUndoActive(bool active) { transformUndoBusy_ = active; }
    bool anySpinFocused() const;
    World* world_ = nullptr;
    AssetBrowser* browser_ = nullptr;
    EditorHistory* history_ = nullptr;
    Renderer* renderer_ = nullptr;
    TransformEditor* transform_ = nullptr;
    Entity selected_{};
    unsigned builtRevision_ = ~1u;
    unsigned builtEntity_ = ~0u;
    bool editable_ = true;
    bool updating_ = false;
    QScrollArea* scroll_ = nullptr;
    QWidget* content_ = nullptr;
    QLabel* header_ = nullptr;
    QLineEdit* nameEdit_ = nullptr;
    QCheckBox* activeCheck_ = nullptr;
    Vector3Edit* position_ = nullptr;
    Vector3Edit* rotation_ = nullptr;
    Vector3Edit* scale_ = nullptr;
    QCheckBox* staticCheck_ = nullptr;
    class TransformUndo* transformUndo_ = nullptr;
    bool transformUndoBusy_ = false;
    // Picture of the selected camera, filled by the engine each frame.
    PreviewView* cameraPreview_ = nullptr;
    QImage cameraPreviewImage_;
};

class AssetPanel : public QWidget {
    Q_OBJECT

  public:
    explicit AssetPanel(QWidget* parent = nullptr);
    void bind(World& world0, AssetBrowser& browser0, EditorHistory& history0, Renderer& renderer0);
    void refresh();
    void setEditable(bool editable);

  Q_SIGNALS:
    void spawnModel(const QString& path);
    void spawnPrefab(const QString& path);
    void editPrefab(const QString& path);
    void editCubemap(const QString& path);
    void editAnimation(const QString& path);
    void previewModel(const QString& path);
    void openScript(const QString& path);
    void importRequested();

  private:
    void rebuildTree();
    void selectPath(const QString& path);
    void showDetails(const QString& path);
    void activate(const QString& path);
    std::vector<std::string> currentFiles() const;
    QImage thumbnailImage(const std::string& path) const;

    World* world_ = nullptr;
    AssetBrowser* browser_ = nullptr;
    EditorHistory* history_ = nullptr;
    Renderer* renderer_ = nullptr;
    QLineEdit* pathEdit_ = nullptr;
    QLineEdit* search_ = nullptr;
    QTreeWidget* files_ = nullptr;
    QLabel* preview_ = nullptr;
    QLabel* details_ = nullptr;
    QToolButton* gridButton_ = nullptr;
    QToolButton* listButton_ = nullptr;
    bool grid_ = false;
    bool editable_ = true;
    unsigned builtRevision_ = ~1u;
    QString currentFolder_;
};

class ScriptPanel : public QWidget {
    Q_OBJECT

  public:
    explicit ScriptPanel(QWidget* parent = nullptr);
    void bind(World& world0, AssetBrowser& browser0, ScriptEditor& scripts0, EditorHistory& history0);
    void refresh();
    void openPath(const QString& path);
    QString currentPath() const;

  Q_SIGNALS:
    void scriptEdited();

  private:
    void loadCurrent();
    void saveCurrent();
    void runCurrent();
    World* world_ = nullptr;
    AssetBrowser* browser_ = nullptr;
    ScriptEditor* scripts_ = nullptr;
    EditorHistory* history_ = nullptr;
    QPlainTextEdit* code_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QString path_;
    bool dirty_ = false;
};

class ConsolePanel : public QWidget {
    Q_OBJECT

  public:
    explicit ConsolePanel(QWidget* parent = nullptr);
    void bind(ScriptEditor& scripts0);
    void refresh();
    // Extra sink so the engine can also print its own log lines (import
    // errors, level load failures) into the same console.
    void setLogger(std::function<void()> logger) {
        logger_ = std::move(logger);
    }

    void appendLine(const QString& text);

  private:
    void execute();
    ScriptEditor* scripts_ = nullptr;
    QPlainTextEdit* output_ = nullptr;
    QLineEdit* input_ = nullptr;
    std::function<void()> logger_;
    std::size_t printedLines_ = 0;
};

// Floating editor docks that replace the old windows.
// Profiler: an updating bar chart of where frame time goes, fed by the
// engine's Profiler through EditorWindow::setProfilerData().
class ProfilerDock : public QDockWidget {
    Q_OBJECT

  public:
    explicit ProfilerDock(QWidget* parent = nullptr);
    void setData(const FrameProfiler& profiler);

  Q_SIGNALS:
    void closed();

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    class FrameChart* chart_ = nullptr;
    QLabel* stats_ = nullptr;
};

class InputSettingsDock : public QDockWidget {
    Q_OBJECT

  public:
    explicit InputSettingsDock(QWidget* parent = nullptr);
    void bind(World& world0,
              AssetBrowser& browser0,
              InputSettings& settings0,
              EditorHistory& history0);
    void refresh();

  Q_SIGNALS:
    void closed();
    void bindingsChanged();

  public:
    // The dock edits the engine's own InputState, so bindings take effect on
    // the next polled frame without copying anything.
    void bindInput(InputState& input0);

  protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

  private:
    void rebuild();
    void applyCapturedBinding(int code);
    World* world_ = nullptr;
    AssetBrowser* browser_ = nullptr;
    InputSettings* settings_ = nullptr;
    EditorHistory* history_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLineEdit* newName_ = nullptr;
    QLabel* status_ = nullptr;
    bool capturing_ = false;
    bool configLocked_ = false;
    InputState* input_ = nullptr;
};

// Translates a Qt key press into the SDL scancode a binding stores.
int sdlCodeForQtKey(QKeyEvent* event);

// Frame-time bar chart for the profiler dock.
class FrameChart : public QWidget {
    Q_OBJECT

  public:
    explicit FrameChart(QWidget* parent = nullptr);
    void setFrameMs(double last, double average, float ceiling);

  protected:
    void paintEvent(QPaintEvent* event) override;

  private:
    std::vector<double> samples_;
    double last_ = 0, average_ = 0;
    float ceiling_ = 20.0f;
};

class RenderingSettingsDock : public QDockWidget {
    Q_OBJECT

  public:
    explicit RenderingSettingsDock(QWidget* parent = nullptr);
    void bind(World& world0,
              AssetBrowser& browser0,
              EditorSettings& editorSettings0,
              EditorHistory& history0,
              RenderingSettings& settings0);
    void refresh();

  Q_SIGNALS:
    void closed();
    void changed();

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    void apply();
    World* world_ = nullptr;
    AssetBrowser* browser_ = nullptr;
    EditorSettings* editorSettings_ = nullptr;
    EditorHistory* history_ = nullptr;
    RenderingSettings* settings_ = nullptr;
    QCheckBox* vsync_ = nullptr;
    QComboBox* shadowMode_ = nullptr;
    QComboBox* filtering_ = nullptr;
    QComboBox* resolution_ = nullptr;
    QLabel* driver_ = nullptr;
    QLabel* error_ = nullptr;
    bool loading_ = false;
};

// Dock wrapper around the engine's AnimationWindow data API: a timeline
// scrubber, the track list, clip settings and the isolated preview picture.
class AnimationDock : public QDockWidget {
    Q_OBJECT

  public:
    explicit AnimationDock(QWidget* parent = nullptr);
    void bind(World& world0,
              AssetBrowser& browser0,
              EditorHistory& history0,
              AnimationWindow& window0);
    void setSelection(Entity entity);
    // Called once per editor frame: advances playback and repaints the preview.
    void update(float dt);
    void refresh();

  Q_SIGNALS:
    void closed();
    void changed();

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    void build();
    void syncTimeline();
    void applyTimeline();
    void openClip(const QString& path);
    float sampleStep() const;

    World* world_ = nullptr;
    AssetBrowser* browser_ = nullptr;
    EditorHistory* history_ = nullptr;
    AnimationWindow* window_ = nullptr;
    Entity selected_{};
    QScrollArea* scroll_ = nullptr;
    QWidget* content_ = nullptr;
    QLabel* pathLabel_ = nullptr;
    QLabel* errorLabel_ = nullptr;
    QLabel* timeLabel_ = nullptr;
    QComboBox* clipPath_ = nullptr;
    QComboBox* property_ = nullptr;
    QLineEdit* target_ = nullptr;
    QLineEdit* draftPath_ = nullptr;
    IntEdit* samples_ = nullptr;
    FloatEdit* duration_ = nullptr;
    QCheckBox* loop_ = nullptr;
    QWidget* timelineHost_ = nullptr;
    QWidget* timeline_ = nullptr;
    PreviewView* preview_ = nullptr;
    unsigned builtRevision_ = ~1u;
    unsigned renderedRevision_ = 0;
    bool loading_ = false;
};

// Dock wrapper around the engine's LightmappingWindow data API.
class LightingDock : public QDockWidget {
    Q_OBJECT

  public:
    explicit LightingDock(QWidget* parent = nullptr);
    void bind(World& world0,
              AssetBrowser& browser0,
              EditorHistory& history0,
              LightmappingWindow& window0,
              const ProjectIO::Meshes& meshes0,
              const std::filesystem::path& level0);
    void setLevel(const std::filesystem::path& level0);
    void setSelection(Entity entity);
    void setEditable(bool editable);
    void refresh();

  Q_SIGNALS:
    void closed();
    void changed();

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    World* world_ = nullptr;
    AssetBrowser* browser_ = nullptr;
    EditorHistory* history_ = nullptr;
    LightmappingWindow* window_ = nullptr;
    std::filesystem::path level_;
    const ProjectIO::Meshes* meshes_ = nullptr;
    Entity selected_{};
    QScrollArea* scroll_ = nullptr;
    QWidget* content_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QLabel* status_ = nullptr;
    QTreeWidget* objects_ = nullptr;
    QComboBox* quality_ = nullptr;
    int builtRevision_ = -1;
    bool editable_ = true;
    bool loading_ = false;
};

// Terrain sculpting: drives Terrains::Editor, which owns the brush maths and
// undo gestures. Shown while an object with a Terrain component is selected.
class TerrainPanel : public QWidget {
    Q_OBJECT

  public:
    explicit TerrainPanel(QWidget* parent = nullptr);
    void bind(World& world0,
              AssetBrowser& browser0,
              EditorHistory& history0,
              Terrains::Editor& editor0);
    void setSelection(Entity entity);
    void setEditable(bool editable);
    void refresh();

  Q_SIGNALS:
    void terrainEdited();

  private:
    void rebuild();
    World* world_ = nullptr;
    AssetBrowser* browser_ = nullptr;
    EditorHistory* history_ = nullptr;
    Terrains::Editor* editor_ = nullptr;
    Entity selected_{};
    bool editable_ = true;
    unsigned builtEntity_ = ~0u;
    QScrollArea* scroll_ = nullptr;
    QWidget* content_ = nullptr;
};

// Model Preview: the engine renders a 720x405 preview into its own
// framebuffer; this dialog only presents the picture and the clip controls.
// Terrain sculpting lives in a dock too; see TerrainPanel below.
class ModelPreviewDialog : public QDialog {
    Q_OBJECT

  public:
    explicit ModelPreviewDialog(QWidget* parent = nullptr);
    void bind(AssetManager& assets0, Renderer& renderer0, ModelPreviewWindow& window0);
    void openPath(const QString& path);
    void refresh(float dt);

  Q_SIGNALS:
    void closed();
    void changed();

  protected:
    void closeEvent(QCloseEvent* event) override;
    void showEvent(QShowEvent* event) override;

  private:
    void sync();
    AssetManager* assets_ = nullptr;
    Renderer* renderer_ = nullptr;
    ModelPreviewWindow* window_ = nullptr;
    PreviewView* view_ = nullptr;
    QComboBox* clip_ = nullptr;
    class QSlider* time_ = nullptr;
    QLabel* duration_ = nullptr;
    QLabel* error_ = nullptr;
    QCheckBox* loop_ = nullptr;
    QCheckBox* blend_ = nullptr;
    FloatEdit* fade_ = nullptr;
    QToolButton* play_ = nullptr;
    bool loading_ = false;
    bool dirty_ = true;
};

// Layer names: project-wide list the Inspector's Layer combo reads from.
class LayersDock : public QDockWidget {
    Q_OBJECT

  public:
    explicit LayersDock(QWidget* parent = nullptr);
    void bind(AssetBrowser& browser0);
    void refresh();

  Q_SIGNALS:
    void closed();
    void changed();

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    AssetBrowser* browser_ = nullptr;
    QListWidget* list_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLabel* error_ = nullptr;
    bool loading_ = false;
};

// Prefab mode: the level is swapped for a single prefab's tree. Save writes
// the prefab and refreshes instances; Exit restores the level.
class PrefabDock : public QDockWidget {
    Q_OBJECT

  public:
    explicit PrefabDock(QWidget* parent = nullptr);
    void setPrefab(const QString& path);
    void clear();

  Q_SIGNALS:
    void closed();
    void saveRequested();
    void exitRequested();

  protected:
    void closeEvent(QCloseEvent* event) override;

  private:
    QLabel* path_ = nullptr;
};

// Cubemap editor: six face paths, saved back to the .cubemap asset. The
// browser used to open this as an immediate-mode window.
class CubemapDialog : public QDialog {
    Q_OBJECT

  public:
    explicit CubemapDialog(QWidget* parent = nullptr);
    void openPath(AssetManager& assets0, const QString& path);

  Q_SIGNALS:
    void saved(const QString& path);

  private:
    void save();
    AssetManager* assets_ = nullptr;
    QString path_;
    std::array<TextRow*, 6> faces_{};
    QLabel* error_ = nullptr;
};

// The Editor Settings dialog: themes, FPS cap, autosave and the scene grid.
class SettingsDialog : public QDialog {
    Q_OBJECT

  public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    void bind(EditorSettings& settings0,
              const std::filesystem::path& projectFolder,
              AssetBrowser& browser0);
    void refresh();
    void setThemeRequested(int theme);

  Q_SIGNALS:
    void themeSelected(int theme);
    void settingsChanged();

  private:
    EditorSettings* settings_ = nullptr;
    AssetBrowser* browser_ = nullptr;
    std::filesystem::path folder_;
    QComboBox* theme_ = nullptr;
    QCheckBox* colliders_ = nullptr;
    FloatEdit* raycast_ = nullptr;
    IntEdit* fps_ = nullptr;
    IntEdit* autosave_ = nullptr;
    QCheckBox* gridVisible_ = nullptr;
    QComboBox* gridPlane_ = nullptr;
    IntEdit* gridCells_ = nullptr;
    FloatEdit* gridSpacing_ = nullptr;
    Vector3Edit* gridPosition_ = nullptr;
    ColorEdit* gridColour_ = nullptr;
    QComboBox* codeEditor_ = nullptr;
    QLabel* error_ = nullptr;
    bool loading_ = false;
};

} // namespace qt
} // namespace ui
