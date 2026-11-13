#pragma once
#include <algorithm>
#include <glad/gl.h>
#include <stdexcept>
class SceneSurface {
  public:
    unsigned texture = 0, framebuffer = 0, depth = 0;
    int width = 0, height = 0;
    ~SceneSurface() {
        if (depth) {
            glDeleteRenderbuffers(1, &depth);
        }
        if (texture) {
            glDeleteTextures(1, &texture);
        }
        if (framebuffer) {
            glDeleteFramebuffers(1, &framebuffer);
        }
    }
    void resize(int w, int h) {
        w = std::clamp(w, 1, 8192);
        h = std::clamp(h, 1, 8192);
        if (width == w && height == h) {
            return;
        }
        if (!framebuffer) {
            glGenFramebuffers(1, &framebuffer);
            glGenTextures(1, &texture);
            glGenRenderbuffers(1, &depth);
        }
        width = w;
        height = h;
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindRenderbuffer(GL_RENDERBUFFER, depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        glFramebufferRenderbuffer(
            GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            throw std::runtime_error("Scene framebuffer is incomplete");
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
};
