#pragma once

#include <QString>
#include <memory>

struct SDL_Window;

namespace ui {
namespace qt {

class EditorWindow;

// The Qt application object.
//
// The engine keeps its own frame loop and SDL keeps the window, input and
// audio backends. Qt supplies the editor's widget layer and its event
// dispatcher, which is pumped once per engine frame by processEvents(). That
// keeps Qt's event ordering, painting and modal dialogs working without
// handing control of the loop to Qt, so the engine's timing, profiling and
// shutdown paths stay exactly as they were.
class Application {
  public:
    // Creates the QApplication and the main editor window, parented to the
    // SDL window (which stays hidden: Qt draws the editor frame).
    static bool create(SDL_Window* window);
    static void destroy();

    // Drains pending Qt events and repaints dirty widgets. Called once per
    // engine frame, after the world has been updated.
    static void processEvents();

    // True once create() succeeded and the engine should keep running.
    static bool active();

    static EditorWindow* window();

    // Native clipboard bridge, used by the value editors. Qt owns the
    // clipboard, so no SDL clipboard state is involved.
    static void setClipboardText(const QString& text);
    static QString clipboardText();
};

} // namespace qt
} // namespace ui
