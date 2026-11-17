// Headless smoke test for the Qt editor shell.
//
// Builds the whole widget tree offscreen, drives a few frames through the same
// entry points the engine uses, and checks that the panels report sane
// geometry. This catches the failures that only show up at run time: a missing
// signal connection, a dock that never gets its widget, a null panel pointer.
//
// It compiles and runs without a GPU: QT_QPA_PLATFORM=offscreen is enough for
// widgets, and no engine renderer is created.
#include "ui/qt/qt_editor_window.hpp"
#include "ui/qt/qt_panels.hpp"
#include "ui/qt/qt_viewport.hpp"

#include <QApplication>
#include <QImage>
#include <cstdio>
#include <cstdlib>

namespace {

int failures = 0;

void require(bool ok, const char* message) {
    if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

} // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // The window builds from nothing but the SDL window: every panel owns the
    // data it shows, and bind() attaches the engine afterwards.
    ui::qt::EditorWindow window(nullptr);
    require(window.sceneView() != nullptr, "Scene view is created");
    require(window.gameView() != nullptr, "Game view is created");
    require(window.hierarchyPanel() != nullptr, "Hierarchy panel is created");
    require(window.inspectorPanel() != nullptr, "Inspector panel is created");
    require(window.assetPanel() != nullptr, "Asset panel is created");
    require(window.consolePanel() != nullptr, "Console panel is created");
    require(window.scriptPanel() != nullptr, "Script panel is created");
    require(window.toolbar() != nullptr, "Toolbar is created");

    // The menus and docks the engine drives by name must all exist.
    for (const char* name :
         {"Scene", "Game", "Hierarchy", "Inspector", "Assets", "Script", "Console"}) {
        require(window.panel(name) != nullptr, name);
    }

    window.show();
    window.resize(1280, 800);

    // Report geometry the way the engine does, then push a picture back in.
    const QSize scene = window.scenePixelSize();
    const QSize game = window.gamePixelSize();
    require(scene.width() > 0 && scene.height() > 0, "Scene picture has a size");
    require(game.width() > 0 && game.height() > 0, "Game picture has a size");

    QImage picture(scene, QImage::Format_RGBA8888);
    picture.fill(QColor(30, 30, 36));
    window.setSceneSurfaceImage(picture);
    window.setGameSurfaceImage(picture);

    // A pointer state must survive a round trip through the shell.
    auto& pointer = window.scenePointer();
    require(pointer.u >= 0.0f && pointer.u <= 1.0f, "pointer u is in picture space");
    require(pointer.v >= 0.0f && pointer.v <= 1.0f, "pointer v is in picture space");

    // Frames: the engine calls these in this order every tick.
    window.setFrameInfo(60.0f, 1.0f / 60.0f, 16.0);
    window.setStatus(QStringLiteral("Ready"), QStringLiteral("Main"));
    window.setLevelName(QStringLiteral("Main"));
    window.setProjectName(QStringLiteral("Untitled"));
    window.setLevelMenu(true, {"Main", "Second"}, "Main");
    window.setRecentProjects({});
    window.setSeparateGame(false, false);
    window.setPlayState(false, false, false);
    window.setEditable(true);
    window.refresh(16.0);
    window.consumePointerEvents();


    // Theme switching is a menu action the engine forwards.
    for (int theme = 0; theme < 3; ++theme) {
        window.applyTheme(theme);
    }

    // Actions recorded by widgets must be readable and clearable.
    require(window.actions.spawnModel.empty(), "no action was requested by default");
    window.actions.clear();

    // The Layout menu writes and restores a file; a missing one must not throw.
    window.resetLayout();
    require(window.panel("Scene") != nullptr, "Scene survives a layout reset");

    // Shutdown: the window closes without leaving Qt in a bad state.
    window.requestClose();
    app.processEvents();

    std::printf("Qt editor shell smoke test: %d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
