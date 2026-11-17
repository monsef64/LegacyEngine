#include "ui/qt/qt_framebuffer.hpp"

#include <glad/gl.h>

namespace ui {
namespace qt {

QImage readFramebuffer(unsigned framebuffer, int width, int height) {
    if (!framebuffer || width <= 0 || height <= 0) {
        return {};
    }
    QImage image(width, height, QImage::Format_RGBA8888);
    GLint previous = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
    glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(previous));
    // OpenGL's origin is bottom-left; Qt's is top-left.
    return image.mirrored(false, true);
}

} // namespace qt
} // namespace ui
