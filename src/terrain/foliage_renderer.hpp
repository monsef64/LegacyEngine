#pragma once
#include "core/frustum.hpp"
#include "core/hierarchy.hpp"
#include "materials/material_io.hpp"
#include "models/model_asset.hpp"
#include "rendering/material_textures.hpp"
#include "rendering/shader_data.hpp"
#include <glad/gl.h>
#include <map>

class FoliageRenderer {
    struct Part {
        GLuint vao = 0, vbo = 0;
        GLsizei count = 0;
        MaterialProperties material;
        float radius = 1;
    };
    struct Instance {
        Vec3 position;
        float scale, yaw;
    };
    std::map<std::string, std::vector<Part>> cache_;
    std::vector<Instance> visible_;
    std::map<std::string, std::string> failures_;
    MaterialTextureCache textures_;
    std::filesystem::path root_;
    unsigned revision_ = ~0u;
    GLuint program_ = 0, instances_ = 0;
    bool attempted_ = false;
    GLint vp_ = -1, model_ = -1, normal_ = -1, dimensions_ = -1, time_ = -1, grass_ = -1,
          tint_ = -1, textured_ = -1;
    static GLuint shader(GLenum type, const char* source) {
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
    static void release(std::vector<Part>& parts) {
        for (auto& p : parts) {
            if (p.vbo) {
                glDeleteBuffers(1, &p.vbo);
            }
            if (p.vao) {
                glDeleteVertexArrays(1, &p.vao);
            }
        }
        parts.clear();
    }
    void clear() {
        for (auto& p : cache_) {
            release(p.second);
        }
        cache_.clear();
        failures_.clear();
        error.clear();
        textures_.clear();
    }
    Part upload(const MeshData& mesh, MaterialProperties material) {
        Part p;
        p.material = std::move(material);
        p.count = GLsizei(mesh.vertices.size());
        for (const auto& v : mesh.vertices) {
            p.radius = std::max(p.radius, std::sqrt(dot(v.position, v.position)));
        }
        glGenVertexArrays(1, &p.vao);
        glGenBuffers(1, &p.vbo);
        glBindVertexArray(p.vao);
        glBindBuffer(GL_ARRAY_BUFFER, p.vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     mesh.vertices.size() * sizeof(Vertex),
                     mesh.vertices.data(),
                     GL_STATIC_DRAW);
        const std::size_t offsets[]{
            offsetof(Vertex, position), offsetof(Vertex, normal), offsetof(Vertex, u)};
        const int sizes[]{3, 3, 2};
        for (int i = 0; i < 3; ++i) {
            glEnableVertexAttribArray(i);
            glVertexAttribPointer(i,
                                  sizes[i],
                                  GL_FLOAT,
                                  GL_FALSE,
                                  sizeof(Vertex),
                                  reinterpret_cast<void*>(offsets[i]));
        }
        glBindBuffer(GL_ARRAY_BUFFER, instances_);
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Instance), nullptr);
        glVertexAttribDivisor(3, 1);
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4,
                              1,
                              GL_FLOAT,
                              GL_FALSE,
                              sizeof(Instance),
                              reinterpret_cast<void*>(offsetof(Instance, yaw)));
        glVertexAttribDivisor(4, 1);
        return p;
    }
    void load(const AssetManager& assets,
              const std::string& path,
              const std::string& material,
              std::vector<Part>& parts,
              unsigned depth,
              std::size_t& vertices) {
        if (depth > 2 || parts.size() >= 32) {
            throw std::runtime_error("Foliage model exceeds 32 parts");
        }
        auto doc = modelDocument(assets, path);
        if (doc.at("version") == 2) {
            for (const auto& p : doc.at("parts")) {
                load(assets,
                     p.at("mesh").get<std::string>(),
                     p.value("material", std::string{}),
                     parts,
                     depth + 1,
                     vertices);
            }
            return;
        }
        auto mesh = loadModelAsset(assets, path);
        vertices += mesh.vertices.size();
        std::size_t cachedVertices = 0;
        for (const auto& entry : cache_) {
            for (const auto& part : entry.second) {
                cachedVertices += part.count;
            }
        }
        if (vertices + cachedVertices > 2000000) {
            throw std::runtime_error("Foliage geometry exceeds 2 million cached vertices");
        }
        if (vertices > 300000) {
            throw std::runtime_error("Foliage model exceeds 300000 vertices; use a low-poly tree");
        }
        MaterialProperties m;
        if (!material.empty()) {
            m = loadMaterial(assets, material);
        }
        parts.push_back(upload(mesh, m));
    }
    void init() {
        attempted_ = true;
        GLuint v = 0, f = 0;
        try {
            v = shader(GL_VERTEX_SHADER, loadShaderData("foliage.vert").c_str());
            f = shader(GL_FRAGMENT_SHADER, loadShaderData("foliage.frag").c_str());
            program_ = glCreateProgram();
            glAttachShader(program_, v);
            glAttachShader(program_, f);
            glLinkProgram(program_);
            GLint ok = 0;
            glGetProgramiv(program_, GL_LINK_STATUS, &ok);
            if (!ok) {
                throw std::runtime_error("Foliage shader link failed");
            }
            glDeleteShader(v);
            v = 0;
            glDeleteShader(f);
            f = 0;
            glGenBuffers(1, &instances_);
            vp_ = glGetUniformLocation(program_, "vp");
            model_ = glGetUniformLocation(program_, "terrainModel");
            normal_ = glGetUniformLocation(program_, "terrainNormal");
            dimensions_ = glGetUniformLocation(program_, "dimensions");
            time_ = glGetUniformLocation(program_, "time");
            grass_ = glGetUniformLocation(program_, "grass");
            tint_ = glGetUniformLocation(program_, "tint");
            textured_ = glGetUniformLocation(program_, "textured");
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
            SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Foliage: %s", e.what());
        }
    }
    static float height(const Terrains::Data& d, float x, float z) {
        float fx = x / d.size * (d.resolution - 1), fz = z / d.size * (d.resolution - 1);
        int ix = std::clamp(int(fx), 0, d.resolution - 2),
            iz = std::clamp(int(fz), 0, d.resolution - 2);
        float u = fx - ix, v = fz - iz;
        auto a = d.heights[iz * d.resolution + ix], b = d.heights[iz * d.resolution + ix + 1],
             c = d.heights[(iz + 1) * d.resolution + ix],
             e = d.heights[(iz + 1) * d.resolution + ix + 1];
        return u + v <= 1 ? a * (1 - u - v) + b * u + c * v
                          : b * (1 - v) + c * (1 - u) + e * (u + v - 1);
    }

  public:
    std::string error;
    bool streaming() const {
        return textures_.streaming();
    }
    void shutdown() {
        clear();
        if (instances_) {
            glDeleteBuffers(1, &instances_);
        }
        if (program_) {
            glDeleteProgram(program_);
        }
        instances_ = program_ = 0;
        attempted_ = false;
    }
    void draw(World& world, const AssetManager& assets, const Mat4& vp, Vec3 eye) {
        if (root_ != assets.root || revision_ != modelAssetRevision) {
            clear();
            root_ = assets.root;
            revision_ = modelAssetRevision;
        }
        bool any = false;
        world.eachReadOnly<Terrain>([&](Entity e, const Terrain& t) {
            any |=
                isActive(world, e) && t.data && (!t.data->grass.empty() || !t.data->trees.empty());
        });
        if (!any) {
            return;
        }
        GLint oldProgram, oldVao, oldBuffer, oldTexture, oldActive;
        glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldBuffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTexture);
        bool cull = glIsEnabled(GL_CULL_FACE), blend = glIsEnabled(GL_BLEND),
             depth = glIsEnabled(GL_DEPTH_TEST);
        GLboolean write;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &write);
        if (!attempted_) {
            init();
        }
        if (program_) {
            glUseProgram(program_);
            glUniformMatrix4fv(vp_, 1, GL_FALSE, vp.m);
            glUniform1f(time_, float(std::fmod(double(SDL_GetTicksNS()) * 1e-9, 1000.)));
            glUniform1i(glGetUniformLocation(program_, "albedo"), 0);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            glDisable(GL_CULL_FACE);
            textures_.beginFrame();
            ViewFrustum frustum(vp);
            world.eachReadOnly<Transform, Terrain>([&](Entity e,
                                                       const Transform& t,
                                                       const Terrain& terrain) {
                if (!isActive(world, e) || !terrain.data ||
                    std::abs(t.scale.x * t.scale.y * t.scale.z) < 1e-8f) {
                    return;
                }
                const auto& d = *terrain.data;
                auto model = transformMatrix(t.position, t.rotation, t.scale);
                glUniformMatrix4fv(model_, 1, GL_FALSE, model.m);
                auto normals = normalMatrix(t.rotation, t.scale);
                glUniformMatrix3fv(normal_, 1, GL_FALSE, normals.m);
                float terrainScale =
                    std::max({std::abs(t.scale.x), std::abs(t.scale.y), std::abs(t.scale.z)});
                for (bool trees : {false, true}) {
                    const auto& types = trees ? d.treeTypes : d.grassTypes;
                    const auto& placed = trees ? d.trees : d.grass;
                    for (int type = 0; type < int(types.size()); ++type) {
                        const auto& proto = types[type];
                        if (proto.asset.empty()) {
                            continue;
                        }
                        std::string key = trees ? proto.asset : (proto.billboard ? "@billboard" : "@grass");
                        if (failures_.count(key)) {
                            continue;
                        }
                        auto found = cache_.find(key);
                        if (found == cache_.end()) {
                            if (cache_.size() >= 64) {
                                continue;
                            }
                            std::vector<Part> parts;
                            try {
                                if (trees) {
                                    std::size_t vertices = 0;
                                    load(assets, proto.asset, "", parts, 0, vertices);
                                } else {
                                    MeshData mesh;
                                    for (int plane = 0; plane < (proto.billboard ? 1 : 2); ++plane) {
                                        Vec3 a = plane ? Vec3{0, 0, -.5f} : Vec3{-.5f, 0, 0},
                                             b = plane ? Vec3{0, 0, .5f} : Vec3{.5f, 0, 0},
                                             c = b + Vec3{0, 1, 0}, dd = a + Vec3{0, 1, 0},
                                             n = plane ? Vec3{1, 0, 0} : Vec3{0, 0, 1};
                                        // MaterialTextureCache flips image rows for GL: base v=0,
                                        // tips v=1.
                                        mesh.vertices.insert(mesh.vertices.end(),
                                                             {{a, n, 0, 0},
                                                              {b, n, 1, 0},
                                                              {c, n, 1, 1},
                                                              {a, n, 0, 0},
                                                              {c, n, 1, 1},
                                                              {dd, n, 0, 1}});
                                    }
                                    parts.push_back(upload(mesh, MaterialProperties{}));
                                }
                                found = cache_.emplace(key, std::move(parts)).first;
                            } catch (const std::exception& ex) {
                                release(parts);
                                failures_[key] = ex.what();
                                error = ex.what();
                                continue;
                            }
                        }
                        float radius = 1;
                        for (const auto& part : found->second) {
                            radius = std::max(radius, part.radius);
                        }
                        radius *= std::max(proto.width, proto.height) * terrainScale;
                        auto& visible = visible_;
                        visible.clear();
                        visible.reserve(std::min<std::size_t>(placed.size(), 100000));
                        for (auto p : placed) {
                            if (p.prototype != type) {
                                continue;
                            }
                            Vec3 local{p.x, height(d, p.x, p.z), p.z};
                            Vec3 center = t.position + rotateVector(t.rotation, local * t.scale),
                                 delta = center - eye;
                            float r = radius * p.scale;
                            if (dot(delta, delta) > (proto.distance + r) * (proto.distance + r) ||
                                !frustum.visible(center, r)) {
                                continue;
                            }
                            visible.push_back({local, p.scale, p.yaw});
                        }
                        if (visible.empty()) {
                            continue;
                        }
                        glBindBuffer(GL_ARRAY_BUFFER, instances_);
                        glBufferData(GL_ARRAY_BUFFER,
                                     visible.size() * sizeof(Instance),
                                     visible.data(),
                                     GL_STREAM_DRAW);
                        glUniform2f(dimensions_, proto.width, proto.height);
                        glUniform1i(grass_, trees ? 0 : (proto.billboard ? 2 : 1));
                        glUniform3f(glGetUniformLocation(program_, "eye"), eye.x, eye.y, eye.z);
                        for (const auto& part : found->second) {
                            auto path = trees ? part.material.texture : proto.asset;
                            GLuint texture = path.empty() ? 0 : textures_.get(assets, path, error);
                            if (!trees && !texture) {
                                continue;
                            }
                            Vec3 color = proto.color * part.material.color;
                            glUniform3f(tint_, color.x, color.y, color.z);
                            glUniform1i(textured_, texture ? 1 : 0);
                            glBindTexture(GL_TEXTURE_2D, texture);
                            glBindVertexArray(part.vao);
                            glDrawArraysInstanced(
                                GL_TRIANGLES, 0, part.count, GLsizei(visible.size()));
                        }
                    }
                }
            });
        }
        glDepthMask(write);
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
        if (!depth) {
            glDisable(GL_DEPTH_TEST);
        }
        glBindTexture(GL_TEXTURE_2D, oldTexture);
        glActiveTexture(oldActive);
        glUseProgram(oldProgram);
        glBindVertexArray(oldVao);
        glBindBuffer(GL_ARRAY_BUFFER, oldBuffer);
    }
};
