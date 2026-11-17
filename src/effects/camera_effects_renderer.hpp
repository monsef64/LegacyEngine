#pragma once
#include "effects/camera_effects.hpp"
#include "rendering/shader_data.hpp"
#include "scene/scene_camera.hpp"
#include <glad/gl.h>
#include <map>
namespace CameraFx {
class Renderer {
    struct Target {
        GLuint fbo = 0, color = 0, depth = 0;
        int width = 0, height = 0;
    };
    struct Buffers {
        Target scene, blur[2];
        std::uint64_t used = 0;
    };
    std::map<std::uint64_t, Buffers> buffers_;
    std::uint64_t frame_ = 0;
    std::map<std::string, GLint, std::less<>> locations_;
    GLuint program_ = 0, vao_ = 0;
    struct History {
        Mat4 vp;
        int width, height;
        double time;
    };
    std::map<std::uint64_t, History> history_;
    static void release(Target& t) {
        if (t.fbo) {
            glDeleteFramebuffers(1, &t.fbo);
        }
        if (t.color) {
            glDeleteTextures(1, &t.color);
        }
        if (t.depth) {
            glDeleteTextures(1, &t.depth);
        }
        t = {};
    }
    static void resize(Target& t, int w, int h, bool depth) {
        if (t.width == w && t.height == h) {
            return;
        }
        release(t);
        t.width = w;
        t.height = h;
        glGenFramebuffers(1, &t.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
        glGenTextures(1, &t.color);
        glBindTexture(GL_TEXTURE_2D, t.color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.color, 0);
        if (depth) {
            glGenTextures(1, &t.depth);
            glBindTexture(GL_TEXTURE_2D, t.depth);
            glTexImage2D(GL_TEXTURE_2D,
                         0,
                         GL_DEPTH24_STENCIL8,
                         w,
                         h,
                         0,
                         GL_DEPTH_STENCIL,
                         GL_UNSIGNED_INT_24_8,
                         nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(
                GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, t.depth, 0);
        }
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            release(t);
            throw std::runtime_error("CameraEffects framebuffer incomplete");
        }
    }
    static GLuint compile(GLenum type, const std::string& text) {
        auto id = glCreateShader(type);
        const char* p = text.c_str();
        glShaderSource(id, 1, &p, nullptr);
        glCompileShader(id);
        GLint ok;
        glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048]{};
            glGetShaderInfoLog(id, sizeof(log), nullptr, log);
            glDeleteShader(id);
            throw std::runtime_error(std::string("CameraEffects shader: ") + log);
        }
        return id;
    }
    GLint loc(const char* n) {
        auto it = locations_.find(n);
        if (it != locations_.end()) {
            return it->second;
        }
        return locations_.emplace(n, glGetUniformLocation(program_, n)).first->second;
    }
    void init() {
        if (program_) {
            return;
        }
        GLuint v = 0, f = 0;
        try {
            v = compile(GL_VERTEX_SHADER, loadShaderData("camera_effects.vert"));
            f = compile(GL_FRAGMENT_SHADER, loadShaderData("camera_effects.frag"));
            program_ = glCreateProgram();
            glAttachShader(program_, v);
            glAttachShader(program_, f);
            glLinkProgram(program_);
            GLint ok;
            glGetProgramiv(program_, GL_LINK_STATUS, &ok);
            if (!ok) {
                throw std::runtime_error("CameraEffects shader link failed");
            }
            glDeleteShader(v);
            glDeleteShader(f);
            glGenVertexArrays(1, &vao_);
        } catch (...) {
            if (v) {
                glDeleteShader(v);
            }
            if (f) {
                glDeleteShader(f);
            }
            if (program_) {
                glDeleteProgram(program_);
            }
            program_ = 0;
            throw;
        }
    }

  public:
    void shutdown() {
        for (auto& entry : buffers_) {
            release(entry.second.scene);
            for (auto& target : entry.second.blur) {
                release(target);
            }
        }
        buffers_.clear();
        locations_.clear();
        if (program_) {
            glDeleteProgram(program_);
        }
        if (vao_) {
            glDeleteVertexArrays(1, &vao_);
        }
        program_ = vao_ = 0;
        history_.clear();
    }
    template <class Draw>
    void render(const CameraEffects& c,
                int w,
                int h,
                int x,
                int y,
                const Mat4& vp,
                Vec3 eye,
                std::uint64_t key,
                Draw draw) {
        GLint framebuffer, readFramebuffer, active, oldProgram, oldVao, bindings[3], viewport[4];
        GLboolean depthMask;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        const bool depth = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND),
                   cull = glIsEnabled(GL_CULL_FACE), scissor = glIsEnabled(GL_SCISSOR_TEST);
        for (int i = 0; i < 3; ++i) {
            glActiveTexture(GL_TEXTURE0 + i);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &bindings[i]);
        }
        auto restore = [&] {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
            glViewport(x, y, w, h);
            for (auto pair : {std::make_pair(GL_DEPTH_TEST, depth),
                              std::make_pair(GL_BLEND, blend),
                              std::make_pair(GL_CULL_FACE, cull),
                              std::make_pair(GL_SCISSOR_TEST, scissor)}) {
                if (pair.second) {
                    glEnable(pair.first);
                } else {
                    glDisable(pair.first);
                }
            }
            glDepthMask(depthMask);
            for (int i = 0; i < 3; ++i) {
                glActiveTexture(GL_TEXTURE0 + i);
                glBindTexture(GL_TEXTURE_2D, bindings[i]);
            }
            glActiveTexture(active);
            glUseProgram(oldProgram);
            glBindVertexArray(oldVao);
        };
        try {
            init();
            if (!buffers_.count(key) && buffers_.size() >= 2) {
                auto oldest = buffers_.begin();
                for (auto it = buffers_.begin(); it != buffers_.end(); ++it) {
                    if (it->second.used < oldest->second.used) {
                        oldest = it;
                    }
                }
                release(oldest->second.scene);
                for (auto& target : oldest->second.blur) {
                    release(target);
                }
                history_.erase(oldest->first);
                buffers_.erase(oldest);
            }
            auto& buffers = buffers_[key];
            buffers.used = ++frame_;
            auto& scene_ = buffers.scene;
            auto& blur_ = buffers.blur;
            resize(scene_, w, h, true);
            glBindFramebuffer(GL_FRAMEBUFFER, scene_.fbo);
            draw();
            glUseProgram(program_);
            glBindVertexArray(vao_);
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glDisable(GL_CULL_FACE);
            glDisable(GL_BLEND);
            glDisable(GL_SCISSOR_TEST);
            glUniform1i(loc("sourceImage"), 0);
            glUniform1i(loc("depthImage"), 1);
            glUniform1i(loc("bloomImage"), 2);
            glUniform1i(loc("flags"), mask(c));
            for (const auto& f : floatFields) {
                glUniform1f(loc(f.name), c.*f.member);
            }
            auto bind = [&](GLuint source, int width, int height) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, source);
                glUniform2f(loc("texel"), 1.f / width, 1.f / height);
            };
            if (c.bloom && c.bloomIntensity > 0) {
                int bw = std::max(1, w / 2), bh = std::max(1, h / 2);
                resize(blur_[0], bw, bh, false);
                resize(blur_[1], bw, bh, false);
                glBindFramebuffer(GL_FRAMEBUFFER, blur_[0].fbo);
                glViewport(0, 0, bw, bh);
                glUniform1i(loc("pass"), 0);
                bind(scene_.color, w, h);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                glUniform1i(loc("pass"), 1);
                for (int i = 0; i < 2; ++i) {
                    glBindFramebuffer(GL_FRAMEBUFFER, blur_[1 - i].fbo);
                    bind(blur_[i].color, bw, bh);
                    glUniform2f(loc("direction"), i == 0 ? 1.f : 0.f, i == 1 ? 1.f : 0.f);
                    glDrawArrays(GL_TRIANGLES, 0, 3);
                }
            }
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
            glViewport(x, y, w, h);
            glUniform1i(loc("pass"), 2);
            bind(scene_.color, w, h);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, scene_.depth);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, c.bloom && c.bloomIntensity > 0 ? blur_[0].color : 0);
            auto inv = inverseCameraMatrix(vp);
            glUniformMatrix4fv(loc("inverseVP"), 1, GL_FALSE, inv.m);
            glUniform3f(loc("eye"), eye.x, eye.y, eye.z);
            double now = double(SDL_GetTicksNS()) * 1e-9;
            auto it = history_.find(key);
            bool history = it != history_.end() && it->second.width == w &&
                           it->second.height == h && now - it->second.time < .2 &&
                           now > it->second.time;
            float difference = 0;
            if (history) {
                for (int i = 0; i < 16; ++i) {
                    difference = std::max(difference, std::abs(vp.m[i] - it->second.vp.m[i]));
                }
            }
            history &= difference < 2.f;
            const auto previous = history ? it->second.vp : vp;
            glUniformMatrix4fv(loc("previousVP"), 1, GL_FALSE, previous.m);
            glUniform1f(loc("motionScale"),
                        history ? float(std::clamp((1. / 60) / (now - it->second.time), 0., 2.))
                                : 0);
            glUniform3f(loc("fogColor"), c.fogR, c.fogG, c.fogB);
            glUniform1f(loc("fogDensity"), c.fogDensity);
            glUniform1f(loc("fogStart"), c.fogStart);
            glUniform1f(loc("fogEnd"), c.fogEnd);
            glUniform3f(loc("fadeColor"), c.fadeR, c.fadeG, c.fadeB);
            glUniform1f(loc("fadeAmount"), c.fadeAmount);
            glUniform1f(loc("time"), float(std::fmod(now, 1000.)));
            glDrawArrays(GL_TRIANGLES, 0, 3);
            // Preserve scene depth for post-render debug overlays and later cameras.
            glBindFramebuffer(GL_READ_FRAMEBUFFER, scene_.fbo);
            glBlitFramebuffer(0, 0, w, h, x, y, x + w, y + h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
            if (history_.size() >= 64 && !history_.count(key)) {
                history_.erase(history_.begin());
            }
            history_[key] = {vp, w, h, now};
            restore();
        } catch (...) {
            restore();
            throw;
        }
    }
};
} // namespace CameraFx
