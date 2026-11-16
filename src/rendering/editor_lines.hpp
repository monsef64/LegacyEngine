#pragma once
#include "core/math.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <glad/gl.h>
// Screen-space expansion avoids driver-dependent glLineWidth limits on Windows.
class EditorLines {
    GLuint program_ = 0;
    bool attempted_ = false;
    GLint mvp_ = -1, color_ = -1, viewport_ = -1, width_ = -1;
    static GLuint compile(GLenum type, const char* text) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &text, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024]{};
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Editor lines: %s", log);
            glDeleteShader(s);
            return 0;
        }
        return s;
    }
    void init() {
        attempted_ = true;
        const GLuint v = compile(GL_VERTEX_SHADER, R"(#version 330 core
layout(location=0) in vec3 aPosition;uniform mat4 uMVP;
void main(){gl_Position=uMVP*vec4(aPosition,1);}
)");
        const GLuint g = compile(GL_GEOMETRY_SHADER, R"(#version 330 core
layout(lines) in;layout(triangle_strip,max_vertices=4) out;
uniform vec2 uViewport;uniform float uWidth;
void main(){
    vec4 a=gl_in[0].gl_Position,b=gl_in[1].gl_Position;
    float da=a.z+a.w,db=b.z+b.w;
    if(da<0. && db<0.)return;
    if(da<0.)a=mix(a,b,da/(da-db));
    else if(db<0.)b=mix(b,a,db/(db-da));
    if(a.w<=.000001||b.w<=.000001)return;
    vec2 delta=(b.xy/b.w-a.xy/a.w)*uViewport;
    float len=length(delta);if(len<.0001)return;
    vec2 offset=vec2(-delta.y,delta.x)/len*uWidth/uViewport;
    gl_Position=a+vec4(offset*a.w,0,0);EmitVertex();
    gl_Position=a-vec4(offset*a.w,0,0);EmitVertex();
    gl_Position=b+vec4(offset*b.w,0,0);EmitVertex();
    gl_Position=b-vec4(offset*b.w,0,0);EmitVertex();EndPrimitive();
}
)");
        const GLuint f = compile(GL_FRAGMENT_SHADER, R"(#version 330 core
uniform vec3 uColor;out vec4 fragColor;void main(){fragColor=vec4(uColor,1);}
)");
        if (v && g && f) {
            program_ = glCreateProgram();
            glAttachShader(program_, v);
            glAttachShader(program_, g);
            glAttachShader(program_, f);
            glLinkProgram(program_);
            GLint ok = 0;
            glGetProgramiv(program_, GL_LINK_STATUS, &ok);
            if (!ok) {
                glDeleteProgram(program_);
                program_ = 0;
            }
        }
        if (v) {
            glDeleteShader(v);
        }
        if (g) {
            glDeleteShader(g);
        }
        if (f) {
            glDeleteShader(f);
        }
        if (program_) {
            mvp_ = glGetUniformLocation(program_, "uMVP");
            color_ = glGetUniformLocation(program_, "uColor");
            viewport_ = glGetUniformLocation(program_, "uViewport");
            width_ = glGetUniformLocation(program_, "uWidth");
        }
    }

  public:
    void shutdown() {
        if (program_) {
            glDeleteProgram(program_);
        }
        program_ = 0;
        attempted_ = false;
    }
    bool bind(const Mat4& mvp, Vec3 color, float width) {
        if (!attempted_) {
            init();
        }
        if (!program_) {
            return false;
        }
        GLint viewport[4]{};
        glGetIntegerv(GL_VIEWPORT, viewport);
        glUseProgram(program_);
        glUniformMatrix4fv(mvp_, 1, GL_FALSE, mvp.m);
        glUniform3f(color_, color.x, color.y, color.z);
        glUniform2f(viewport_, float(std::max(1, viewport[2])), float(std::max(1, viewport[3])));
        glUniform1f(width_, width);
        return true;
    }
};
