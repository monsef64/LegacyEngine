#pragma once

namespace ui {
namespace qt {

// Keyboard bridge.
//
// The SDL window only carries the OpenGL context now: the Qt window is the one
// the user types into, so SDL never sees a key press by itself. This bridge
// listens on the Qt application, translates key events into the SDL events the
// engine already speaks, and pushes them into SDL's own event queue. The
// engine's event loop then handles them exactly as it did before: input
// bindings, capture, and SDL_GetKeyboardState() all keep working, and nothing
// in the engine has to know Qt exists.
//
// Keys typed into a text field are left alone, so typing in an inspector never
// moves the camera or toggles Play.
void installInputBridge();
void removeInputBridge();

} // namespace qt
} // namespace ui
