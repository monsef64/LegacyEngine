#pragma once

#include <QImage>

namespace ui {
namespace qt {

// Reads an engine framebuffer into a QImage, flipping to Qt's top-left origin.
// This is the only place the Qt layer touches OpenGL, and it only ever reads a
// surface the engine already rendered in its own context, so the editor keeps
// exactly one GL context (the SDL one). It must be called while that context is
// current, i.e. from inside the engine frame.
QImage readFramebuffer(unsigned framebuffer, int width, int height);

} // namespace qt
} // namespace ui
