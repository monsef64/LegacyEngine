#include "ui/qt/qt_input_bridge.hpp"

#include <SDL3/SDL.h>

#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QObject>
#include <QWidget>


namespace ui {
namespace qt {

namespace {

// Qt key codes for modifiers map onto the SDL scancodes the engine reads with
// SDL_GetKeyboardState(). Everything else is a plain letter, digit or named
// key, so the table stays small.
SDL_Scancode scancodeFor(const QKeyEvent& event) {
    const int key = event.key();
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        return static_cast<SDL_Scancode>(SDL_SCANCODE_A + (key - Qt::Key_A));
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return static_cast<SDL_Scancode>(SDL_SCANCODE_0 + (key - Qt::Key_0));
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F12) {
        return static_cast<SDL_Scancode>(SDL_SCANCODE_F1 + (key - Qt::Key_F1));
    }
    switch (key) {
    case Qt::Key_Space:
        return SDL_SCANCODE_SPACE;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return SDL_SCANCODE_RETURN;
    case Qt::Key_Escape:
        return SDL_SCANCODE_ESCAPE;
    case Qt::Key_Tab:
        return SDL_SCANCODE_TAB;
    case Qt::Key_Backspace:
        return SDL_SCANCODE_BACKSPACE;
    case Qt::Key_Delete:
        return SDL_SCANCODE_DELETE;
    case Qt::Key_Insert:
        return SDL_SCANCODE_INSERT;
    case Qt::Key_Home:
        return SDL_SCANCODE_HOME;
    case Qt::Key_End:
        return SDL_SCANCODE_END;
    case Qt::Key_PageUp:
        return SDL_SCANCODE_PAGEUP;
    case Qt::Key_PageDown:
        return SDL_SCANCODE_PAGEDOWN;
    case Qt::Key_Left:
        return SDL_SCANCODE_LEFT;
    case Qt::Key_Right:
        return SDL_SCANCODE_RIGHT;
    case Qt::Key_Up:
        return SDL_SCANCODE_UP;
    case Qt::Key_Down:
        return SDL_SCANCODE_DOWN;
    case Qt::Key_Shift:
        return SDL_SCANCODE_LSHIFT;
    case Qt::Key_Control:
        return SDL_SCANCODE_LCTRL;
    case Qt::Key_Alt:
        return SDL_SCANCODE_LALT;
    case Qt::Key_Meta:
        return SDL_SCANCODE_LGUI;
    case Qt::Key_CapsLock:
        return SDL_SCANCODE_CAPSLOCK;
    case Qt::Key_Minus:
        return SDL_SCANCODE_MINUS;
    case Qt::Key_Equal:
        return SDL_SCANCODE_EQUALS;
    case Qt::Key_BracketLeft:
        return SDL_SCANCODE_LEFTBRACKET;
    case Qt::Key_BracketRight:
        return SDL_SCANCODE_RIGHTBRACKET;
    case Qt::Key_Backslash:
        return SDL_SCANCODE_BACKSLASH;
    case Qt::Key_Semicolon:
        return SDL_SCANCODE_SEMICOLON;
    case Qt::Key_Apostrophe:
        return SDL_SCANCODE_APOSTROPHE;
    case Qt::Key_Comma:
        return SDL_SCANCODE_COMMA;
    case Qt::Key_Period:
        return SDL_SCANCODE_PERIOD;
    case Qt::Key_Slash:
        return SDL_SCANCODE_SLASH;
    case Qt::Key_QuoteLeft:
        return SDL_SCANCODE_GRAVE;
    default:
        return SDL_SCANCODE_UNKNOWN;
    }
}

SDL_Keymod modifiersFor(Qt::KeyboardModifiers modifiers) {
    SDL_Keymod result = SDL_KMOD_NONE;
    if (modifiers & Qt::ShiftModifier) {
        result |= SDL_KMOD_SHIFT;
    }
    if (modifiers & Qt::ControlModifier) {
        result |= SDL_KMOD_CTRL;
    }
    if (modifiers & Qt::AltModifier) {
        result |= SDL_KMOD_ALT;
    }
    if (modifiers & Qt::MetaModifier) {
        result |= SDL_KMOD_GUI;
    }
    return result;
}

class Filter : public QObject {
  public:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() != QEvent::KeyPress && event->type() != QEvent::KeyRelease) {
            return QObject::eventFilter(watched, event);
        }
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->isAutoRepeat()) {
            return QObject::eventFilter(watched, event);
        }
        // Text fields keep every key they handle: typing in an inspector field
        // must not also move the camera or toggle play mode.
        if (auto* widget = qobject_cast<QWidget*>(watched)) {
            if (widget->inherits("QLineEdit") || widget->inherits("QTextEdit") ||
                widget->inherits("QPlainTextEdit") || widget->inherits("QAbstractSpinBox")) {
                return QObject::eventFilter(watched, event);
            }
        }
        const SDL_Scancode scancode = scancodeFor(*key);
        if (scancode == SDL_SCANCODE_UNKNOWN) {
            return QObject::eventFilter(watched, event);
        }
        SDL_Event translated{};
        const bool down = key->type() == QEvent::KeyPress;
        translated.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        translated.key.scancode = scancode;
        translated.key.key =
            down ? SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false) : SDLK_UNKNOWN;
        translated.key.mod = modifiersFor(key->modifiers());
        translated.key.down = down;
        translated.key.repeat = false;
        // Pushing into SDL's queue keeps SDL_GetKeyboardState() (which the
        // engine reads for W/E/R and camera movement) in step with the keys
        // the user is really holding.
        SDL_PushEvent(&translated);
        return QObject::eventFilter(watched, event);
    }
};

Filter*& filterSlot() {
    static Filter* instance = nullptr;
    return instance;
}

Filter* filter() {
    return filterSlot();
}

} // namespace

void installInputBridge() {
    if (filter()) {
        return;
    }
    filterSlot() = new Filter;
    QCoreApplication::instance()->installEventFilter(filter());
}

void removeInputBridge() {
    if (!filter()) {
        return;
    }
    QCoreApplication::instance()->removeEventFilter(filter());
    delete filterSlot();
    filterSlot() = nullptr;
}

} // namespace qt
} // namespace ui
