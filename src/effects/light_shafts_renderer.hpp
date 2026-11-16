#pragma once
#include "core/frustum.hpp"
#include "core/hierarchy.hpp"
#include "rendering/material_textures.hpp"
#include <SDL3/SDL.h>
#include <glad/gl.h>
#include <stdexcept>
class LightShaftsRenderer {
    GLuint program_ = 0, vao_ = 0, buffer_ = 0;
    bool attempted_ = false;
    GLint vp_ = -1, color_ = -1, shape_ = -1, profile_ = -1, baked_ = -1;
    MaterialTextureCache textures_;
    std::filesystem::path root_;
    std::string error_;
    struct Vertex {
        Vec3 position;
        float u, v;
    };
    static GLuint shader(GLenum type, const char* source) {
        const GLuint id = glCreateShader(type);
        glShaderSource(id, 1, &source, nullptr);
        glCompileShader(id);
        GLint ok = 0;
        glGetShaderiv(id, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048]{};
            glGetShaderInfoLog(id, sizeof(log), nullptr, log);
            glDeleteShader(id);
            throw std::runtime_error(log);
        }
        return id;
    }
    void init() {
        attempted_ = true;
        GLuint v = 0, f = 0;
        try {
            v = shader(GL_VERTEX_SHADER, R"(#version 330 core
layout(location=0)in vec3 aPosition;layout(location=1)in vec2 aUV;
uniform mat4 uVP;out vec2 uv;
void main(){gl_Position=uVP*vec4(aPosition,1);uv=aUV;}
)");
            f = shader(GL_FRAGMENT_SHADER, R"(#version 330 core
in vec2 uv;out vec4 fragColor;
uniform vec3 uColor;uniform vec3 uShape;uniform sampler2D uProfile;uniform bool uBaked;
void main(){
    float profile;
    if(uBaked)profile=texture(uProfile,uv).r;
    else {
        float radial=pow(max(0.,1.-abs(2.*uv.x-1.)),uShape.x);
        float wave=sin(uv.x*uShape.y*6.2831853);
        profile=radial*sqrt(max(0.,1.-uv.y))*smoothstep(0.,uShape.z,uv.y)*(.65+.35*wave*wave);
    }
    fragColor=vec4(uColor*profile,0);
}
)");
            program_ = glCreateProgram();
            glAttachShader(program_, v);
            glAttachShader(program_, f);
            glLinkProgram(program_);
            GLint ok = 0;
            glGetProgramiv(program_, GL_LINK_STATUS, &ok);
            if (!ok) {
                throw std::runtime_error("Cannot link LightShafts shader");
            }
            glDeleteShader(v);
            v = 0;
            glDeleteShader(f);
            f = 0;
            vp_ = glGetUniformLocation(program_, "uVP");
            color_ = glGetUniformLocation(program_, "uColor");
            shape_ = glGetUniformLocation(program_, "uShape");
            profile_ = glGetUniformLocation(program_, "uProfile");
            baked_ = glGetUniformLocation(program_, "uBaked");
            glGenVertexArrays(1, &vao_);
            glGenBuffers(1, &buffer_);
            glBindVertexArray(vao_);
            glBindBuffer(GL_ARRAY_BUFFER, buffer_);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(
                1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(sizeof(Vec3)));
        } catch (const std::exception& e) {
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
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "LightShafts: %s", e.what());
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
        program_ = vao_ = buffer_ = 0;
        attempted_ = false;
    }
    void draw(World& world, const AssetManager& assets, const Mat4& vp, Vec3 eye) {
        bool any = false;
        world.eachReadOnly<LightShafts>(
            [&](Entity e, const LightShafts& s) { any |= s.enabled && isActive(world, e); });
        if (!any) {
            return;
        }
        GLint oldProgram, oldVao, oldBuffer, active, binding, src, dst, srcA, dstA;
        glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldBuffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding);
        glGetIntegerv(GL_BLEND_SRC_RGB, &src);
        glGetIntegerv(GL_BLEND_DST_RGB, &dst);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcA);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &dstA);
        const bool blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE),
                   depth = glIsEnabled(GL_DEPTH_TEST);
        GLboolean write;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &write);
        if (root_ != assets.root) {
            textures_.clear();
            root_ = assets.root;
        }
        textures_.beginFrame();
        if (!attempted_) {
            init();
        }
        if (program_) {
            glUseProgram(program_);
            glBindVertexArray(vao_);
            glBindBuffer(GL_ARRAY_BUFFER, buffer_);
            glEnable(GL_BLEND);
            glDisable(GL_CULL_FACE);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
            glUniformMatrix4fv(vp_, 1, GL_FALSE, vp.m);
            glUniform1i(profile_, 0);
            const ViewFrustum frustum(vp);
            unsigned count = 0;
            world.eachReadOnly<Transform, LightShafts>(
                [&](Entity e, const Transform& t, const LightShafts& s) {
                    if (count >= 128 || !s.enabled || !isActive(world, e) || s.intensity <= 0 ||
                        s.density <= 0) {
                        return;
                    }
                    const Vec3 axis = rotateVector(normalize(t.rotation), normalize(s.direction)) *
                                      s.height,
                               end = t.position + axis, center = (t.position + end) * .5f;
                    if (!frustum.visible(center, s.height * .5f + std::max(s.width, s.endWidth))) {
                        return;
                    }
                    Vec3 side = normalize(cross(axis, eye - center));
                    if (dot(side, side) < .1f) {
                        side = normalize(Vec3{vp.m[0], vp.m[4], vp.m[8]});
                    }
                    const Vec3 a = t.position - side * (s.width * .5f),
                               b = t.position + side * (s.width * .5f),
                               c = end + side * (s.endWidth * .5f),
                               d = end - side * (s.endWidth * .5f);
                    const Vertex vertices[]{
                        {a, 0, 0}, {b, 1, 0}, {c, 1, 1}, {a, 0, 0}, {c, 1, 1}, {d, 0, 1}};
                    GLuint texture = s.useBakedTexture && !s.texture.empty()
                                         ? textures_.get(assets, s.texture, error_)
                                         : 0;
                    glBindTexture(GL_TEXTURE_2D, texture);
                    glUniform1i(baked_, texture != 0);
                    glUniform3f(shape_, s.falloff, s.streaks, s.fade);
                    const Vec3 delta = eye - t.position;
                    const float fade =
                        std::clamp(std::sqrt(dot(delta, delta)) / s.nearFade, 0.f, 1.f);
                    const Vec3 color = s.color * (s.intensity * s.density * fade);
                    glUniform3f(color_, color.x, color.y, color.z);
                    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);
                    glDrawArrays(GL_TRIANGLES, 0, 6);
                    ++count;
                });
        }
        glUseProgram(oldProgram);
        glBindVertexArray(oldVao);
        glBindBuffer(GL_ARRAY_BUFFER, oldBuffer);
        glBindTexture(GL_TEXTURE_2D, binding);
        glActiveTexture(active);
        glDepthMask(write);
        glBlendFuncSeparate(src, dst, srcA, dstA);
        if (!blend) {
            glDisable(GL_BLEND);
        }
        if (cull) {
            glEnable(GL_CULL_FACE);
        }
        if (!depth) {
            glDisable(GL_DEPTH_TEST);
        }
    }
};
