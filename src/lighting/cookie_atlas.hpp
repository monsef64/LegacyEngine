#pragma once
#include "lighting/lighting.hpp"
#include "rendering/material_textures.hpp"
#include <array>
#include <glad/gl.h>

// One texture unit for every visible spotlight, including on OpenGL 3.3 hardware.
// Reuse the asynchronous image cache; copy/resample resident images into per-light layers.
class CookieAtlas {
    GLuint texture_ = 0, read_ = 0, draw_ = 0;
    struct Layer {
        std::string path;
        std::uint64_t generation = 0;
    };
    std::array<Layer, Lighting::maxLights> layers_{};
    std::filesystem::path root_;

  public:
    void shutdown() {
        if (texture_) {
            glDeleteTextures(1, &texture_);
        }
        if (read_) {
            glDeleteFramebuffers(1, &read_);
        }
        if (draw_) {
            glDeleteFramebuffers(1, &draw_);
        }
        texture_ = read_ = draw_ = 0;
        layers_ = {};
        root_.clear();
    }
    GLuint texture() const {
        return texture_;
    }
    std::array<bool, Lighting::maxLights> update(const AssetManager& assets,
                                                 MaterialTextureCache& cache,
                                                 const std::vector<Lighting::Light>& lights,
                                                 std::string& error) {
        if (root_ != assets.root) {
            layers_ = {};
            root_ = assets.root;
        }
        std::array<bool, Lighting::maxLights> enabled{};
        bool any = false;
        for (const auto& light : lights) {
            any |= light.type == 2 && !light.settings.cookie.empty();
        }
        if (!any) {
            return enabled;
        }
        GLint oldRead, oldDraw, active, binding;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &oldRead);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &oldDraw);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
        GLint arrayBinding;
        glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &arrayBinding);
        if (!texture_) {
            glGenTextures(1, &texture_);
            glBindTexture(GL_TEXTURE_2D_ARRAY, texture_);
            glTexImage3D(GL_TEXTURE_2D_ARRAY,
                         0,
                         GL_RGBA8,
                         Lighting::cookieResolution,
                         Lighting::cookieResolution,
                         Lighting::maxLights,
                         0,
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         nullptr);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glGenFramebuffers(1, &read_);
            glGenFramebuffers(1, &draw_);
        }
        for (std::size_t i = 0; i < lights.size(); ++i) {
            const auto& l = lights[i];
            if (l.type != 2 || l.settings.cookie.empty()) {
                continue;
            }
            const GLuint source = cache.get(assets, l.settings.cookie, error, true);
            if (!source) {
                continue;
            }
            const auto generation = cache.generation(l.settings.cookie, true);
            if (layers_[i].path == l.settings.cookie && layers_[i].generation == generation) {
                enabled[i] = true;
                continue;
            }
            glBindTexture(GL_TEXTURE_2D, source);
            GLint w = 0, h = 0;
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
            if (w <= 0 || h <= 0) {
                continue;
            }
            glBindFramebuffer(GL_READ_FRAMEBUFFER, read_);
            glFramebufferTexture2D(
                GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, source, 0);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw_);
            glFramebufferTextureLayer(
                GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, texture_, 0, GLint(i));
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE &&
                glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
                glBlitFramebuffer(0,
                                  0,
                                  w,
                                  h,
                                  0,
                                  0,
                                  Lighting::cookieResolution,
                                  Lighting::cookieResolution,
                                  GL_COLOR_BUFFER_BIT,
                                  GL_LINEAR);
                layers_[i] = {l.settings.cookie, generation};
                enabled[i] = true;
            } else {
                error = "Cannot build spotlight cookie atlas";
            }
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, oldRead);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, oldDraw);
        glBindTexture(GL_TEXTURE_2D, binding);
        glBindTexture(GL_TEXTURE_2D_ARRAY, arrayBinding);
        glActiveTexture(active);
        return enabled;
    }
};
