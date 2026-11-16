#pragma once
#include "core/hierarchy.hpp"
#include <SDL3/SDL.h>
#include <cstddef>
#include <glad/gl.h>
#include <stdexcept>
#include <vector>

class LaserRenderer {
    GLuint program_ = 0, vao_ = 0, buffer_ = 0;
    bool attempted_ = false;
    struct Vertex {
        Vec3 position, color;
        float u, v, disc;
    };
    std::vector<Vertex> vertices_;
    static GLuint compile(GLenum type, const char* source) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048]{};
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            glDeleteShader(shader);
            throw std::runtime_error(log);
        }
        return shader;
    }
    void init() {
        attempted_ = true;
        GLuint vertex = 0, fragment = 0;
        try {
            vertex = compile(GL_VERTEX_SHADER, R"(#version 330 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aColor;
layout(location=2) in vec3 aUV;
uniform mat4 uVP;
out vec3 vColor;out vec3 vUV;
void main(){gl_Position=uVP*vec4(aPosition,1);vColor=aColor;vUV=aUV;}
)");
            fragment = compile(GL_FRAGMENT_SHADER, R"(#version 330 core
in vec3 vColor;in vec3 vUV;out vec4 fragColor;
void main(){
    float r=vUV.z>.5?length(vUV.xy):abs(vUV.y);
    if(r>=1.)discard;
    float glow=exp(-4.*r*r)*(1.-smoothstep(.7,1.,r));
    fragColor=vec4(vColor*glow,0);
}
)");
            program_ = glCreateProgram();
            glAttachShader(program_, vertex);
            glAttachShader(program_, fragment);
            glLinkProgram(program_);
            GLint ok = 0;
            glGetProgramiv(program_, GL_LINK_STATUS, &ok);
            if (!ok) {
                throw std::runtime_error("Cannot link laser shader");
            }
            glDeleteShader(vertex);
            vertex = 0;
            glDeleteShader(fragment);
            fragment = 0;
            glGenVertexArrays(1, &vao_);
            glGenBuffers(1, &buffer_);
            glBindVertexArray(vao_);
            glBindBuffer(GL_ARRAY_BUFFER, buffer_);
            const std::size_t offsets[]{
                offsetof(Vertex, position), offsetof(Vertex, color), offsetof(Vertex, u)};
            for (int i = 0; i < 3; ++i) {
                glEnableVertexAttribArray(i);
                glVertexAttribPointer(
                    i, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsets[i]));
            }
            glBindVertexArray(0);
        } catch (const std::exception& e) {
            if (vertex) {
                glDeleteShader(vertex);
            }
            if (fragment) {
                glDeleteShader(fragment);
            }
            if (program_) {
                glDeleteProgram(program_);
            }
            program_ = 0;
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Laser renderer: %s", e.what());
        }
    }
    void quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 color, bool disc) {
        const Vec3 points[]{a, b, c, a, c, d};
        const float uv[][2]{{-1, -1}, {1, -1}, {1, 1}, {-1, -1}, {1, 1}, {-1, 1}};
        for (int i = 0; i < 6; ++i) {
            vertices_.push_back({points[i], color, uv[i][0], uv[i][1], disc ? 1.f : 0.f});
        }
    }

  public:
    void shutdown() {
        if (buffer_) {
            glDeleteBuffers(1, &buffer_);
        }
        if (vao_) {
            glDeleteVertexArrays(1, &vao_);
        }
        if (program_) {
            glDeleteProgram(program_);
        }
        buffer_ = vao_ = program_ = 0;
        attempted_ = false;
        vertices_.clear();
    }
    void draw(World& world, const Mat4& vp, Vec3 eye) {
        vertices_.clear();
        const Vec3 cameraRight = normalize(Vec3{vp.m[0], vp.m[4], vp.m[8]});
        world.eachReadOnly<Transform, Laser>([&](Entity e, const Transform& t, const Laser& l) {
            if (!isActive(world, e) || !l.enabled || !l.ready || l.intensity <= 0 ||
                vertices_.size() >= 36 * 10000) {
                return;
            }
            const Vec3 axis = l.end - t.position;
            const float length2 = dot(axis, axis);
            if (length2 < 1e-10f) {
                return;
            }
            Vec3 side = normalize(cross(axis, eye - (t.position + l.end) * .5f));
            if (dot(side, side) < .1f) {
                side = cameraRight;
            }
            auto beam = [&](float width, Vec3 color) {
                const Vec3 half = side * (width * .5f);
                quad(
                    t.position - half, l.end - half, l.end + half, t.position + half, color, false);
            };
            beam(l.glowWidth, l.color * (l.intensity * .22f));
            beam(l.width, (l.color * .45f + Vec3{.55f, .55f, .55f}) * l.intensity);
            if (l.hit) {
                const Vec3 n = normalize(l.hitNormal), center = l.end + n * .003f;
                Vec3 right =
                    normalize(cross(n, std::abs(n.y) < .9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}));
                const Vec3 up = cross(n, right);
                auto disc = [&](float diameter, Vec3 color) {
                    const Vec3 x = right * (diameter * .5f), y = up * (diameter * .5f);
                    quad(center - x - y,
                         center + x - y,
                         center + x + y,
                         center - x + y,
                         color,
                         true);
                };
                disc(l.impactSize, l.color * (l.intensity * .6f));
                disc(l.impactSize * .22f, Vec3{1, 1, 1} * l.intensity);
            }
        });
        if (vertices_.empty()) {
            return;
        }
        GLint previousProgram, previousVao, previousBuffer, srcRGB, dstRGB, srcA, dstA;
        glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousBuffer);
        glGetIntegerv(GL_BLEND_SRC_RGB, &srcRGB);
        glGetIntegerv(GL_BLEND_DST_RGB, &dstRGB);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcA);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &dstA);
        const bool blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE),
                   depth = glIsEnabled(GL_DEPTH_TEST);
        GLboolean depthWrite;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
        if (!attempted_) {
            init();
        }
        if (program_) {
            glEnable(GL_DEPTH_TEST);
            glEnable(GL_BLEND);
            glDisable(GL_CULL_FACE);
            glDepthMask(GL_FALSE);
            glBlendFuncSeparate(GL_ONE, GL_ONE, GL_ZERO, GL_ONE);
            glUseProgram(program_);
            glUniformMatrix4fv(glGetUniformLocation(program_, "uVP"), 1, GL_FALSE, vp.m);
            glBindVertexArray(vao_);
            glBindBuffer(GL_ARRAY_BUFFER, buffer_);
            glBufferData(GL_ARRAY_BUFFER,
                         vertices_.size() * sizeof(Vertex),
                         vertices_.data(),
                         GL_STREAM_DRAW);
            glDrawArrays(GL_TRIANGLES, 0, GLsizei(vertices_.size()));
        }
        glDepthMask(depthWrite);
        if (!depth) {
            glDisable(GL_DEPTH_TEST);
        }
        if (cull) {
            glEnable(GL_CULL_FACE);
        } else {
            glDisable(GL_CULL_FACE);
        }
        if (blend) {
            glEnable(GL_BLEND);
        } else {
            glDisable(GL_BLEND);
        }
        glBlendFuncSeparate(srcRGB, dstRGB, srcA, dstA);
        glUseProgram(previousProgram);
        glBindVertexArray(previousVao);
        glBindBuffer(GL_ARRAY_BUFFER, previousBuffer);
    }
};
