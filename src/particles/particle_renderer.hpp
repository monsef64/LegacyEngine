#pragma once
#include "../../third_party/stb/stb_image.h"
#include "core/hierarchy.hpp"
#include "rendering/material_textures.hpp"
#include "rendering/shader_data.hpp"
#include <array>
#include <cstddef>
#include <glad/gl.h>

class ParticleRenderer {
    GLuint program_ = 0, vao_ = 0, buffer_ = 0;
    std::array<GLuint, 3> sprites_{};
    MaterialTextureCache textures_;
    std::filesystem::path root_;
    int vp_ = -1, right_ = -1, up_ = -1, sheet_ = -1, texture_ = -1;
    struct Instance {
        Vec3 position;
        float size, rotation;
        Vec3 color;
        float alpha, frame;
    };
    std::vector<Instance> instances_;
    std::string error_;
    bool attempted_ = false;
    static GLuint shader(GLenum type, const char* source) {
        auto object = glCreateShader(type);
        glShaderSource(object, 1, &source, nullptr);
        glCompileShader(object);
        GLint ok = 0;
        glGetShaderiv(object, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024]{};
            glGetShaderInfoLog(object, sizeof(log), nullptr, log);
            glDeleteShader(object);
            throw std::runtime_error(log);
        }
        return object;
    }
    void init() {
        attempted_ = true;
        GLuint vertex = 0, fragment = 0;
        try {
            vertex = shader(GL_VERTEX_SHADER, loadShaderData("particles.vert").c_str());
            fragment = shader(GL_FRAGMENT_SHADER, loadShaderData("particles.frag").c_str());
            program_ = glCreateProgram();
            glAttachShader(program_, vertex);
            glAttachShader(program_, fragment);
            glLinkProgram(program_);
            GLint ok = 0;
            glGetProgramiv(program_, GL_LINK_STATUS, &ok);
            if (!ok) {
                throw std::runtime_error("Cannot link particle shader");
            }
            glDeleteShader(vertex);
            vertex = 0;
            glDeleteShader(fragment);
            fragment = 0;
            vp_ = glGetUniformLocation(program_, "uVP");
            right_ = glGetUniformLocation(program_, "uRight");
            up_ = glGetUniformLocation(program_, "uUp");
            sheet_ = glGetUniformLocation(program_, "uSheet");
            texture_ = glGetUniformLocation(program_, "uTexture");
            glGenVertexArrays(1, &vao_);
            glGenBuffers(1, &buffer_);
            glBindVertexArray(vao_);
            glBindBuffer(GL_ARRAY_BUFFER, buffer_);
            const int sizes[]{3, 1, 1, 3, 1, 1};
            const std::size_t offsets[]{offsetof(Instance, position),
                                        offsetof(Instance, size),
                                        offsetof(Instance, rotation),
                                        offsetof(Instance, color),
                                        offsetof(Instance, alpha),
                                        offsetof(Instance, frame)};
            for (int i = 0; i < 6; ++i) {
                glEnableVertexAttribArray(i);
                glVertexAttribPointer(i,
                                      sizes[i],
                                      GL_FLOAT,
                                      GL_FALSE,
                                      sizeof(Instance),
                                      reinterpret_cast<void*>(offsets[i]));
                glVertexAttribDivisor(i, 1);
            }
            const char* names[]{"fire.png", "smoke.png", "sparkles.png"};
            for (int i = 0; i < 3; ++i) {
                int w = 0, h = 0, n = 0;
                const auto path =
                    (engineDataRoot.empty() ? std::filesystem::path(SDL_GetBasePath()) / "data"
                                            : engineDataRoot) /
                    "particles" / names[i];
                auto* pixels = stbi_load(path.string().c_str(), &w, &h, &n, 4);
                if (!pixels) {
                    throw std::runtime_error("Cannot load bundled particle sprite");
                }
                // stb returns top-first rows; custom textures use the same GL orientation.
                for (int y = 0; y < h / 2; ++y) {
                    std::swap_ranges(
                        pixels + y * w * 4, pixels + (y + 1) * w * 4, pixels + (h - y - 1) * w * 4);
                }
                glGenTextures(1, &sprites_[i]);
                glBindTexture(GL_TEXTURE_2D, sprites_[i]);
                glTexImage2D(
                    GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
                stbi_image_free(pixels);
                glGenerateMipmap(GL_TEXTURE_2D);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
        } catch (const std::exception& error) {
            if (vertex) {
                glDeleteShader(vertex);
            }
            if (fragment) {
                glDeleteShader(fragment);
            }
            error_ = error.what();
            shutdown();
            attempted_ = true;
        }
    }

  public:
    void shutdown() {
        textures_.clear();
        if (program_) {
            glDeleteProgram(program_);
        }
        if (vao_) {
            glDeleteVertexArrays(1, &vao_);
        }
        if (buffer_) {
            glDeleteBuffers(1, &buffer_);
        }
        glDeleteTextures(3, sprites_.data());
        sprites_ = {};
        program_ = vao_ = buffer_ = 0;
        attempted_ = false;
    }
    void draw(World& world, const AssetManager& assets, const Mat4& vp, Vec3 eye, bool editorView) {
        bool any = false;
        world.eachReadOnly<ParticleSystem>([&](Entity e, const ParticleSystem& s) {
            any |= !s.particles.empty() && (!s.preview || editorView) && isActive(world, e);
        });
        if (!any) {
            return;
        }
        if (root_ != assets.root) {
            textures_.clear();
            root_ = assets.root;
        }
        GLint active = 0, binding = 0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
        if (!attempted_) {
            init();
        }
        if (!program_) {
            glBindTexture(GL_TEXTURE_2D, binding);
            glActiveTexture(active);
            return;
        }
        static const Vec3 fallbackRight{1, 0, 0}, fallbackUp{0, 1, 0};
        // Rows 0/1 of a standard view-projection are scaled camera axes.
        auto right = normalize(Vec3{vp.m[0], vp.m[4], vp.m[8]});
        auto up = normalize(Vec3{vp.m[1], vp.m[5], vp.m[9]});
        if (dot(right, right) < .5f) {
            right = fallbackRight;
        }
        if (dot(up, up) < .5f) {
            up = fallbackUp;
        }
        const bool blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE);
        GLboolean depthWrite;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
        GLint srcRGB, dstRGB, srcA, dstA;
        glGetIntegerv(GL_BLEND_SRC_RGB, &srcRGB);
        glGetIntegerv(GL_BLEND_DST_RGB, &dstRGB);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcA);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &dstA);
        glEnable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
        glUseProgram(program_);
        glUniformMatrix4fv(vp_, 1, GL_FALSE, vp.m);
        glUniform3f(right_, right.x, right.y, right.z);
        glUniform3f(up_, up.x, up.y, up.z);
        glUniform1i(texture_, 0);
        glBindVertexArray(vao_);
        textures_.beginFrame();
        std::size_t remaining = 50000;
        world.eachReadOnly<Transform, ParticleSystem>([&](Entity e,
                                                          const Transform& t,
                                                          const ParticleSystem& s) {
            if (!isActive(world, e) || (s.preview && !editorView) || s.particles.empty() ||
                !remaining) {
                return;
            }
            instances_.clear();
            const auto count = std::min(remaining, s.particles.size());
            instances_.reserve(count);
            const float scale =
                std::max({std::abs(t.scale.x), std::abs(t.scale.y), std::abs(t.scale.z)});
            for (std::size_t i = 0; i < count; ++i) {
                const auto& p = s.particles[i];
                float f = std::clamp(p.age / p.lifetime, 0.f, 1.f);
                auto position = p.position;
                if (s.localSpace) {
                    position = t.position + rotateVector(t.rotation, position * t.scale);
                }
                Transform particlePose;
                particlePose.position = position;
                const float radius = std::abs((p.size + (s.endSize - p.size) * f) * scale);
                if (!RenderVisibility::visible(world,
                                               e,
                                               particlePose,
                                               {-radius, -radius, -radius},
                                               {radius, radius, radius})) {
                    continue;
                }
                instances_.push_back({position,
                                      (p.size + (s.endSize - p.size) * f) * scale,
                                      p.rotation,
                                      lerp(s.color, s.endColor, f),
                                      s.alpha + (s.endAlpha - s.alpha) * f,
                                      s.useSpriteSheet ? std::floor(p.age * s.frameRate) : 0.f});
            }
            if (s.blend == 0) {
                std::sort(instances_.begin(), instances_.end(), [&](const auto& a, const auto& b) {
                    return dot(a.position - eye, a.position - eye) >
                           dot(b.position - eye, b.position - eye);
                });
            }
            auto texture = s.texture.empty() ? sprites_[std::clamp(s.sprite, 0, 2)]
                                             : textures_.get(assets, s.texture, error_);
            if (!texture) {
                texture = sprites_[std::clamp(s.sprite, 0, 2)];
            }
            glBindTexture(GL_TEXTURE_2D, texture);
            glUniform2i(sheet_,
                        s.useSpriteSheet ? std::clamp(s.sheetColumns, 1, 64) : 1,
                        s.useSpriteSheet ? std::clamp(s.sheetRows, 1, 64) : 1);
            glBlendFuncSeparate(GL_SRC_ALPHA,
                                s.blend ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA,
                                GL_ONE,
                                GL_ONE_MINUS_SRC_ALPHA);
            glBindBuffer(GL_ARRAY_BUFFER, buffer_);
            glBufferData(GL_ARRAY_BUFFER,
                         GLsizeiptr(instances_.size() * sizeof(Instance)),
                         instances_.data(),
                         GL_STREAM_DRAW);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, GLsizei(instances_.size()));
            remaining -= instances_.size();
        });
        glBindVertexArray(0);
        glDepthMask(depthWrite);
        glBlendFuncSeparate(srcRGB, dstRGB, srcA, dstA);
        if (!blend) {
            glDisable(GL_BLEND);
        }
        if (cull) {
            glEnable(GL_CULL_FACE);
        }
        glBindTexture(GL_TEXTURE_2D, binding);
        glActiveTexture(active);
    }
};
