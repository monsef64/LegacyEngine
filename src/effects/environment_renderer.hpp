#pragma once
#include "core/frustum.hpp"
#include "core/hierarchy.hpp"
#include "effects/water_mesh.hpp"
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
        Vec3 normal{0, 1, 0};
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
    std::string error;
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
layout(location=0)in vec3 position;layout(location=1)in vec2 texcoord;layout(location=2)in vec3 normal;
uniform mat4 vp,model;uniform int mode;uniform float time,speed,waveHeight,waveLength,waveSpeed;
out vec3 localPoint;out vec3 worldPoint;out vec3 surfaceNormal;out vec2 uv;
void main(){surfaceNormal=transpose(inverse(mat3(model)))*normal;localPoint=position;worldPoint=(model*vec4(position,1)).xyz;
if(mode==0){float k=6.2831853/waveLength,t=time*speed*waveSpeed;
worldPoint.y+=waveHeight*(.6*sin(k*dot(worldPoint.xz,vec2(.8,.6))-t*1.4)+.28*sin(k*1.73*dot(worldPoint.xz,vec2(-.6,.8))-t*1.9)+.12*sin(k*2.61*dot(worldPoint.xz,vec2(.3,.95))-t*2.6));}
uv=texcoord;gl_Position=vp*vec4(worldPoint,1);}
)");
            f = compile(GL_FRAGMENT_SHADER, R"(#version 330 core
in vec3 localPoint;in vec3 worldPoint;in vec3 surfaceNormal;in vec2 uv;out vec4 fragColor;
uniform mat4 vp,model,inverseModel;uniform vec3 eye,tint;uniform int mode,steps;
uniform int transparentWater,contactCount,waterStyle,meshWater;
uniform float waveHeight,waveLength,waveSpeed;
uniform vec4 contacts[16];
uniform float contactStrength;
uniform float handedness,time,opacity,speed,noiseScale,coverage,density,waveScale,waveStrength,foam;
float hash(vec3 p){p=fract(p*.1031);p+=dot(p,p.yzx+33.33);return fract((p.x+p.y)*p.z);}
float noise(vec3 p){vec3 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(mix(hash(i),hash(i+vec3(1,0,0)),f.x),mix(hash(i+vec3(0,1,0)),hash(i+vec3(1,1,0)),f.x),f.y),mix(mix(hash(i+vec3(0,0,1)),hash(i+vec3(1,0,1)),f.x),mix(hash(i+vec3(0,1,1)),hash(i+vec3(1,1,1)),f.x),f.y),f.z);}
// Value noise with no sine/hash textures; used only for subtle water surface detail.
float waterNoise(vec2 p){vec2 i=floor(p),f=fract(p);f=f*f*(3.-2.*f);return mix(mix(hash(vec3(i,0)),hash(vec3(i+vec2(1,0),0)),f.x),mix(hash(vec3(i+vec2(0,1),0)),hash(vec3(i+vec2(1),0)),f.x),f.y);}
float cloud(vec3 p){return .65*noise(p)+.35*noise(p*2.07+11.);}
void main(){
 gl_FragDepth=gl_FragCoord.z;
 if(mode==0){
  // Interpolated base normals plus analytic wave slopes avoid triangle-flat
  // specular highlights on the displaced surface.
  vec2 p=(meshWater!=0?worldPoint.xz:vec2(uv.x*2.,uv.y))*waveScale;
  float styleDetail=waterStyle==4?2.4:waterStyle==3?.4:waterStyle==7?.65:1.;
  p*=styleDetail;
  vec2 drift=vec2(time*speed*.027,-time*speed*.11);
  float detail=waterNoise(p+drift)*.65+waterNoise(p*2.13-drift*.7+17.)*.35;
  float contactFoam=0.;
  // Bounded world-space surface contacts: expanding rings and a foamy collar.
  for(int i=0;i<16;++i){if(i>=contactCount)break;
   float radius=contacts[i].w;
   float d=length(worldPoint-contacts[i].xyz);
   float band=max(0.,d-radius);
   float envelope=exp(-band*2.)*smoothstep(radius*.6,radius,d);
   detail+=sin(band*14.-time*5.)*envelope*contactStrength*.18;
   contactFoam+=envelope*(.55+.45*sin(band*14.-time*5.))*contactStrength;
  }
  vec3 dx=dFdx(worldPoint),dy=dFdy(worldPoint),viewDir=normalize(eye-worldPoint);
  vec3 n=normalize(surfaceNormal);
  float k=6.2831853/waveLength,t=time*speed*waveSpeed;
  vec2 slope=waveHeight*k*(.6*vec2(.8,.6)*cos(k*dot(worldPoint.xz,vec2(.8,.6))-t*1.4)
   +.28*1.73*vec2(-.6,.8)*cos(k*1.73*dot(worldPoint.xz,vec2(-.6,.8))-t*1.9)
   +.12*2.61*vec2(.3,.95)*cos(k*2.61*dot(worldPoint.xz,vec2(.3,.95))-t*2.6));
  n=normalize(vec3(n.x-slope.x*n.y,n.y,n.z-slope.y*n.y));
  if(dot(n,viewDir)<0.)n=-n;
  vec3 rx=cross(dy,n),ry=cross(n,dx);float det=dot(dx,rx);
  float detailAttenuation=1./(1.+dot(fwidth(p),fwidth(p))*4.);
  vec3 gradient=(rx*dFdx(detail)+ry*dFdy(detail))*(sign(det)/max(abs(det),.000001));
  n=normalize(n-gradient*waveStrength*.12*detailAttenuation);
  float nv=clamp(dot(n,viewDir),0.,1.);float f=1.-nv;float fresnel=.02+.98*f*f*f*f*f;
  vec3 reflected=reflect(-viewDir,n);
  vec3 sky=mix(vec3(.48,.63,.72),vec3(.12,.27,.42),clamp(reflected.y,0.,1.));
  float bank=smoothstep(.55,1.,abs(uv.x*2.-1.));
  vec3 waterTint=tint;
  if(waterStyle==2)waterTint=mix(tint,vec3(.015,.10,.19),.8);
  if(waterStyle==3)waterTint=mix(tint,vec3(.07,.18,.13),.65);
  if(waterStyle==4)waterTint=mix(tint,vec3(.13,.31,.34),.5);
  if(waterStyle==5)waterTint=mix(tint,vec3(.02,.55,.48),.8);
  if(waterStyle==6)waterTint=mix(tint,vec3(.12,.15,.045),.85);
  if(waterStyle==7)waterTint=mix(tint,vec3(.15,.14,.085),.9);
  if(waterStyle==7)sky=mix(vec3(.37,.35,.23),vec3(.65,.64,.48),clamp(reflected.y,0.,1.));
  vec3 body=waterTint*mix(.55,.9,bank);
  vec3 light=normalize(vec3(.4,.85,.25)),halfway=normalize(viewDir+light);
  float nh=max(dot(n,halfway),0.),nl=max(dot(n,light),0.);
  float baseRoughness=waterStyle==7?.1:waterStyle==3?.12:waterStyle==4?.32:.18;
  float roughness=clamp(baseRoughness+length(fwidth(n))*.4,baseRoughness,.5),a2=roughness*roughness;
  float denom=nh*nh*(a2-1.)+1.;float specular=min(a2/(3.14159*denom*denom)*.018*nl,1.);
  float edgeMask=meshWater!=0?0.:smoothstep(.82,.99,abs(uv.x*2.-1.));
  float edgeFoam=foam*edgeMask*smoothstep(.48,.78,detail);
  if(waterStyle==4)edgeFoam+=foam*smoothstep(.58,.85,detail);
  if(waterStyle==6)edgeFoam+=.12*smoothstep(.65,.8,detail);
  float shoreFade=meshWater!=0?1.:(1.-smoothstep(.94,1.,abs(uv.x*2.-1.)));
  vec3 c=mix(body,sky,fresnel)+vec3(1.,.96,.85)*specular+vec3(.65,.72,.7)*edgeFoam;
  float alpha=clamp(opacity+fresnel*(1.-opacity),0.,1.)*shoreFade;
  if(transparentWater!=0||waterStyle==1||waterStyle==5){
   // Physically motivated grazing reflection, low face-on opacity and tint.
   // Standard alpha transmission shows underlying scene geometry; no screen copy.
   c=mix(waterTint*.7,sky,fresnel)+vec3(1.,.96,.85)*specular;
   alpha=clamp(opacity*.42+fresnel*(1.-opacity*.42),0.,1.)*
     shoreFade;
  }
  c=mix(c,vec3(.78,.88,.9),clamp(contactFoam,0.,.8));
  alpha=max(alpha,clamp(contactFoam,0.,.75)*shoreFade);
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
        Vec3 lastL{}, lastR{}, lastNormal{0, 1, 0};
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
            Vec3 normal = normalize(cross(side, tangent));
            if (dot(normal, normal) < .1f) {
                normal = {0, 1, 0};
            }
            if (normal.y < 0) {
                normal = normal * -1.f;
            }
            Vec3 l = p - side * (r.width * .5f), rr = p + side * (r.width * .5f);
            if (i) {
                float next = distance + std::sqrt(dot(l - lastL, l - lastL));
                v.insert(v.end(),
                         {{lastL, 0, distance, lastNormal},
                          {l, 0, next, normal},
                          {rr, 1, next, normal},
                          {lastL, 0, distance, lastNormal},
                          {rr, 1, next, normal},
                          {lastR, 1, distance, lastNormal}});
                distance = next;
            }
            lastL = l;
            lastR = rr;
            lastNormal = normal;
        }
        for (int pass = 0; pass < 3 && v.size() * 4 < 120000; ++pass) {
            std::vector<Vertex> refined;
            for (size_t i = 0; i + 2 < v.size(); i += 3) {
                auto a = v[i], b = v[i + 1], c = v[i + 2];
                auto mid = [](Vertex x, Vertex y) {
                    return Vertex{(x.p + y.p) * .5f,
                                  (x.u + y.u) * .5f,
                                  (x.v + y.v) * .5f,
                                  normalize(x.normal + y.normal)};
                };
                auto ab = mid(a, b), bc = mid(b, c), ca = mid(c, a);
                refined.insert(refined.end(), {a, ab, ca, ab, b, bc, ca, bc, c, ab, bc, ca});
            }
            v = std::move(refined);
        }
        return v;
    }

    struct Contact {
        float x, y, z, radius;
    };
    static std::vector<Contact>
    meshContacts(World& world, Entity e, const Transform& t, const AssetManager& assets) {
        std::vector<Contact> contacts;
        try {
            const auto& mesh = Waters::geometry(world, e, assets, false);
            unsigned considered = 0;
            float bound = 0;
            for (const auto& vertex : mesh.vertices) {
                auto p = vertex.position * t.scale;
                bound = std::max(bound, std::sqrt(dot(p, p)));
            }
            auto point = [&](Vec3 p) { return t.position + rotateVector(t.rotation, p * t.scale); };
            world.eachReadOnly<Transform, Collider>(
                [&](Entity other, const Transform& p, const Collider& c) {
                    if (contacts.size() >= 16 || considered >= 128 || e == other || c.isTrigger ||
                        !isActive(world, other) || c.shape == ColliderShape::Mesh) {
                        return;
                    }
                    ++considered;
                    auto center = p.position + rotateVector(p.rotation, c.center * p.scale);
                    auto size = c.size * p.scale;
                    float radius = c.shape == ColliderShape::Box
                                       ? .5f * std::sqrt(dot(size, size))
                                       : c.radius * std::max({std::abs(p.scale.x),
                                                              std::abs(p.scale.y),
                                                              std::abs(p.scale.z)});
                    auto delta = center - t.position;
                    if (dot(delta, delta) > (bound + radius) * (bound + radius)) {
                        return;
                    }
                    for (size_t i = 0; i + 2 < mesh.vertices.size(); i += 3) {
                        Vec3 hit, normal;
                        if (Waters::surface(point(mesh.vertices[i].position),
                                            point(mesh.vertices[i + 1].position),
                                            point(mesh.vertices[i + 2].position),
                                            center,
                                            hit,
                                            normal)) {
                            hit.y += Environment::waterWave(
                                hit, world.get<Water>(e), Environment::waterTime);
                            if (std::abs(dot(center - hit, normal)) > radius) {
                                continue;
                            }
                            contacts.push_back({hit.x, hit.y, hit.z, std::max(.05f, radius * .6f)});
                            break;
                        }
                    }
                });
        } catch (const std::exception&) {
        }
        return contacts;
    }
    // Visual surface intersections, not rigid-body forces. Bounds are deliberately
    // conservative for round colliders; triggers and mesh colliders are excluded.
    static std::vector<Contact>
    riverContacts(World& world, Entity riverEntity, const Transform& pose, const River& river) {
        std::vector<Contact> result;
        struct Segment {
            Vec3 a, b, side;
        };
        std::vector<Segment> segments;
        auto worldPoint = [&](Vec3 p) {
            return pose.position + rotateVector(pose.rotation, p * pose.scale);
        };
        const int samples = (int(river.points.size()) - 1) * river.subdivisions;
        for (int i = 0; i < samples; ++i) {
            const auto a = Environment::curve(river, float(i) / samples);
            const auto b = Environment::curve(river, float(i + 1) / samples);
            auto side = normalize(Vec3{-(b - a).z, 0, (b - a).x});
            if (dot(side, side) < .1f) {
                side = {1, 0, 0};
            }
            segments.push_back(
                {worldPoint(a),
                 worldPoint(b),
                 rotateVector(pose.rotation, side * pose.scale) * (river.width * .5f)});
        }
        unsigned considered = 0;
        world.eachReadOnly<Transform, Collider>(
            [&](Entity e, const Transform& t, const Collider& c) {
                if (e == riverEntity || result.size() >= 16 || considered >= 512 ||
                    !isActive(world, e) || c.isTrigger || c.shape == ColliderShape::Mesh) {
                    return;
                }
                ++considered;
                Vec3 half;
                if (c.shape == ColliderShape::Box) {
                    auto size = c.size * t.scale;
                    half = {std::abs(size.x) * .5f, std::abs(size.y) * .5f, std::abs(size.z) * .5f};
                } else {
                    const float radius =
                        std::abs(c.radius) *
                        std::max({std::abs(t.scale.x), std::abs(t.scale.y), std::abs(t.scale.z)});
                    half = {radius,
                            radius + (c.shape == ColliderShape::Capsule
                                          ? std::abs(c.height * t.scale.y) * .5f
                                          : 0),
                            radius};
                    if (c.shape == ColliderShape::Cone) {
                        half.y = std::abs(c.height * t.scale.y) * .5f;
                    }
                }
                const auto center = t.position + rotateVector(t.rotation, c.center * t.scale);
                if (!Environment::finite(center) || !Environment::finite(half)) {
                    return;
                }
                const auto ax = rotateVector(t.rotation, {1, 0, 0}),
                           ay = rotateVector(t.rotation, {0, 1, 0}),
                           az = rotateVector(t.rotation, {0, 0, 1});
                auto extent = [&](Vec3 direction) {
                    return std::abs(dot(direction, ax)) * half.x +
                           std::abs(dot(direction, ay)) * half.y +
                           std::abs(dot(direction, az)) * half.z;
                };
                float best = 1e30f;
                Vec3 contact{};
                float contactRadius = 0;
                for (const auto& segment : segments) {
                    const auto delta = segment.b - segment.a;
                    const float len2 = dot(delta, delta);
                    if (len2 < 1e-10f) {
                        continue;
                    }
                    const auto along = normalize(delta), across = normalize(segment.side);
                    const auto normal = normalize(cross(along, across));
                    if (dot(normal, normal) < .1f) {
                        continue;
                    }
                    const float u = std::clamp(dot(center - segment.a, delta) / len2, 0.f, 1.f);
                    const auto nearest = segment.a + delta * u;
                    const auto offset = center - nearest;
                    const float width = std::sqrt(dot(segment.side, segment.side));
                    if (std::abs(dot(offset, normal)) > extent(normal) + .03f ||
                        std::abs(dot(offset, across)) > width + extent(across) ||
                        std::abs(dot(offset, along)) > extent(along) + .03f) {
                        continue;
                    }
                    const float score = dot(offset, offset);
                    if (score < best) {
                        best = score;
                        contact = center - normal * dot(offset, normal);
                        contactRadius =
                            std::clamp(std::min(extent(along), extent(across)), .05f, 20.f);
                    }
                }
                if (best < 1e29f) {
                    result.push_back({contact.x, contact.y, contact.z, contactRadius});
                }
            });
        return result;
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
    void draw(World& world, const AssetManager& assets, const Mat4& vp, Vec3 eye) {
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
                collect(e, t, 0, (radius * 1.5f + r.width) * scale + r.waveHeight);
            }
        });
        world.eachReadOnly<Transform, Water>([&](Entity e, const Transform& t, const Water& w) {
            if (!w.enabled || !Environment::valid(w) || w.opacity <= 0) {
                return;
            }
            try {
                float radius = 0;
                for (const auto& v : Waters::geometry(world, e, assets).vertices) {
                    radius = std::max(radius,
                                      std::sqrt(dot(v.position * t.scale, v.position * t.scale)));
                }
                collect(e, t, 2, radius + w.waveHeight);
            } catch (const std::exception& ex) {
                error = ex.what();
            }
        });
        for (auto it = meshes_.begin(); it != meshes_.end();) {
            Entity e{std::get<0>(it->first), std::get<1>(it->first)};
            if (!world.alive(e) || (std::get<2>(it->first) == 0   ? !world.has<River>(e)
                                    : std::get<2>(it->first) == 2 ? !world.has<Water>(e)
                                                                  : !world.has<CloudSystem>(e))) {
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
            if (item.kind == 1) {
                scalar("time", float(std::fmod(double(SDL_GetTicksNS()) * 1e-9, 10000.)));
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
                scalar("time", float(Environment::waterTime));
                river = item.kind == 2 ? static_cast<const River*>(&world.get<Water>(item.e))
                                       : &world.get<River>(item.e);
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
                if (item.kind == 2) {
                    const auto& w = world.get<Water>(item.e);
                    signature += w.mesh;
                    const auto* r = world.tryGet<MeshRenderer>(item.e);
                    if (r) {
                        signature += r->model;
                        keyValue(r->mesh.id);
                    }
                    keyValue(modelAssetRevision);
                }
                scalar("waveHeight", river->waveHeight);
                scalar("waveLength", river->waveLength);
                scalar("waveSpeed", river->waveSpeed);
                glUniform1i(loc("waterStyle"), river->shaderStyle);
                glUniform1i(loc("meshWater"), item.kind == 2 ? 1 : 0);
                scalar("speed", river->flowing ? river->speed * river->flowAmount : 0);
                glUniform1i(loc("transparentWater"), river->transparent ? 1 : 0);
                scalar("contactStrength", river->collisionStrength);
                const auto contacts =
                    river->collisionEffects && river->collisionStrength > 0
                        ? (item.kind == 2 ? meshContacts(world, item.e, t, assets)
                                          : riverContacts(world, item.e, t, *river))
                        : std::vector<Contact>{};
                glUniform1i(loc("contactCount"), int(contacts.size()));
                if (!contacts.empty()) {
                    glUniform4fv(loc("contacts[0]"), GLsizei(contacts.size()), &contacts[0].x);
                }
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
                if (item.kind == 2) {
                    vertices.clear();
                    try {
                        for (const auto& v : Waters::geometry(world, item.e, assets).vertices) {
                            vertices.push_back({v.position, v.u, v.v, v.normal});
                        }
                    } catch (const std::exception& ex) {
                        error = ex.what();
                        continue;
                    }
                }
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
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(2,
                                      3,
                                      GL_FLOAT,
                                      GL_FALSE,
                                      sizeof(Vertex),
                                      reinterpret_cast<void*>(offsetof(Vertex, normal)));
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
