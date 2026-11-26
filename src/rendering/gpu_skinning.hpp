#pragma once
#include "models/model_asset.hpp"
#include "models/skeleton_pose.hpp"
#include "rendering/shader_data.hpp"
#include <cstring>
#include <glad/gl.h>
#include <map>

// OpenGL 3.3 transform feedback skins once into the ordinary mesh VBO. All
// subsequent passes (including cascaded shadows) consume the same GPU result.
class GpuSkinning {
  public:
    struct PreparedSource {
        std::vector<Vertex> data;
        std::vector<unsigned> ranges;
        std::vector<float> influences;
        Vec3 low{}, high{};
    };
    static PreparedSource prepareSource(const Json& part) {
        const auto boneCount = part.at("bones").size();
        const auto& vertices = part.at("vertices");
        const auto& weights = part.at("weights");
        if (!vertices.is_array() || vertices.empty() || vertices.size() > maxModelVertices ||
            !weights.is_array() || weights.size() != vertices.size()) {
            throw std::runtime_error("Invalid GPU skin data");
        }
        PreparedSource result;
        auto& data = result.data;
        auto& ranges = result.ranges;
        auto& influences = result.influences;
        data.reserve(vertices.size());
        ranges.reserve(vertices.size() * 2);
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            const auto& v = vertices[i];
            if (!v.is_array() || v.size() != 8) {
                throw std::runtime_error("Invalid vertex");
            }
            Vertex vertex{{v[0], v[1], v[2]}, {v[3], v[4], v[5]}, v[6], v[7]};
            if (!validModelVertex(vertex)) {
                throw std::runtime_error("Invalid GPU vertex");
            }
            if (data.empty()) {
                result.low = result.high = vertex.position;
            } else {
                result.low = {std::min(result.low.x, vertex.position.x),
                              std::min(result.low.y, vertex.position.y),
                              std::min(result.low.z, vertex.position.z)};
                result.high = {std::max(result.high.x, vertex.position.x),
                               std::max(result.high.y, vertex.position.y),
                               std::max(result.high.z, vertex.position.z)};
            }
            data.push_back(vertex);
            unsigned start = unsigned(influences.size() / 2);
            float total = 0;
            if (!weights[i].is_array() || weights[i].size() > 4096) {
                throw std::runtime_error("Invalid influences");
            }
            for (const auto& w : weights[i]) {
                int b = w.at(0);
                float value = w.at(1);
                if (b < 0 || std::size_t(b) >= boneCount || !std::isfinite(value) || value < 0) {
                    throw std::runtime_error("Invalid bone weight");
                }
                if (value > 0) {
                    influences.push_back(float(b));
                    influences.push_back(value);
                    total += value;
                    if (!std::isfinite(total)) {
                        throw std::runtime_error("Bone weight sum overflow");
                    }
                }
            }
            if (total <= 0) {
                influences.push_back(float(boneCount));
                influences.push_back(1);
            }
            if (influences.size() > 32 * 1024 * 1024) {
                throw std::runtime_error("Skin influences exceed 128 MiB");
            }
            ranges.push_back(start);
            ranges.push_back(unsigned(influences.size() / 2) - start);
        }
        return result;
    }
    void publishSource(const std::string& path, PreparedSource source) {
        if (!sources_.count(path)) {
            preparedSources_.insert_or_assign(path, std::move(source));
        }
    }
    void publishSkeleton(const Json* root, std::shared_ptr<const SkeletonPose> pose) {
        skeletons_.insert_or_assign(root, std::move(pose));
    }

  private:
    std::map<std::string, PreparedSource> preparedSources_;
    static_assert(sizeof(Vertex) == 8 * sizeof(float),
                  "Transform feedback requires tightly packed Vertex");
    struct Source {
        GLuint vao = 0, vertices = 0, ranges = 0, weights = 0, weightTexture = 0;
        std::size_t count = 0;
        Vec3 low{}, high{};
    };
    std::map<std::string, Source> sources_;
    std::map<const Json*, std::shared_ptr<const SkeletonPose>> skeletons_;
    struct Binding {
        int node;
        std::vector<std::pair<int, Mat4>> bones;
    };
    std::map<const Json*, Binding> bindings_;
    std::map<std::pair<const Json*, const Animation*>, std::vector<Mat4>> poses_;
    struct Output {
        std::vector<Mat4> palette;
        float scale = 0;
        Vec3 low{}, high{};
        float radius = 0;
    };
    std::map<std::pair<GLuint, GLintptr>, Output> outputs_;
    GLint weightsLocation_ = -1, paletteLocation_ = -1, scaleLocation_ = -1;
    GLuint program_ = 0, palette_ = 0, paletteTexture_ = 0;
    void init() {
        if (program_) {
            return;
        }
        const auto text = loadShaderData("skinning.vert");
        const char* ptr = text.c_str();
        GLuint shader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(shader, 1, &ptr, nullptr);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048]{};
            glGetShaderInfoLog(shader, 2048, nullptr, log);
            glDeleteShader(shader);
            throw std::runtime_error(std::string("GPU skinning shader: ") + log);
        }
        GLuint program = glCreateProgram();
        glAttachShader(program, shader);
        const char* outputs[]{"skinnedPosition", "skinnedNormal", "skinnedUV"};
        glTransformFeedbackVaryings(program, 3, outputs, GL_INTERLEAVED_ATTRIBS);
        glLinkProgram(program);
        glDeleteShader(shader);
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[2048]{};
            glGetProgramInfoLog(program, 2048, nullptr, log);
            glDeleteProgram(program);
            throw std::runtime_error(std::string("GPU skinning link: ") + log);
        }
        program_ = program;
        weightsLocation_ = glGetUniformLocation(program_, "uWeights");
        paletteLocation_ = glGetUniformLocation(program_, "uPalette");
        scaleLocation_ = glGetUniformLocation(program_, "uScale");
        glGenBuffers(1, &palette_);
        glGenTextures(1, &paletteTexture_);
    }

  public:
    void beginDraw() {
        poses_.clear();
    }
    void clear() {
        for (auto& entry : sources_) {
            auto& s = entry.second;
            glDeleteVertexArrays(1, &s.vao);
            GLuint buffers[]{s.vertices, s.ranges, s.weights};
            glDeleteBuffers(3, buffers);
            glDeleteTextures(1, &s.weightTexture);
        }
        sources_.clear();
        preparedSources_.clear();
        skeletons_.clear();
        bindings_.clear();
        poses_.clear();
        outputs_.clear();
    }
    void shutdown() {
        clear();
        glDeleteProgram(program_);
        glDeleteBuffers(1, &palette_);
        glDeleteTextures(1, &paletteTexture_);
        program_ = palette_ = paletteTexture_ = 0;
    }
    void skin(const std::string& key,
              const Json& part,
              const Json& root,
              const Animation& animation,
              GLuint output,
              Vec3& low,
              Vec3& high,
              float& radius,
              GLintptr outputOffset = 0) {
        init();
        auto poseKey = std::make_pair(&root, &animation);
        auto pose = poses_.find(poseKey);
        if (pose == poses_.end()) {
            auto skeleton = skeletons_.find(&root);
            if (skeleton == skeletons_.end()) {
                skeleton =
                    skeletons_.emplace(&root, std::make_shared<const SkeletonPose>(root)).first;
            }
            pose = poses_.emplace(poseKey, skeleton->second->evaluate(animation)).first;
        }
        const auto& global = pose->second;
        const auto& bones = part.at("bones");
        int node = part.at("node");
        if (node < 0 || std::size_t(node) >= global.size() || !bones.is_array() ||
            bones.size() > 4096) {
            throw std::runtime_error("Invalid GPU skeleton");
        }
        auto binding = bindings_.find(&part);
        if (binding == bindings_.end()) {
            Binding decoded{node, {}};
            decoded.bones.reserve(bones.size());
            for (const auto& b : bones) {
                int i = b.at("node");
                if (i < 0 || std::size_t(i) >= global.size()) {
                    throw std::runtime_error("Invalid bone node");
                }
                decoded.bones.push_back({i, modelMatrix(b.at("offset"))});
            }
            binding = bindings_.emplace(&part, std::move(decoded)).first;
        }
        std::vector<Mat4> palette;
        palette.reserve(binding->second.bones.size() + 1);
        for (const auto& bone : binding->second.bones) {
            palette.push_back(multiply(global[bone.first], bone.second));
        }
        palette.push_back(global[binding->second.node]);
        auto found = sources_.find(key);
        if (found == sources_.end()) {
            auto prepared = preparedSources_.find(key);
            PreparedSource fallback;
            if (prepared == preparedSources_.end()) {
                fallback = prepareSource(part);
            }
            auto& decoded = prepared == preparedSources_.end() ? fallback : prepared->second;
            const auto& data = decoded.data;
            const auto& ranges = decoded.ranges;
            const auto& influences = decoded.influences;
            GLint limit = 0;
            glGetIntegerv(GL_MAX_TEXTURE_BUFFER_SIZE, &limit);
            if (influences.size() / 2 > std::size_t(limit) ||
                palette.size() * 8 > std::size_t(limit)) {
                throw std::runtime_error("GPU skin texture-buffer limit exceeded");
            }
            Source s;
            s.count = data.size();
            s.low = decoded.low;
            s.high = decoded.high;
            glGenVertexArrays(1, &s.vao);
            glBindVertexArray(s.vao);
            glGenBuffers(1, &s.vertices);
            glBindBuffer(GL_ARRAY_BUFFER, s.vertices);
            glBufferData(GL_ARRAY_BUFFER,
                         GLsizeiptr(data.size() * sizeof(Vertex)),
                         data.data(),
                         GL_STATIC_DRAW);
            for (int a = 0; a < 3; ++a) {
                glEnableVertexAttribArray(a);
                glVertexAttribPointer(a,
                                      a == 2 ? 2 : 3,
                                      GL_FLOAT,
                                      GL_FALSE,
                                      sizeof(Vertex),
                                      reinterpret_cast<void*>(a == 0   ? offsetof(Vertex, position)
                                                              : a == 1 ? offsetof(Vertex, normal)
                                                                       : offsetof(Vertex, u)));
            }
            glGenBuffers(1, &s.ranges);
            glBindBuffer(GL_ARRAY_BUFFER, s.ranges);
            glBufferData(GL_ARRAY_BUFFER,
                         GLsizeiptr(ranges.size() * sizeof(unsigned)),
                         ranges.data(),
                         GL_STATIC_DRAW);
            glEnableVertexAttribArray(3);
            glVertexAttribIPointer(3, 2, GL_UNSIGNED_INT, 2 * sizeof(unsigned), nullptr);
            glGenBuffers(1, &s.weights);
            glBindBuffer(GL_TEXTURE_BUFFER, s.weights);
            glBufferData(GL_TEXTURE_BUFFER,
                         GLsizeiptr(influences.size() * sizeof(float)),
                         influences.data(),
                         GL_STATIC_DRAW);
            glGenTextures(1, &s.weightTexture);
            glBindTexture(GL_TEXTURE_BUFFER, s.weightTexture);
            glTexBuffer(GL_TEXTURE_BUFFER, GL_RG32F, s.weights);
            found = sources_.emplace(key, s).first;
            if (prepared != preparedSources_.end()) {
                preparedSources_.erase(prepared);
            }
        }
        const auto& source = found->second;
        float scale = modelImportScale(root) * modelImportScale(part);
        auto& previous = outputs_[{output, outputOffset}];
        if (previous.scale == scale && previous.palette.size() == palette.size() &&
            !std::memcmp(previous.palette.data(), palette.data(), palette.size() * sizeof(Mat4))) {
            low = previous.low;
            high = previous.high;
            radius = previous.radius;
            return;
        }
        std::vector<Mat4> upload;
        upload.reserve(palette.size() * 2);
        for (const auto& m : palette) {
            upload.push_back(m);
            Mat4 normal{};
            normal.m[15] = 1;
            for (int i = 0; i < 3; ++i) {
                Vec3 axis{};
                if (i == 0) {
                    axis.x = 1;
                }
                if (i == 1) {
                    axis.y = 1;
                }
                if (i == 2) {
                    axis.z = 1;
                }
                auto n = modelNormal(m, axis);
                normal.m[i * 4] = n.x;
                normal.m[i * 4 + 1] = n.y;
                normal.m[i * 4 + 2] = n.z;
            }
            upload.push_back(normal);
        }

        glBindBuffer(GL_TEXTURE_BUFFER, palette_);
        glBufferData(GL_TEXTURE_BUFFER,
                     GLsizeiptr(upload.size() * sizeof(Mat4)),
                     upload.data(),
                     GL_STREAM_DRAW);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_BUFFER, source.weightTexture);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_BUFFER, paletteTexture_);
        glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, palette_);
        glUseProgram(program_);
        glUniform1i(weightsLocation_, 0);
        glUniform1i(paletteLocation_, 1);
        glUniform1f(scaleLocation_, scale);
        glBindVertexArray(source.vao);
        glBindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER,
                          0,
                          output,
                          outputOffset,
                          GLsizeiptr(source.count * sizeof(Vertex)));
        glEnable(GL_RASTERIZER_DISCARD);
        glBeginTransformFeedback(GL_POINTS);
        glDrawArrays(GL_POINTS, 0, GLsizei(source.count));
        glEndTransformFeedback();
        glDisable(GL_RASTERIZER_DISCARD);
        glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, 0);
        glBindVertexArray(0);
        glActiveTexture(GL_TEXTURE0);
        // Conservative bounds from all palette transforms of the source AABB.
        bool first = true;
        radius = 0;
        for (const auto& m : palette) {
            for (int i = 0; i < 8; ++i) {
                auto p = modelPoint(m,
                                    {i & 1 ? source.high.x : source.low.x,
                                     i & 2 ? source.high.y : source.low.y,
                                     i & 4 ? source.high.z : source.low.z}) *
                         scale;
                if (first) {
                    low = high = p;
                    first = false;
                }
                low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
                high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
                radius = std::max(radius, std::sqrt(dot(p, p)));
            }
        }
        previous = {std::move(palette), scale, low, high, radius};
    }
};
