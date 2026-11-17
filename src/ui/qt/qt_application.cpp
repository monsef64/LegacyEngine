#include "ui/qt/qt_application.hpp"

#include "ui/qt/qt_editor_window.hpp"
#include "ui/qt/qt_input_bridge.hpp"

#include <QApplication>
#include <QClipboard>
#include <QEventLoop>
#include <QGuiApplication>
#include <QWidget>

#include <memory>

namespace ui {
namespace qt {

namespace {

struct State {
    std::unique_ptr<QApplication> app;
    std::unique_ptr<EditorWindow> window;
    int argc = 1;
    char arg0[16] = "legacyengine";
    char* argv[2] = {arg0, nullptr};
};

State& state() {
    static State instance;
    return instance;
}

} // namespace

bool Application::create(SDL_Window* window) {
    State& s = state();
    if (!s.app) {
        s.app = std::make_unique<QApplication>(s.argc, s.argv);
        // The engine decides when the editor closes; Qt must not exit when the
        // last window is hidden during shutdown or prefab mode.
        s.app->setQuitOnLastWindowClosed(false);
    }
    if (!s.window) {
        s.window = std::make_unique<EditorWindow>(window);
    }
    s.window->show();
    s.window->raise();
    return s.app != nullptr;
}

void Application::destroy() {
    State& s = state();
    if (!s.window) {
        return;
    }
    // Take the window off screen and stop it painting before the tree goes
    // away. The panels read the engine's world while they paint, so a paint
    // delivered during teardown could touch an object that is already gone.
    s.window->hide();
    s.window->setAttribute(Qt::WA_DontShowOnScreen, true);
    s.app->processEvents(QEventLoop::ExcludeUserInputEvents, 0);
    // The input filter lives on the application, so it has to go before the
    // application does.
    removeInputBridge();
    s.window.reset();
    s.app.reset();
}

void Application::processEvents() {
    State& s = state();
    if (!s.app) {
        return;
    }
    // Exclude user-input for the app-level drain so a long engine frame is not
    // starved, but still let Qt paint and run queued slots.
    s.app->processEvents(QEventLoop::AllEvents, 0);
}

bool Application::active() {
    return state().app != nullptr;
}

EditorWindow* Application::window() {
    return state().window.get();
}

void Application::setClipboardText(const QString& text) {
    if (QClipboard* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(text);
    }
}

QString Application::clipboardText() {
    if (QClipboard* clipboard = QGuiApplication::clipboard()) {
        return clipboard->text();
    }
    return {};
}

} // namespace qt
} // namespace ui
