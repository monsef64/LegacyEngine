#include "project/layers.hpp"
#pragma once
#include "core/frustum.hpp"
#include "core/hierarchy.hpp"
#include "lighting/lighting.hpp"
#include "lighting/lightmap_state.hpp"
#include "rendering/material_textures.hpp"
#include "rendering/occlusion_bake.hpp"
#include "rendering/shader_data.hpp"
#include "terrain/terrain_mesh.hpp"
#include <SDL3/SDL.h>
#include <glad/gl.h>
#include <map>
#include <tuple>
namespace Terrains {
class Renderer {
    struct Buffer {
        GLuint vao = 0, vbo = 0;
        int count = 0;
    };
    struct Chunk {
        unsigned identity = 0, revision = 0;
        std::array<Buffer, 4> lod{};
        Vec3 center;
        float radius = 0;
        std::uint64_t used = 0;
    };
    std::map<std::tuple<unsigned, unsigned, int>, Chunk> chunks_;
    struct Depth {
        Buffer buffer;
        unsigned identity = 0, revision = 0;
        Vec3 center;
        float radius = 0;
    };
    std::map<std::tuple<unsigned, unsigned, int>, Depth> depth_;
    static void releaseDepth(Depth& d) {
        if (d.buffer.vao) {
            glDeleteVertexArrays(1, &d.buffer.vao);
        }
        if (d.buffer.vbo) {
            glDeleteBuffers(1, &d.buffer.vbo);
        }
        d = {};
    }
    struct UniformCopy {
        GLint from, to;
        GLenum type;
    };
    std::vector<UniformCopy> uniforms_;
    GLuint sourceProgram_ = 0;
    std::map<std::string, GLint, std::less<>> locations_;
    GLint location(const char* name) {
        auto it = locations_.find(name);
        if (it != locations_.end()) {
            return it->second;
        }
        return locations_.emplace(name, glGetUniformLocation(program_, name)).first->second;
    }
    Lightmapping::ValidationCache lightmaps_;
    void copyLighting(GLuint source) {
        if (sourceProgram_ != source) {
            uniforms_.clear();
            sourceProgram_ = source;
            GLint count = 0;
            glGetProgramiv(source, GL_ACTIVE_UNIFORMS, &count);
            for (int i = 0; i < count; ++i) {
                char name[256]{};
                GLsizei length;
                GLint size;
                GLenum type;
                glGetActiveUniform(source, i, sizeof(name), &length, &size, &type, name);
                std::string base = name;
                auto bracket = base.find("[0]");
                if (bracket != std::string::npos) {
                    base.resize(bracket);
                }
                for (int j = 0; j < size; ++j) {
                    auto n = size > 1 ? base + "[" + std::to_string(j) + "]" : std::string(name);
                    auto to = location(n.c_str());
                    if (to >= 0) {
                        uniforms_.push_back({glGetUniformLocation(source, n.c_str()), to, type});
                    }
                }
            }
        }
        for (const auto& u : uniforms_) {
            GLfloat f[16]{};
            GLint i[4]{};
            switch (u.type) {
            case GL_FLOAT:
                glGetUniformfv(source, u.from, f);
                glUniform1f(u.to, f[0]);
                break;
            case GL_FLOAT_VEC2:
                glGetUniformfv(source, u.from, f);
                glUniform2fv(u.to, 1, f);
                break;
            case GL_FLOAT_VEC3:
                glGetUniformfv(source, u.from, f);
                glUniform3fv(u.to, 1, f);
                break;
            case GL_FLOAT_VEC4:
                glGetUniformfv(source, u.from, f);
                glUniform4fv(u.to, 1, f);
                break;
            case GL_FLOAT_MAT4:
                glGetUniformfv(source, u.from, f);
                glUniformMatrix4fv(u.to, 1, GL_FALSE, f);
                break;
            case GL_INT:
            case GL_BOOL:
            case GL_SAMPLER_2D:
            case GL_SAMPLER_CUBE:
            case GL_SAMPLER_2D_ARRAY:
            case GL_SAMPLER_2D_ARRAY_SHADOW:
                glGetUniformiv(source, u.from, i);
                glUniform1i(u.to, i[0]);
                break;
            default:
                break;
            }
        }
    }
    GLuint program_ = 0;
    bool attempted_ = false;
    std::uint64_t frame_ = 0;
    MaterialTextureCache textures_;
    MaterialAssetCache materials_;
    std::filesystem::path root_;
    static GLuint shader(GLenum type, const char* text) {
        GLuint id = glCreateShader(type);
        glShaderSource(id, 1, &text, nullptr);
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
    static void release(Chunk& c) {
        for (auto& b : c.lod) {
            if (b.vao) {
                glDeleteVertexArrays(1, &b.vao);
            }
            if (b.vbo) {
                glDeleteBuffers(1, &b.vbo);
            }
            b = {};
        }
    }
    void init() {
        attempted_ = true;
        GLuint v = 0, f = 0;
        try {
            v = shader(GL_VERTEX_SHADER, R"(#version 330 core
layout(location=0)in vec3 aPosition;layout(location=1)in vec3 aNormal;layout(location=2)in vec4 aWeights;
uniform mat4 uVP,uModel;uniform mat3 uNormal;uniform float uTerrainSize;
out vec3 vNormal,vWorldPosition;out vec2 vUV,vCheckerUV,vLightmapUV;out vec4 vWeights;
void main(){vec4 p=uModel*vec4(aPosition,1);vWorldPosition=p.xyz;vNormal=normalize(uNormal*aNormal);vUV=aPosition.xz;vCheckerUV=vec2(0);vLightmapUV=aPosition.xz/uTerrainSize;vWeights=aWeights;gl_Position=uVP*p;}
)");
            // Reuse the mesh lighting implementation, including PCF/CSM, cookies,
            // baked-direct masking and dynamic occluders. Only material evaluation differs.
            auto fragment = loadShaderData("lit.frag");
            const std::string helpers = R"(
in vec4 vWeights;
uniform sampler2D uTexture0,uTexture1,uTexture2,uTexture3;
uniform sampler2D uNormal0,uNormal1,uNormal2,uNormal3;
uniform vec3 uColors[4];uniform vec2 uLayerUV[4];uniform vec4 uNormalStrengths;
uniform bvec4 uHasTexture,uHasNormal;
vec2 terrainHash(vec2 x){return fract(sin(vec2(dot(x,vec2(127.1,311.7)),dot(x,vec2(269.5,183.3))))*43758.5453);}
vec3 terrainTile(sampler2D tex,vec2 cell,vec2 p,vec2 dx,vec2 dy,bool normalMap){
    vec2 h=terrainHash(cell);float angle=floor(h.x*4.)*1.5707963;
    mat2 r=mat2(cos(angle),-sin(angle),sin(angle),cos(angle));vec3 value=textureGrad(tex,r*p+h,r*dx,r*dy).rgb;
    if(normalMap){value=value*2.-1.;value.xy=transpose(r)*value.xy;}return value;
}
vec3 terrainBomb(sampler2D tex,vec2 p,vec2 dx,vec2 dy,bool normalMap){
    vec2 q=vec2(p.x-p.y*.57735027,p.y*1.15470054),cell=floor(q),f=fract(q),a=cell,b,c;vec3 w;
    if(f.x+f.y<1.){b=cell+vec2(1,0);c=cell+vec2(0,1);w=vec3(1.-f.x-f.y,f.x,f.y);}
    else{a=cell+vec2(1,1);b=cell+vec2(0,1);c=cell+vec2(1,0);w=vec3(f.x+f.y-1.,1.-f.x,1.-f.y);}
    w=w*w*w;w/=max(dot(w,vec3(1)),.00001);
    return terrainTile(tex,a,p,dx,dy,normalMap)*w.x+terrainTile(tex,b,p,dx,dy,normalMap)*w.y+terrainTile(tex,c,p,dx,dy,normalMap)*w.z;
}
vec3 terrainLayerNormal(sampler2D tex,vec2 p,vec2 dx,vec2 dy,float strength){vec3 n=terrainBomb(tex,p,dx,dy,true);n.xy*=strength;return normalize(vec3(n.xy,max(n.z,.001)));}
)";
            auto main = fragment.find("void main() {");
            auto lighting = fragment.find("    vec3 viewDirection", main);
            if (main == std::string::npos || lighting == std::string::npos) {
                throw std::runtime_error("Terrain lighting shader interface changed");
            }
            fragment.replace(main, lighting - main, helpers + R"(
void main(){
    vec3 worldDx=dFdx(vWorldPosition),worldDy=dFdy(vWorldPosition);vec2 dx=dFdx(vUV),dy=dFdy(vUV);
    vec4 w=max(vWeights,vec4(0));w/=max(dot(w,vec4(1)),.0001);vec3 albedo=vec3(0);vec3 mapped=vec3(0);float alpha=1.;
    if(w.x>.0001)albedo+=w.x*uColors[0]*(uHasTexture.x?terrainBomb(uTexture0,vUV*uLayerUV[0],dx*uLayerUV[0],dy*uLayerUV[0],false):vec3(1));
    if(w.y>.0001)albedo+=w.y*uColors[1]*(uHasTexture.y?terrainBomb(uTexture1,vUV*uLayerUV[1],dx*uLayerUV[1],dy*uLayerUV[1],false):vec3(1));
    if(w.z>.0001)albedo+=w.z*uColors[2]*(uHasTexture.z?terrainBomb(uTexture2,vUV*uLayerUV[2],dx*uLayerUV[2],dy*uLayerUV[2],false):vec3(1));
    if(w.w>.0001)albedo+=w.w*uColors[3]*(uHasTexture.w?terrainBomb(uTexture3,vUV*uLayerUV[3],dx*uLayerUV[3],dy*uLayerUV[3],false):vec3(1));
    if(w.x>.0001)mapped+=w.x*(uHasNormal.x?terrainLayerNormal(uNormal0,vUV*uLayerUV[0],dx*uLayerUV[0],dy*uLayerUV[0],uNormalStrengths.x):vec3(0,0,1));
    if(w.y>.0001)mapped+=w.y*(uHasNormal.y?terrainLayerNormal(uNormal1,vUV*uLayerUV[1],dx*uLayerUV[1],dy*uLayerUV[1],uNormalStrengths.y):vec3(0,0,1));
    if(w.z>.0001)mapped+=w.z*(uHasNormal.z?terrainLayerNormal(uNormal2,vUV*uLayerUV[2],dx*uLayerUV[2],dy*uLayerUV[2],uNormalStrengths.z):vec3(0,0,1));
    if(w.w>.0001)mapped+=w.w*(uHasNormal.w?terrainLayerNormal(uNormal3,vUV*uLayerUV[3],dx*uLayerUV[3],dy*uLayerUV[3],uNormalStrengths.w):vec3(0,0,1));
    vec3 normal=normalize(vNormal);float det=dx.x*dy.y-dx.y*dy.x;
    if(abs(det)>1e-10){vec3 t=(worldDx*dy.y-worldDy*dx.y)/det,b=(-worldDx*dy.x+worldDy*dx.x)/det;t-=normal*dot(normal,t);if(dot(t,t)>1e-10){t=normalize(t);b=normalize(cross(normal,t))*sign(dot(cross(normal,t),b));normal=normalize(t*mapped.x+b*mapped.y+normal*mapped.z);}}
)");
            f = shader(GL_FRAGMENT_SHADER, fragment.c_str());
            program_ = glCreateProgram();
            glAttachShader(program_, v);
            glAttachShader(program_, f);
            glLinkProgram(program_);
            GLint ok = 0;
            glGetProgramiv(program_, GL_LINK_STATUS, &ok);
            if (!ok) {
                throw std::runtime_error("Terrain shader link failed");
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
            error = e.what();
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Terrain: %s", e.what());
        }
    }

  public:
    std::string error;
    // Exact surface only: no LOD skirts in shadow maps. Dynamic-only passes
    // omit static terrain so its baked soft self-shadow is not applied twice.
    void drawDepth(World& world, const Mat4& vp, GLint mvpLocation, bool dynamicOnly) {
        const ViewFrustum frustum(vp);
        for (auto it = depth_.begin(); it != depth_.end();) {
            if (!world.alive(Entity{std::get<0>(it->first), std::get<1>(it->first)}) ||
                !world.has<Terrain>(Entity{std::get<0>(it->first), std::get<1>(it->first)})) {
                releaseDepth(it->second);
                it = depth_.erase(it);
            } else {
                ++it;
            }
        }
        world.eachReadOnly<Transform, Terrain>(
            [&](Entity e, const Transform& t, const Terrain& terrain) {
                if (!terrain.data || !isActive(world, e) || (dynamicOnly && t.isStatic)) {
                    return;
                }
                const auto& d = *terrain.data;
                const auto model = transformMatrix(t.position, t.rotation, t.scale);
                const auto mvp = multiply(vp, model);
                glUniformMatrix4fv(mvpLocation, 1, GL_FALSE, mvp.m);
                glFrontFace(t.scale.x * t.scale.y * t.scale.z < 0 ? GL_CW : GL_CCW);
                const int count = chunkCount(d);
                for (int z = 0; z < count; ++z) {
                    for (int x = 0; x < count; ++x) {
                        const auto& bounds = chunkBounds(d, x, z);
                        if (!frustum.visibleBox(bounds.center, bounds.extent, model)) {
                            continue;
                        }
                        const int index = z * count + x;
                        const auto key = std::make_tuple(e.index, e.generation, index);
                        auto it = depth_.find(key);
                        if (it == depth_.end()) {
                            if (depth_.size() >= 1024) {
                                releaseDepth(depth_.begin()->second);
                                depth_.erase(depth_.begin());
                            }
                            it = depth_.emplace(key, Depth{}).first;
                        }
                        auto& c = it->second;
                        if (c.identity != d.identity || c.revision != d.heightChunks[index]) {
                            releaseDepth(c);
                            const auto vertices = chunk(d, x, z, 1);
                            std::vector<Vec3> positions;
                            positions.reserve(vertices.size());
                            for (const auto& vertex : vertices) {
                                positions.push_back(vertex.position);
                            }
                            c.buffer.count = int(positions.size());
                            glGenVertexArrays(1, &c.buffer.vao);
                            glGenBuffers(1, &c.buffer.vbo);
                            glBindVertexArray(c.buffer.vao);
                            glBindBuffer(GL_ARRAY_BUFFER, c.buffer.vbo);
                            glBufferData(GL_ARRAY_BUFFER,
                                         positions.size() * sizeof(Vec3),
                                         positions.data(),
                                         GL_STATIC_DRAW);
                            glEnableVertexAttribArray(0);
                            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vec3), nullptr);
                            c.identity = d.identity;
                            c.revision = d.heightChunks[index];
                        }
                        glBindVertexArray(c.buffer.vao);
                        glDrawArrays(GL_TRIANGLES, 0, c.buffer.count);
                    }
                }
            });
    }
    void shutdown() {
        for (auto& pair : depth_) {
            releaseDepth(pair.second);
        }
        depth_.clear();
        for (auto& p : chunks_) {
            release(p.second);
        }
        chunks_.clear();
        textures_.clear();
        materials_.clear();
        if (program_) {
            glDeleteProgram(program_);
        }
        program_ = 0;
        attempted_ = false;
        locations_.clear();
        sourceProgram_ = 0;
        uniforms_.clear();
    }
    void draw(World& world,
              const AssetManager& assets,
              const Mat4& vp,
              Vec3 eye,
              GLuint lightingProgram,
              const std::vector<Lighting::Light>& lights,
              Occlusion::Bake* occlusion = nullptr) {
        bool any = false;
        world.eachReadOnly<Terrain>(
            [&](Entity e, const Terrain& t) { any |= bool(t.data) && isActive(world, e); });
        if (!any) {
            for (auto& p : chunks_) {
                release(p.second);
            }
            chunks_.clear();
            return;
        }
        if (root_ != assets.root) {
            shutdown();
            root_ = assets.root;
        }
        if (!attempted_) {
            init();
        }
        if (!program_) {
            return;
        }
        ++frame_;
        textures_.beginFrame();
        const ViewFrustum frustum(vp);
        GLint oldProgram, oldVao, oldBuffer, active, front;
        glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldBuffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
        glGetIntegerv(GL_FRONT_FACE, &front);
        std::array<GLint, 13> bindings{};
        for (int i = 0; i < 13; ++i) {
            glActiveTexture(GL_TEXTURE0 + i);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &bindings[i]);
        }
        glUseProgram(program_);
        glUniformMatrix4fv(location("uVP"), 1, GL_FALSE, vp.m);
        copyLighting(lightingProgram);
        glUniform1i(location("uReflective"), false);
        glUniform1f(location("uSpecular"), .1f);
        glUniform3f(location("uCameraPosition"), eye.x, eye.y, eye.z);
        glUniform1i(location("uHasIndirectLightmap"), false);
        glUniform1i(location("uLightmap"), 4);
        world.eachReadOnly<Transform, Terrain>([&](Entity e,
                                                   const Transform& t,
                                                   const Terrain& terrain) {
            if (!terrain.data || !isActive(world, e)) {
                return;
            }
            const auto& d = *terrain.data;
            const auto model = transformMatrix(t.position, t.rotation, t.scale);
            // Reject fully offscreen terrains before material resolution, texture jobs,
            // lightmap validation, or per-layer GL state updates. Keep full detail.
            const int count = chunkCount(d);
            bool inView = false;
            for (int z = 0; z < count && !inView; ++z) {
                for (int x = 0; x < count; ++x) {
                    const auto& bounds = chunkBounds(d, x, z);
                    if (frustum.visibleBox(bounds.center, bounds.extent, model)) {
                        inView = true;
                        break;
                    }
                }
            }
            if (!inView) {
                return;
            }
            const auto normals = normalMatrix(t.rotation, t.scale);
            glUniformMatrix4fv(location("uModel"), 1, GL_FALSE, model.m);
            glUniformMatrix3fv(location("uNormal"), 1, GL_FALSE, normals.m);
            glFrontFace(t.scale.x * t.scale.y * t.scale.z < 0 ? GL_CW : GL_CCW);
            glUniform1f(location("uTerrainSize"), d.size);
            MeshRenderer baked;
            baked.lightmap = d.lightmap;
            baked.lightmapGrid = 1;
            auto atlas = lightmaps_.valid(assets, baked, "")
                             ? textures_.get(assets, d.lightmap, error, true)
                             : 0;
            glActiveTexture(GL_TEXTURE4);
            glBindTexture(GL_TEXTURE_2D, atlas);
            if (atlas) {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            }
            glUniform1i(location("uLightmapGrid"), atlas ? 1 : 0);
            glUniform1i(location("uBakedSoft"), atlas && lightmaps_.softDirect(baked));
            const auto manifest =
                (std::filesystem::path(d.lightmap).parent_path() / "bake.json").generic_string();
            for (std::size_t i = 0; i < lights.size(); ++i) {
                const auto& l = lights[i].settings;
                glUniform1i(location(("uLightBaked[" + std::to_string(i) + "]").c_str()),
                            atlas && l.mode == 1 && l.bake == manifest);
            }
            GLint has[4]{}, hasNormal[4]{};
            const int units[]{0, 2, 3, 5};
            GLfloat strengths[4]{};
            for (int i = 0; i < 4; ++i) {
                auto layer = d.layers[i];
                float tilingX = 1, tilingY = 1;
                if (!layer.material.empty()) {
                    try {
                        const auto material = materials_.get(assets, layer.material);
                        layer.texture = material.texture;
                        layer.normalMap = material.normalMap;
                        layer.color = layer.color * material.color;
                        layer.normalStrength *= material.normalStrength;
                        tilingX = material.tilingX;
                        tilingY = material.tilingY;
                    } catch (const std::exception& e) {
                        error = e.what();
                    }
                }
                strengths[i] = layer.normalStrength;
                glUniform2f(location(("uLayerUV[" + std::to_string(i) + "]").c_str()),
                            layer.tiling / d.size * tilingX,
                            layer.tiling / d.size * tilingY);
                glActiveTexture(GL_TEXTURE0 + units[i]);
                const auto texture = textures_.get(assets, layer.texture, error);
                glBindTexture(GL_TEXTURE_2D, texture);
                has[i] = texture != 0;
                glUniform1i(location(("uTexture" + std::to_string(i)).c_str()), units[i]);
                glActiveTexture(GL_TEXTURE9 + i);
                auto normal = textures_.get(assets, layer.normalMap, error);
                glBindTexture(GL_TEXTURE_2D, normal);
                hasNormal[i] = normal != 0;
                glUniform1i(location(("uNormal" + std::to_string(i)).c_str()), 9 + i);
                const auto c = layer.color;
                glUniform3f(
                    location(("uColors[" + std::to_string(i) + "]").c_str()), c.x, c.y, c.z);
            }
            glUniform4iv(location("uHasTexture"), 1, has);
            glUniform4iv(location("uHasNormal"), 1, hasNormal);
            glUniform4fv(location("uNormalStrengths"), 1, strengths);
            for (int z = 0; z < count; ++z) {
                for (int x = 0; x < count; ++x) {
                    const int index = z * count + x;
                    const auto& bounds = chunkBounds(d, x, z);
                    if (!frustum.visibleBox(bounds.center, bounds.extent, model)) {
                        continue;
                    }
                    if (!RenderVisibility::visible(world,
                                                   e,
                                                   t,
                                                   bounds.center - bounds.extent,
                                                   bounds.center + bounds.extent)) {
                        continue;
                    }
                    if (RenderVisibility::world != &world && occlusion && occlusion->valid &&
                        Occlusion::eligible(world, e, t, MeshRenderer{}) &&
                        occlusion->hidden(e,
                                          t,
                                          bounds.center - bounds.extent,
                                          bounds.center + bounds.extent,
                                          eye)) {
                        continue;
                    }
                    auto key = std::make_tuple(e.index, e.generation, index);
                    auto it = chunks_.find(key);
                    if (it == chunks_.end()) {
                        if (chunks_.size() >= 1024) {
                            auto oldest = chunks_.end();
                            for (auto p = chunks_.begin(); p != chunks_.end(); ++p) {
                                if (p->second.used != frame_ &&
                                    (oldest == chunks_.end() ||
                                     p->second.used < oldest->second.used)) {
                                    oldest = p;
                                }
                            }
                            // Entries already drawn this view can be evicted safely:
                            // GL defers deletion until submitted draws complete.
                            if (oldest == chunks_.end()) {
                                oldest = chunks_.begin();
                            }
                            release(oldest->second);
                            chunks_.erase(oldest);
                        }
                        it = chunks_.emplace(key, Chunk{}).first;
                    }
                    auto& c = it->second;
                    if (c.identity != d.identity || c.revision != d.chunks[index]) {
                        release(c);
                        c.identity = d.identity;
                        c.revision = d.chunks[index];
                    }
                    c.used = frame_;
                    // Uniform surface detail avoids cracks without introducing
                    // downward skirt walls between different chunk LODs.
                    const int lod = 0;
                    auto& b = c.lod[lod];
                    if (!b.vao) {
                        auto vertices = chunk(d, x, z, 1 << lod);
                        b.count = int(vertices.size());
                        glGenVertexArrays(1, &b.vao);
                        glGenBuffers(1, &b.vbo);
                        glBindVertexArray(b.vao);
                        glBindBuffer(GL_ARRAY_BUFFER, b.vbo);
                        glBufferData(GL_ARRAY_BUFFER,
                                     vertices.size() * sizeof(Vertex),
                                     vertices.data(),
                                     GL_STATIC_DRAW);
                        const int sizes[]{3, 3, 4};
                        const std::size_t offsets[]{offsetof(Vertex, position),
                                                    offsetof(Vertex, normal),
                                                    offsetof(Vertex, weights)};
                        for (int a = 0; a < 3; ++a) {
                            glEnableVertexAttribArray(a);
                            glVertexAttribPointer(a,
                                                  sizes[a],
                                                  GL_FLOAT,
                                                  GL_FALSE,
                                                  sizeof(Vertex),
                                                  reinterpret_cast<void*>(offsets[a]));
                        }
                    }
                    glBindVertexArray(b.vao);
                    glStencilFunc(GL_ALWAYS,LayerSettings::effective(world,e),0xff);
                    glDrawArrays(GL_TRIANGLES, 0, b.count);
                }
            }
        });
        for (auto it = chunks_.begin(); it != chunks_.end();) {
            if (frame_ - it->second.used > 120) {
                release(it->second);
                it = chunks_.erase(it);
            } else {
                ++it;
            }
        }
        for (int i = 0; i < 13; ++i) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, bindings[i]);
        }
        glActiveTexture(active);
        glFrontFace(front);
        glUseProgram(oldProgram);
        glBindVertexArray(oldVao);
        glBindBuffer(GL_ARRAY_BUFFER, oldBuffer);
    }
};
} // namespace Terrains
