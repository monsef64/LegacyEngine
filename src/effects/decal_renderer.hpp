#pragma once
#include "core/frustum.hpp"
#include "core/hierarchy.hpp"
#include "rendering/material_textures.hpp"
#include "scene/scene_camera.hpp"
#include <SDL3/SDL.h>
#include <glad/gl.h>

// OpenGL 3.3 box-projected decals: reconstruct opaque receiver positions from
// one depth copy per camera. No collision mesh, reflection or extra scene pass.
class DecalRenderer {
    unsigned program_ = 0, vao_ = 0, depth_ = 0;
    int width_ = 0, height_ = 0;
    MaterialTextureCache textures_;
    std::filesystem::path root_;
    bool failed_ = false;
    std::string lastTextureError_;
    static unsigned shader(unsigned type, const char* source) {
        auto s = glCreateShader(type);
        glShaderSource(s, 1, &source, nullptr);
        glCompileShader(s);
        int ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048]{};
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            glDeleteShader(s);
            throw std::runtime_error(std::string("Decal shader compilation failed: ") + log);
        }
        return s;
    }
    void init() {
        auto vs = shader(GL_VERTEX_SHADER, R"(#version 330 core
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0,1);})");
        unsigned fs = 0;
        try {
            fs = shader(GL_FRAGMENT_SHADER, R"(#version 330 core
uniform sampler2D uTexture;uniform sampler2D uDepth;
uniform mat4 uInverseVP;uniform mat4 uInverseModel;
uniform vec4 uViewport;uniform vec4 uColor;
out vec4 frag;
void main(){
 vec2 screen=(gl_FragCoord.xy-uViewport.xy)/uViewport.zw;
 float depth=texture(uDepth,screen).r;if(depth>=1.)discard;
 vec4 w=uInverseVP*vec4(screen*2.-1.,depth*2.-1.,1);w/=w.w;
 vec3 p=(uInverseModel*w).xyz;if(any(greaterThan(abs(p),vec3(.5))))discard;
 frag=texture(uTexture,p.xy+.5)*uColor;if(frag.a<.004)discard;
})");
            program_ = glCreateProgram();
            glAttachShader(program_, vs);
            glAttachShader(program_, fs);
            glLinkProgram(program_);
            int ok = 0;
            glGetProgramiv(program_, GL_LINK_STATUS, &ok);
            if (!ok) {
                throw std::runtime_error("Decal shader linking failed");
            }
            glGenVertexArrays(1, &vao_);
            glGenTextures(1, &depth_);
        } catch (...) {
            glDeleteShader(vs);
            if (fs) {
                glDeleteShader(fs);
            }
            shutdown();
            throw;
        }
        glDeleteShader(vs);
        glDeleteShader(fs);
    }

  public:
    void shutdown() {
        textures_.clear();
        if (vao_) {
            glDeleteVertexArrays(1, &vao_);
        }
        if (program_) {
            glDeleteProgram(program_);
        }
        if (depth_) {
            glDeleteTextures(1, &depth_);
        }
        vao_ = program_ = depth_ = 0;
        width_ = height_ = 0;
        failed_ = false;
    }
    void draw(World& world, const AssetManager& assets, const Mat4& vp) {
        struct Item {
            Entity entity;
            Mat4 inverse;
        };
        std::vector<Item> decals;
        const ViewFrustum frustum(vp);
        world.each<Decal>([&](Entity e, Decal& d) {
            const auto* t = world.tryGet<Transform>(e);
            if (!t || !d.enabled || d.texture.empty() || !isActive(world, e)) {
                return;
            }
            const auto model = transformMatrix(
                t->position,
                t->rotation,
                {t->scale.x * d.width, t->scale.y * d.height, t->scale.z * d.depth});
            if (!frustum.visibleBox({}, {.5f, .5f, .5f}, model)) {
                return;
            }
            try {
                decals.push_back({e, inverseCameraMatrix(model)});
            } catch (...) {
            } // zero scale is invisible
        });
        if (decals.empty() || failed_) {
            return;
        }
        Mat4 inverseVP;
        try {
            inverseVP = inverseCameraMatrix(vp);
        } catch (...) {
            return;
        }
        if (!program_) {
            try {
                init();
            } catch (const std::exception& e) {
                SDL_LogError(SDL_LOG_CATEGORY_RENDER, "%s", e.what());
                failed_ = true;
                return;
            }
        }
        std::stable_sort(decals.begin(), decals.end(), [&](const Item& a, const Item& b) {
            return world.get<Decal>(a.entity).sortingOrder <
                   world.get<Decal>(b.entity).sortingOrder;
        });
        GLint oldProgram, oldVao, active, binding0, binding1, src, dst, srcA, dstA, read, draw,
            viewport[4];
        glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &read);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
        glGetIntegerv(GL_VIEWPORT, viewport);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding0);
        glActiveTexture(GL_TEXTURE1);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &binding1);
        glGetIntegerv(GL_BLEND_SRC_RGB, &src);
        glGetIntegerv(GL_BLEND_DST_RGB, &dst);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcA);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &dstA);
        bool blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE),
             depth = glIsEnabled(GL_DEPTH_TEST), scissor = glIsEnabled(GL_SCISSOR_TEST);
        GLboolean write;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &write);
        auto restore = [&] {
            glDepthMask(write);
            if (depth) {
                glEnable(GL_DEPTH_TEST);
            } else {
                glDisable(GL_DEPTH_TEST);
            }
            if (blend) {
                glEnable(GL_BLEND);
            } else {
                glDisable(GL_BLEND);
            }
            if (cull) {
                glEnable(GL_CULL_FACE);
            } else {
                glDisable(GL_CULL_FACE);
            }
            if (scissor) {
                glEnable(GL_SCISSOR_TEST);
            } else {
                glDisable(GL_SCISSOR_TEST);
            }
            glBlendFuncSeparate(src, dst, srcA, dstA);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, binding1);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, binding0);
            glActiveTexture(active);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, read);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
            glBindVertexArray(oldVao);
            glUseProgram(oldProgram);
        };
        try {
            glDisable(GL_SCISSOR_TEST);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, depth_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
            if (width_ != viewport[2] || height_ != viewport[3]) {
                width_ = viewport[2];
                height_ = viewport[3];
                glTexImage2D(GL_TEXTURE_2D,
                             0,
                             GL_DEPTH_COMPONENT24,
                             width_,
                             height_,
                             0,
                             GL_DEPTH_COMPONENT,
                             GL_UNSIGNED_INT,
                             nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
            // Copy depth into a sampleable texture. Unlike a depth blit this also
            // supports editor targets whose attachment format differs from ours.
            glBindFramebuffer(GL_READ_FRAMEBUFFER, draw);
            glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, viewport[0], viewport[1], width_, height_);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, draw);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glEnable(GL_BLEND);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
            glDepthMask(GL_FALSE);
            glUseProgram(program_);
            glBindVertexArray(vao_);
            glUniformMatrix4fv(
                glGetUniformLocation(program_, "uInverseVP"), 1, GL_FALSE, inverseVP.m);
            glUniform4f(glGetUniformLocation(program_, "uViewport"),
                        float(viewport[0]),
                        float(viewport[1]),
                        float(width_),
                        float(height_));
            glUniform1i(glGetUniformLocation(program_, "uDepth"), 1);
            glUniform1i(glGetUniformLocation(program_, "uTexture"), 0);
            if (root_ != assets.root) {
                textures_.clear();
                root_ = assets.root;
            }
            textures_.beginFrame();
            glActiveTexture(GL_TEXTURE0);
            for (const auto& item : decals) {
                const auto& d = world.get<Decal>(item.entity);
                std::string error;
                auto texture = textures_.get(assets, d.texture, error);
                if (!texture) {
                    if (!error.empty() && error != lastTextureError_) {
                        SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                                     "Decal texture %s: %s",
                                     d.texture.c_str(),
                                     error.c_str());
                        lastTextureError_ = error;
                    }
                    continue;
                }
                glUniformMatrix4fv(
                    glGetUniformLocation(program_, "uInverseModel"), 1, GL_FALSE, item.inverse.m);
                glUniform4f(glGetUniformLocation(program_, "uColor"),
                            d.color.x,
                            d.color.y,
                            d.color.z,
                            d.opacity);
                glBindTexture(GL_TEXTURE_2D, texture);
                glDrawArrays(GL_TRIANGLES, 0, 3);
            }
        } catch (const std::exception& e) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Decals: %s", e.what());
            failed_ = true;
        }
        restore();
    }
};
