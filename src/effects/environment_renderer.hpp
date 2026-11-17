#pragma once
#include "core/frustum.hpp"
#include "core/hierarchy.hpp"
#include "scene/scene_camera.hpp"
#include <SDL3/SDL.h>
#include <glad/gl.h>
#include <map>
#include <tuple>

// One inexpensive transparent pass; no reflection cameras, depth copies or extra targets.
class EnvironmentRenderer {
    struct Vertex {
        Vec3 p;
        float u, v;
    };
    struct Mesh {
        GLuint vao = 0, vbo = 0;
        GLsizei count = 0;
        std::string signature;
    };
    std::map<std::tuple<unsigned, unsigned, int>, Mesh> meshes_;
    GLuint program_ = 0;
    std::map<std::string, GLint> locations_;
    bool attempted_ = false;
    static GLuint compile(GLenum type, const char* source) {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &source, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048]{};
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            glDeleteShader(s);
            throw std::runtime_error(log);
        }
        return s;
    }
    void init() {
        attempted_ = true;
        GLuint v = 0, f = 0;
        try {
            v = compile(GL_VERTEX_SHADER, R"(#version 330 core
layout(location=0)in vec3 position;layout(location=1)in vec2 texcoord;
uniform mat4 vp,model;out vec3 localPoint;out vec3 worldPoint;out vec2 uv;
void main(){localPoint=position;worldPoint=(model*vec4(position,1)).xyz;uv=texcoord;gl_Position=vp*vec4(worldPoint,1);}
)");
            f = compile(GL_FRAGMENT_SHADER, R"(#version 330 core
in vec3 localPoint;in vec3 worldPoint;in vec2 uv;out vec4 fragColor;
uniform mat4 vp,model,inverseModel;uniform vec3 eye,tint;uniform int mode,steps;
uniform float handedness,time,opacity,speed,noiseScale,coverage,density,waveScale,waveStrength,foam;
float hash(vec3 p){p=fract(p*.1031);p+=dot(p,p.yzx+33.33);return fract((p.x+p.y)*p.z);}
float noise(vec3 p){vec3 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(mix(hash(i),hash(i+vec3(1,0,0)),f.x),mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),f.x),f.y),mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),f.x),mix(hash(i+vec3(0,1,1)),hash(i+vec3(1,1,1)),f.x),f.y),f.z);}
// Value noise with no sine/hash textures; used only for subtle water surface detail.
float waterNoise(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(hash(vec3(i,0)),hash(vec3(i+vec2(1,0),0)),f.x),mix(hash(vec3(i+vec2(0,1),0)),hash(vec3(i+vec2(1),0)),f.x),f.y);}
float cloud(vec3 p){return .65*noise(p)+.35*noise(p*2.07+11.);}
void main(){
 gl_FragDepth=gl_FragCoord.z;
 if(mode==0){
  // No periodic wave bands or vertex displacement. Two drifting noise scales
  // perturb the actual ribbon normal, with derivative filtering at distance.
  vec2 p=vec2(uv.x*2.,uv.y)*waveScale;
  vec2 drift=vec2(time*speed*.027,-time*speed*.11);
  float detail=waterNoise(p+drift)*.65+waterNoise(p*2.13-drift*.7+17.)*.35;
  vec3 dx=dFdx(worldPoint),dy=dFdy(worldPoint),viewDir=normalize(eye-worldPoint);
  vec3 n=normalize(cross(dx,dy));if(dot(n,viewDir)<0.)n=-n;
  vec3 rx=cross(dy,n),ry=cross(n,dx);float det=dot(dx,rx);
  float detailAttenuation=1./(1.+dot(fwidth(p),fwidth(p))*4.);
  vec3 gradient=(rx*dFdx(detail)+ry*dFdy(detail))*(sign(det)/max(abs(det),.000001));
  n=normalize(n-gradient*waveStrength*.12*detailAttenuation);
  float nv=clamp(dot(n,viewDir),0.,1.);float f=1.-nv;float fresnel=.02+.98*f*f*f*f*f;
  vec3 reflected=reflect(-viewDir,n);
  vec3 sky=mix(vec3(.48,.63,.72),vec3(.12,.27,.42),clamp(reflected.y,0.,1.));
  float bank=smoothstep(.55,1.,abs(uv.x*2.-1.));
  vec3 body=tint*mix(.55,.9,bank);
  vec3 light=normalize(vec3(.4,.85,.25)),halfway=normalize(viewDir+light);
  float nh=max(dot(n,halfway),0.),nl=max(dot(n,light),0.);
  float roughness=clamp(.18+length(fwidth(n))*.4,.18,.45),a2=roughness*roughness;
  float denom=nh*nh*(a2-1.)+1.;float specular=min(a2/(3.14159*denom*denom)*.018*nl,1.);
  float edgeFoam=foam*smoothstep(.82,.99,abs(uv.x*2.-1.))*smoothstep(.48,.78,detail);
  vec3 c=mix(body,sky,fresnel)+vec3(1.,.96,.85)*specular+vec3(.65,.72,.7)*edgeFoam;
  float alpha=clamp(opacity+fresnel*(1.-opacity),0.,1.)*(1.-smoothstep(.94,1.,abs(uv.x*2.-1.)));
  fragColor=vec4(c,alpha);return;
 }
 vec3 wind=vec3(time*speed,0,time*speed*.3);
 if(mode==1){float n=cloud(worldPoint*noiseScale+wind);float a=smoothstep(1.-coverage-.1,1.-coverage+.1,n)*density*opacity;
  float edge=((1.-smoothstep(.25,.5,abs(localPoint.x)))*(1.-smoothstep(.25,.5,abs(localPoint.z))));fragColor=vec4(tint*(.75+.25*n),clamp(a*edge,0.,1.));return;}
 // Render back faces only, including when the eye is inside the box.
 if(gl_FrontFacing==(handedness>0.))discard;
 vec3 ro=(inverseModel*vec4(eye,1)).xyz,rd=normalize(localPoint-ro);
 vec3 inv=1./mix(rd,vec3(.00001),lessThan(abs(rd),vec3(.00001)));
 vec3 a=(-.5-ro)*inv,b=(.5-ro)*inv;
 vec3 lo=min(a,b),hi=max(a,b);float nearT=max(max(lo.x,lo.y),lo.z),farT=min(min(hi.x,hi.y),hi.z);nearT=max(nearT,0.);
 if(farT<=nearT)discard;
 float dt=(farT-nearT)/float(steps),alpha=0.,light=0.;
 for(int i=0;i<24;++i){if(i>=steps||alpha>.97)break;vec3 q=ro+rd*(nearT+(float(i)+.5)*dt);vec3 wp=(model*vec4(q,1)).xyz;
  float n=cloud(wp*noiseScale+wind);float edge=(1.-smoothstep(.2,.5,abs(q.x)))*(1.-smoothstep(.2,.5,abs(q.z)))*(1.-smoothstep(.2,.5,abs(q.y)));
  float d=max(0.,n-(1.-coverage))*density*edge;
  float hit=1.-exp(-d*dt*12.);light+=(1.-alpha)*hit*(.65+.35*(q.y+.5));alpha+=(1.-alpha)*hit;
 }
 vec4 clip=vp*model*vec4(ro+rd*nearT,1);gl_FragDepth=clamp(clip.z/clip.w*.5+.5,0.,1.);
 fragColor=vec4(tint*(alpha>.0001?light/alpha:1.),alpha*opacity);
}
)");
            program_ = glCreateProgram();
            glAttachShader(program_, v);
            glAttachShader(program_, f);
            glLinkProgram(program_);
            GLint ok = 0;
            glGetProgramiv(program_, GL_LINK_STATUS, &ok);
            if (!ok) {
                throw std::runtime_error("Environment shader link failed");
            }
            glDeleteShader(v);
            glDeleteShader(f);
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
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Cloud/River renderer: %s", e.what());
        }
    }
    static void release(Mesh& m) {
        if (m.vbo) {
            glDeleteBuffers(1, &m.vbo);
        }
        if (m.vao) {
            glDeleteVertexArrays(1, &m.vao);
        }
        m = {};
    }
    static void quad(std::vector<Vertex>& v, Vec3 a, Vec3 b, Vec3 c, Vec3 d) {
        v.insert(v.end(), {{a, 0, 0}, {b, 0, 1}, {c, 1, 1}, {a, 0, 0}, {c, 1, 1}, {d, 1, 0}});
    }
    static std::vector<Vertex> geometry(const CloudSystem& c) {
        std::vector<Vertex> v;
        if (!c.volumetric) {
            quad(v, {-.5f, 0, -.5f}, {-.5f, 0, .5f}, {.5f, 0, .5f}, {.5f, 0, -.5f});
            return v;
        }
        const Vec3 a{-.5f, -.5f, -.5f}, b{.5f, -.5f, -.5f}, c1{.5f, .5f, -.5f}, d{-.5f, .5f, -.5f},
            e{-.5f, -.5f, .5f}, f{.5f, -.5f, .5f}, g{.5f, .5f, .5f}, h{-.5f, .5f, .5f};
        quad(v, b, a, d, c1);
        quad(v, e, f, g, h);
        quad(v, a, e, h, d);
        quad(v, f, b, c1, g);
        quad(v, d, h, g, c1);
        quad(v, a, b, f, e);
        return v;
    }
    static std::vector<Vertex> geometry(const River& r) {
        std::vector<Vertex> v;
        int n = (int(r.points.size()) - 1) * r.subdivisions;
        v.reserve(n * 6);
        Vec3 lastL{}, lastR{};
        float distance = 0;
        for (int i = 0; i <= n; ++i) {
            float t = float(i) / n;
            Vec3 p = Environment::curve(r, t);
            Vec3 tangent = Environment::curve(r, std::min(1.f, t + .001f)) -
                           Environment::curve(r, std::max(0.f, t - .001f));
            Vec3 side = normalize(Vec3{-tangent.z, 0, tangent.x});
            if (dot(side, side) < .1f) {
                side = {1, 0, 0};
            }
            Vec3 l = p - side * (r.width * .5f), rr = p + side * (r.width * .5f);
            if (i) {
                float next = distance + std::sqrt(dot(l - lastL, l - lastL));
                v.insert(v.end(),
                         {{lastL, 0, distance},
                          {l, 0, next},
                          {rr, 1, next},
                          {lastL, 0, distance},
                          {rr, 1, next},
                          {lastR, 1, distance}});
                distance = next;
            }
            lastL = l;
            lastR = rr;
        }
        return v;
    }

  public:
    void shutdown() {
        for (auto& kv : meshes_) {
            release(kv.second);
        }
        meshes_.clear();
        locations_.clear();
        if (program_) {
            glDeleteProgram(program_);
        }
        program_ = 0;
        attempted_ = false;
    }
    void draw(World& world, const Mat4& vp, Vec3 eye) {
        struct Item {
            Entity e;
            int kind;
            float distance;
        };
        std::vector<Item> items;
        const ViewFrustum frustum(vp);
        auto collect = [&](Entity e, const Transform& t, int kind, float radius) {
            if (items.size() >= 256 || !isActive(world, e) ||
                !frustum.visible(t.position, radius)) {
                return;
            }
            Vec3 d = t.position - eye;
            items.push_back({e, kind, dot(d, d)});
        };
        world.eachReadOnly<Transform, CloudSystem>(
            [&](Entity e, const Transform& t, const CloudSystem& c) {
                if (c.enabled && Environment::valid(c) && c.opacity > 0 && c.coverage > 0 &&
                    c.density > 0) {
                    Vec3 size = t.scale * c.size;
                    collect(e, t, 1, std::sqrt(dot(size, size)) * .5f);
                }
            });
        world.eachReadOnly<Transform, River>([&](Entity e, const Transform& t, const River& r) {
            if (r.enabled && Environment::valid(r) && r.opacity > 0) {
                float radius = 0;
                for (auto p : r.points) {
                    radius = std::max(radius, std::sqrt(dot(p, p)));
                }
                float scale =
                    std::max({std::abs(t.scale.x), std::abs(t.scale.y), std::abs(t.scale.z)});
                collect(e, t, 0, (radius * 1.5f + r.width) * scale);
            }
        });
        for (auto it = meshes_.begin(); it != meshes_.end();) {
            Entity e{std::get<0>(it->first), std::get<1>(it->first)};
            if (!world.alive(e) ||
                (std::get<2>(it->first) == 0 ? !world.has<River>(e) : !world.has<CloudSystem>(e))) {
                release(it->second);
                it = meshes_.erase(it);
            } else {
                ++it;
            }
        }
        if (items.empty()) {
            return;
        }
        if (!attempted_) {
            init();
        }
        if (!program_) {
            return;
        }
        std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
            return a.distance > b.distance;
        });
        GLint oldProgram, oldVao, oldBuffer, src, dst, srcA, dstA, frontFace;
        glGetIntegerv(GL_FRONT_FACE, &frontFace);
        glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldBuffer);
        glGetIntegerv(GL_BLEND_SRC_RGB, &src);
        glGetIntegerv(GL_BLEND_DST_RGB, &dst);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcA);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &dstA);
        bool blend = glIsEnabled(GL_BLEND), depth = glIsEnabled(GL_DEPTH_TEST),
             cull = glIsEnabled(GL_CULL_FACE);
        GLboolean write;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &write);
        glUseProgram(program_);
        glFrontFace(GL_CCW);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
        auto loc = [&](const char* n) {
            auto found = locations_.try_emplace(n, -1);
            if (found.second) {
                found.first->second = glGetUniformLocation(program_, n);
            }
            return found.first->second;
        };
        auto scalar = [&](const char* n, float v) { glUniform1f(loc(n), v); };
        glUniformMatrix4fv(loc("vp"), 1, GL_FALSE, vp.m);
        glUniform3f(loc("eye"), eye.x, eye.y, eye.z);
        scalar("time", float(std::fmod(double(SDL_GetTicksNS()) * 1e-9, 10000.)));
        for (const auto& item : items) {
            const auto& t = world.get<Transform>(item.e);
            Vec3 scale = t.scale, tint;
            std::string signature;
            int mode = 0;
            const CloudSystem* cloud = nullptr;
            const River* river = nullptr;
            if (item.kind) {
                cloud = &world.get<CloudSystem>(item.e);
                scale = scale * cloud->size;
                tint = cloud->color;
                mode = cloud->volumetric ? 2 : 1;
                signature = cloud->volumetric ? "cube" : "plane";
                scalar("coverage", cloud->coverage);
                scalar("density", cloud->density);
                scalar("noiseScale", cloud->scale);
                scalar("speed", cloud->speed);
                scalar("opacity", cloud->opacity);
                glUniform1i(loc("steps"), cloud->steps);
            } else {
                river = &world.get<River>(item.e);
                tint = river->color;
                auto keyValue = [&](auto value) {
                    signature.append(reinterpret_cast<const char*>(&value), sizeof(value));
                };
                keyValue(river->width);
                keyValue(river->subdivisions);
                for (auto p : river->points) {
                    keyValue(p.x);
                    keyValue(p.y);
                    keyValue(p.z);
                }
                scalar("speed", river->speed);
                scalar("opacity", river->opacity);
                scalar("waveScale", river->waveScale);
                scalar("waveStrength", river->waveStrength);
                scalar("foam", river->foam);
            }
            if (std::abs(scale.x * scale.y * scale.z) < 1e-8f || !Environment::finite(scale)) {
                continue;
            }
            auto key = std::make_tuple(item.e.index, item.e.generation, item.kind);
            if (!meshes_.count(key) && meshes_.size() >= 256) {
                continue;
            }
            auto& mesh = meshes_[key];
            if (mesh.signature != signature) {
                auto vertices = cloud ? geometry(*cloud) : geometry(*river);
                if (!mesh.vao) {
                    glGenVertexArrays(1, &mesh.vao);
                    glGenBuffers(1, &mesh.vbo);
                }
                glBindVertexArray(mesh.vao);
                glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
                glBufferData(GL_ARRAY_BUFFER,
                             vertices.size() * sizeof(Vertex),
                             vertices.data(),
                             GL_STATIC_DRAW);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(1,
                                      2,
                                      GL_FLOAT,
                                      GL_FALSE,
                                      sizeof(Vertex),
                                      reinterpret_cast<void*>(offsetof(Vertex, u)));
                mesh.count = GLsizei(vertices.size());
                mesh.signature = signature;
            }
            auto model = transformMatrix(t.position, t.rotation, scale),
                 inv = inverseCameraMatrix(model);
            glUniformMatrix4fv(loc("model"), 1, GL_FALSE, model.m);
            glUniformMatrix4fv(loc("inverseModel"), 1, GL_FALSE, inv.m);
            glUniform3f(loc("tint"), tint.x, tint.y, tint.z);
            scalar("handedness", scale.x * scale.y * scale.z > 0 ? 1.f : -1.f);
            glUniform1i(loc("mode"), mode);
            glBindVertexArray(mesh.vao);
            glDrawArrays(GL_TRIANGLES, 0, mesh.count);
        }
        glDepthMask(write);
        glFrontFace(frontFace);
        if (!blend) {
            glDisable(GL_BLEND);
        }
        if (!depth) {
            glDisable(GL_DEPTH_TEST);
        }
        if (cull) {
            glEnable(GL_CULL_FACE);
        }
        glBlendFuncSeparate(src, dst, srcA, dstA);
        glUseProgram(oldProgram);
        glBindVertexArray(oldVao);
        glBindBuffer(GL_ARRAY_BUFFER, oldBuffer);
    }
};
