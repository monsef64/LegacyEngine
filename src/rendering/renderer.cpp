#include "rendering/renderer.hpp"
#include "audio/audio_gizmo.hpp"
#include "core/profiler.hpp"
#include "lighting/cascades.hpp"
#include "lighting/lighting.hpp"
#include "physics/collider_debug.hpp"
#include "rendering/shader_data.hpp"
#include "scene/camera_gizmo.hpp"
#include "scene/skybox_transition.hpp"

#include "core/components.hpp"

#include <glad/gl.h>

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>

namespace {

GLuint compileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[1024] = "";
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Shader compilation failed:\n%s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

// Returns 0 on failure (the reason is logged).
GLuint createProgram(const char* vertexSource, const char* fragmentSource) {
    const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);

    GLuint program = 0;
    if (vertexShader && fragmentShader) {
        program = glCreateProgram();
        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        glLinkProgram(program);

        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (!linked) {
            char log[1024] = "";
            glGetProgramInfoLog(program, sizeof(log), nullptr, log);
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Shader linking failed:\n%s", log);
            glDeleteProgram(program);
            program = 0;
        }
    }

    // Once linked (or failed), the program no longer needs the shader objects.
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    return program;
}

} // namespace

bool Renderer::init() {
    if (!gladLoadGL(SDL_GL_GetProcAddress)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not load the OpenGL functions");
        return false;
    }
    SDL_Log("OpenGL %s on %s",
            reinterpret_cast<const char*>(glGetString(GL_VERSION)),
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    if (!skyboxRenderer_.init() || !skyboxFadeRenderer_.init()) {
        shutdown();
        return false;
    }
    try {
        unlitProgram_ = createProgram(loadShaderData("unlit.vert").c_str(),
                                      loadShaderData("unlit.frag").c_str());
        litProgram_ =
            createProgram(loadShaderData("lit.vert").c_str(), loadShaderData("lit.frag").c_str());
    } catch (const std::exception& error) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "Shader data: %s", error.what());
        shutdown();
        return false;
    }
    if (!unlitProgram_ || !litProgram_) {
        shutdown();
        return false;
    }

    unlitModelViewProjection_ = glGetUniformLocation(unlitProgram_, "uModelViewProjection");
    unlitColor_ = glGetUniformLocation(unlitProgram_, "uColor");
    litModel_ = glGetUniformLocation(litProgram_, "uModel");
    litCameraPosition_ = glGetUniformLocation(litProgram_, "uCameraPosition");
    litCheckerboard_ = glGetUniformLocation(litProgram_, "uCheckerboard");
    litCheckerTexture_ = glGetUniformLocation(litProgram_, "uCheckerTexture");
    litSpecular_ = glGetUniformLocation(litProgram_, "uSpecular");
    litModelViewProjection_ = glGetUniformLocation(litProgram_, "uModelViewProjection");
    litNormalMatrix_ = glGetUniformLocation(litProgram_, "uNormalMatrix");
    litColor_ = glGetUniformLocation(litProgram_, "uColor");

    lightCountLocation_ = glGetUniformLocation(litProgram_, "uLightCount");
    shadowSamplerLocation_ = glGetUniformLocation(litProgram_, "uShadowTexture");
    softShadowLocation_ = glGetUniformLocation(litProgram_, "uSoftShadows");
    shadowViewLocation_ = glGetUniformLocation(litProgram_, "uShadowView");
    for (int i = 0; i < 16; ++i) {
        cascadeCountLocations_[i] =
            glGetUniformLocation(litProgram_, ("uCascadeCount[" + std::to_string(i) + "]").c_str());
        cascadeSplitLocations_[i] = glGetUniformLocation(
            litProgram_, ("uCascadeSplits[" + std::to_string(i) + "]").c_str());
    }
    const char* lightNames[]{"uLightPosition", "uLightColor", "uLightDirection", "uLightShadow"};
    for (int i = 0; i < Lighting::maxLights; ++i) {
        for (int field = 0; field < 4; ++field) {
            lightLocations_[i][field] = glGetUniformLocation(
                litProgram_,
                (std::string(lightNames[field]) + "[" + std::to_string(i) + "]").c_str());
        }
    }
    for (int i = 0; i < Lighting::maxShadowLayers; ++i) {
        shadowMatrixLocations_[i] =
            glGetUniformLocation(litProgram_, ("uShadowMatrix[" + std::to_string(i) + "]").c_str());
    }

    litTransparent_ = glGetUniformLocation(litProgram_, "uTransparent");
    litTexture_ = glGetUniformLocation(litProgram_, "uMaterialTexture");
    litNormalTexture_ = glGetUniformLocation(litProgram_, "uNormalTexture");
    litHasTexture_ = glGetUniformLocation(litProgram_, "uHasMaterialTexture");
    litBumped_ = glGetUniformLocation(litProgram_, "uBumped");
    litStrength_ = glGetUniformLocation(litProgram_, "uNormalStrength");
    litTiling_ = glGetUniformLocation(litProgram_, "uTiling");
    // Small generated texture: no external asset paths, with mipmaps to reduce
    // shimmer at distance and nearest magnification for crisp checker edges.
    const unsigned char checks[]{0, 0, 0, 255, 255, 255, 255, 255, 255, 0, 0, 0};
    glGenTextures(1, &checkerTexture_);
    glBindTexture(GL_TEXTURE_2D, checkerTexture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, checks);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glGenerateMipmap(GL_TEXTURE_2D);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    // Lines often lie exactly on a surface: the grid on the plane, for example.
    // Pushing surfaces a hair back in the depth buffer lets those lines win the
    // depth test instead of showing through in patches.
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    return true;
}

void Renderer::shutdown() {
    editorGrid_ = {};
    gridCells_ = 0;
    gridSpacing_ = 0;
    transparentDraws_.clear();
    shadowCasters_.clear();
    lights_.clear();
    skyboxRenderer_.shutdown();
    skyboxFadeRenderer_.shutdown();
    materialTextures_.clear();
    materialCache_.clear();
    for (GpuMesh& mesh : meshes_) {
        glDeleteBuffers(1, &mesh.vertexBuffer);
        glDeleteVertexArrays(1, &mesh.vertexArray);
    }
    meshes_.clear();
    glDeleteTextures(1, &shadowTexture_);
    glDeleteFramebuffers(1, &shadowFramebuffer_);
    shadowTexture_ = shadowFramebuffer_ = 0;
    glDeleteTextures(1, &checkerTexture_);
    checkerTexture_ = 0;
    glDeleteBuffers(1, &colliderVertexBuffer_);
    glDeleteVertexArrays(1, &colliderVertexArray_);
    colliderVertexBuffer_ = colliderVertexArray_ = 0;

    glDeleteProgram(litProgram_);
    glDeleteProgram(unlitProgram_);
    litProgram_ = 0;
    unlitProgram_ = 0;
}

MeshHandle Renderer::createMesh(const MeshData& data) {
    GpuMesh mesh;
    mesh.vertexCount = static_cast<int>(data.vertices.size());
    for (const auto& vertex : data.vertices) {
        mesh.radius = std::max(mesh.radius, std::sqrt(dot(vertex.position, vertex.position)));
    }
    mesh.lines = data.topology == MeshData::Topology::Lines;

    glGenVertexArrays(1, &mesh.vertexArray);
    glBindVertexArray(mesh.vertexArray);

    glGenBuffers(1, &mesh.vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(data.vertices.size() * sizeof(Vertex)),
                 data.vertices.data(),
                 GL_STATIC_DRAW);

    // Attribute 0 is the position and attribute 1 the normal, as the shaders
    // expect.
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(Vertex),
                          reinterpret_cast<const void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(Vertex),
                          reinterpret_cast<const void*>(offsetof(Vertex, normal)));

    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2,
                          2,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(Vertex),
                          reinterpret_cast<const void*>(offsetof(Vertex, u)));
    glBindVertexArray(0);

    meshes_.push_back(mesh);
    return {static_cast<std::uint32_t>(meshes_.size() - 1)};
}

void Renderer::draw(World& world,
                    const Mat4& viewProjection,
                    int width,
                    int height,
                    Vec3 cameraPosition,
                    int x,
                    int y,
                    bool editorView) {
    materialTextures_.beginFrame();
    glDisable(GL_SCISSOR_TEST);
    glViewport(x, y, width, height);
    glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    {
        ProfileScope scope(ProfilePart::Lights);
        prepareLighting(world, viewProjection, cameraPosition);
    }
    glViewport(x, y, width, height);
    const Skybox* activeSky = nullptr;
    const SkyboxTransition* skyFade = nullptr;
    world.eachReadOnly<Skybox>([&](Entity e, const Skybox& sky) {
        if (!activeSky && sky.enabled && isActive(world, e)) {
            activeSky = &sky;
            skyFade = world.has<SkyboxTransition>(e) ? world.tryGet<SkyboxTransition>(e) : nullptr;
        }
    });
    {
        ProfileScope scope(ProfilePart::Skybox);
        if (activeSky) {
            skyboxRenderer_.draw(
                skyFade ? skyFade->from : *activeSky, materialAssets_, viewProjection);
            if (skyFade) {
                glEnable(GL_BLEND);
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
                skyboxFadeRenderer_.draw(
                    skyFade->to, materialAssets_, viewProjection, skyFade->weight());
                glDisable(GL_BLEND);
            }
        }
    }
    ProfileScope surfaces(ProfilePart::Surfaces);
    glUseProgram(litProgram_);
    glUniform3f(litCameraPosition_, cameraPosition.x, cameraPosition.y, cameraPosition.z);
    glUniform1i(litCheckerTexture_, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D_ARRAY, shadowTexture_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, checkerTexture_);

    auto resolveMaterial = [&](const MeshRenderer& renderer) {
        MaterialProperties material;
        material.color = renderer.color;
        material.specular = renderer.specular;
        if (!renderer.material.empty()) {
            try {
                material = materialCache_.get(materialAssets_, renderer.material);
            } catch (const std::exception& e) {
                materialError = e.what();
            }
        }
        material =
            applyMaterialOverrides(material, renderer.materialValues, renderer.materialOverrides);
        if (!validMaterial(material)) {
            materialError = "Invalid material overrides";
            material = MaterialProperties{};
        }
        return material;
    };
    const ViewFrustum frustum(viewProjection);
    auto visibleMesh = [&](const Transform& transform, const MeshRenderer& renderer) {
        if (renderer.mesh.id >= meshes_.size()) {
            return false;
        }
        const auto& mesh = meshes_[renderer.mesh.id];
        const float bound = mesh.radius * std::max({std::abs(transform.scale.x),
                                                    std::abs(transform.scale.y),
                                                    std::abs(transform.scale.z)});
        return frustum.visible(transform.position, bound);
    };
    // All callers cull first, before resolving materials or building draw packets.
    auto drawMesh = [&](const Transform& transform,
                        const MeshRenderer& renderer,
                        const MaterialProperties& material) {
        const GpuMesh& mesh = meshes_[renderer.mesh.id];
        const Mat4 model = transformMatrix(transform.position, transform.rotation, transform.scale);
        const Mat4 modelViewProjection = multiply(viewProjection, model);

        if (mesh.lines) {
            glUseProgram(unlitProgram_);
            glUniformMatrix4fv(unlitModelViewProjection_, 1, GL_FALSE, modelViewProjection.m);
            glUniform3f(unlitColor_, renderer.color.x, renderer.color.y, renderer.color.z);
        } else {
            // A negative scale on one axis (or on all three) mirrors the
            // object, which turns the outside of its triangles from
            // counter-clockwise to clockwise.
            const Vec3& scale = transform.scale;
            const bool mirrored = scale.x * scale.y * scale.z < 0.0f;
            glFrontFace(mirrored ? GL_CW : GL_CCW);

            const Mat3 normals = normalMatrix(transform.rotation, transform.scale);
            glUseProgram(litProgram_);
            glUniformMatrix4fv(litModel_, 1, GL_FALSE, model.m);
            glUniform1i(litCheckerboard_, renderer.checkerboard ? 1 : 0);
            const auto texture =
                materialTextures_.get(materialAssets_, material.texture, materialError);
            const auto normal =
                material.shader == 1
                    ? materialTextures_.get(materialAssets_, material.normalMap, materialError)
                    : 0;
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, texture);
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, normal);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, checkerTexture_);
            glUniform1i(litTransparent_, material.shader == 2);
            glUniform1i(litTexture_, 2);
            glUniform1i(litNormalTexture_, 3);
            glUniform1i(litHasTexture_, texture != 0);
            glUniform1i(litBumped_, normal != 0);
            glUniform1f(litStrength_, material.normalStrength);
            glUniform2f(litTiling_, material.tilingX, material.tilingY);
            glUniform1f(litSpecular_, material.specular);
            glUniformMatrix4fv(litModelViewProjection_, 1, GL_FALSE, modelViewProjection.m);
            glUniformMatrix3fv(litNormalMatrix_, 1, GL_FALSE, normals.m);
            glUniform3f(litColor_, material.color.x, material.color.y, material.color.z);
        }

        glBindVertexArray(mesh.vertexArray);
        glDrawArrays(mesh.lines ? GL_LINES : GL_TRIANGLES, 0, mesh.vertexCount);
    };
    auto& transparent = transparentDraws_;
    transparent.clear();
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    world.eachReadOnly<Transform, MeshRenderer>(
        [&](Entity e, const Transform& t, const MeshRenderer& r) {
            if (!isActive(world, e) || world.has<Camera>(e) || !visibleMesh(t, r)) {
                return;
            }
            auto material = resolveMaterial(r);
            if (material.shader == 2) {
                auto delta = t.position - cameraPosition;
                transparent.push_back(
                    {t, &r, std::move(material), dot(delta, delta), transparent.size()});
            } else {
                drawMesh(t, r, material);
            }
        });
    // Explicit insertion order preserves equal-depth behavior without stable_sort's temporary
    // buffer.
    std::sort(transparent.begin(), transparent.end(), [](const auto& a, const auto& b) {
        return a.distance == b.distance ? a.order < b.order : a.distance > b.distance;
    });
    if (!transparent.empty()) {
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        glDepthMask(GL_FALSE);
        for (const auto& item : transparent) {
            drawMesh(item.transform, *item.renderer, item.material);
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
    transparent.clear(); // Do not retain ECS references beyond this view.
    if (editorView) {
        if (audioModel_.id == UINT32_MAX) {
            audioModel_ = createMesh(audioSourceGlyph());
        }
        world.eachReadOnly<AudioSource, Transform>(
            [&](Entity, const AudioSource&, const Transform& t) {
                Transform glyph = t;
                glyph.scale = {1, 1, 1};
                MeshRenderer icon{audioModel_, {.35f, .8f, 1.0f}, false, 0};
                if (visibleMesh(glyph, icon)) {
                    drawMesh(glyph, icon, resolveMaterial(icon));
                }
            });
    }
}

void Renderer::drawCollider(const Transform& transform,
                            const Collider& collider,
                            const Mat4& viewProjection,
                            float opacity) {
    drawWire(colliderWireframe(collider, transform.scale),
             transform,
             viewProjection,
             {0, 1, 0},
             opacity);
}
void Renderer::drawCamera(const Transform& transform,
                          const Camera& camera,
                          const Mat4& viewProjection) {
    drawWire(cameraFrustumWire(camera), transform, viewProjection, {.92f, .94f, .98f});
}
void Renderer::drawRaycast(const Transform& transform,
                           const Raycast& query,
                           const Mat4& viewProjection) {
    MeshData wire;
    wire.topology = MeshData::Topology::Lines;
    auto line = [&](Vec3 a, Vec3 b) {
        wire.vertices.push_back({a + query.offset, {}});
        wire.vertices.push_back({b + query.offset, {}});
    };
    if (query.type == 2) {
        for (int axis = 0; axis < 3; ++axis) {
            for (int i = 0; i < 24; ++i) {
                auto point = [&](int j) {
                    float a = 2 * kPi * j / 24, u = query.radius * std::cos(a),
                          v = query.radius * std::sin(a);
                    return axis == 0 ? Vec3{0, u, v} : axis == 1 ? Vec3{u, 0, v} : Vec3{u, v, 0};
                };
                line(point(i), point(i + 1));
            }
        }
    } else {
        int count = query.type == 1 ? std::clamp(query.samples, 2, 181) : 1;
        Vec3 previous;
        for (int i = 0; i < count; ++i) {
            float a = count == 1 ? 0 : radians(-query.angle * .5f + query.angle * i / (count - 1));
            Vec3 end{query.distance * std::sin(a), 0, -query.distance * std::cos(a)};
            line({}, end);
            if (i) {
                line(previous, end);
            }
            previous = end;
        }
    }
    drawWire(wire, transform, viewProjection, {1, .75f, .2f});
}
void Renderer::drawAudioSource(const Transform& transform,
                               const AudioSource& source,
                               const Mat4& viewProjection) {
    drawWire(audioSourceRange(source), transform, viewProjection, {.35f, .8f, 1.0f});
}
void Renderer::drawWire(const MeshData& wire,
                        const Transform& transform,
                        const Mat4& viewProjection,
                        Vec3 color,
                        float opacity) {
    if (wire.vertices.empty()) {
        return;
    }
    if (!colliderVertexArray_) {
        glGenVertexArrays(1, &colliderVertexArray_);
        glGenBuffers(1, &colliderVertexBuffer_);
        glBindVertexArray(colliderVertexArray_);
        glBindBuffer(GL_ARRAY_BUFFER, colliderVertexBuffer_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0,
                              3,
                              GL_FLOAT,
                              GL_FALSE,
                              sizeof(Vertex),
                              reinterpret_cast<const void*>(offsetof(Vertex, position)));
    }
    glBindVertexArray(colliderVertexArray_);
    glBindBuffer(GL_ARRAY_BUFFER, colliderVertexBuffer_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(wire.vertices.size() * sizeof(Vertex)),
                 wire.vertices.data(),
                 GL_STREAM_DRAW);
    const Mat4 mvp = multiply(viewProjection,
                              transformMatrix(transform.position, transform.rotation, {1, 1, 1}));
    glUseProgram(unlitProgram_);
    glUniformMatrix4fv(unlitModelViewProjection_, 1, GL_FALSE, mvp.m);
    glUniform3f(unlitColor_, color.x, color.y, color.z);
    // Show the entire collider, including the back and the plane's hidden slab.
    const bool depthTest = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);
    const bool blend = glIsEnabled(GL_BLEND);
    if (opacity < 1) {
        glEnable(GL_BLEND);
        glBlendColor(0, 0, 0, opacity);
        glBlendFuncSeparate(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA, GL_ZERO, GL_ONE);
    }
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(wire.vertices.size()));
    if (opacity < 1) {
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        if (!blend) {
            glDisable(GL_BLEND);
        }
    }
    if (depthTest) {
        glEnable(GL_DEPTH_TEST);
    }
    glBindVertexArray(0);
}

void Renderer::prepareLighting(World& world, const Mat4& viewProjection, Vec3 cameraPosition) {
    auto& lights = lights_;
    Lighting::gather(world, viewProjection, cameraPosition, lights, shadowSettings_.mode == 0);
    std::array<Lighting::Cascades, Lighting::maxLights> cascades{};
    Vec3 shadowOrigin{}, shadowForward{0, 0, -1};
    for (std::size_t i = 0; i < lights.size(); ++i) {
        if (lights[i].type == 0 && lights[i].shadowCount == 4 && lights[i].shadowLayer >= 0) {
            try {
                cascades[i] = Lighting::buildCascades(
                    viewProjection,
                    cameraPosition,
                    lights[i].direction,
                    lights[i].settings.shadowDistance * 2,
                    shadowSettings_.lightResolution(lights[i].settings.shadowQuality));
                shadowOrigin = cascades[i].origin;
                shadowForward = cascades[i].forward;
            } catch (const std::exception& e) {
                SDL_LogWarn(
                    SDL_LOG_CATEGORY_RENDER, "CSM disabled for invalid frustum: %s", e.what());
                lights[i].shadowLayer = -1;
            }
        }
    }
    GLint framebuffer = 0, readFramebuffer = 0, frontFace = GL_CCW;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &framebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
    glGetIntegerv(GL_FRONT_FACE, &frontFace);
    bool needsShadows = false;
    for (const auto& l : lights) {
        needsShadows |= l.shadowLayer >= 0;
    }
    if (needsShadows && shadowAtlasResolution_ != shadowSettings_.resolution) {
        if (shadowTexture_) {
            glDeleteTextures(1, &shadowTexture_);
        }
        if (shadowFramebuffer_) {
            glDeleteFramebuffers(1, &shadowFramebuffer_);
        }
        shadowTexture_ = shadowFramebuffer_ = 0;
        shadowAtlasResolution_ = shadowSettings_.resolution;
    }
    if (needsShadows && !shadowTexture_) {
        glGenTextures(1, &shadowTexture_);
        glBindTexture(GL_TEXTURE_2D_ARRAY, shadowTexture_);
        glTexImage3D(GL_TEXTURE_2D_ARRAY,
                     0,
                     GL_DEPTH_COMPONENT24,
                     shadowAtlasResolution_,
                     shadowAtlasResolution_,
                     Lighting::maxShadowLayers,
                     0,
                     GL_DEPTH_COMPONENT,
                     GL_FLOAT,
                     nullptr);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        const float border[]{1, 1, 1, 1};
        glTexParameterfv(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_BORDER_COLOR, border);
        glGenFramebuffers(1, &shadowFramebuffer_);
    }
    auto& casters = shadowCasters_;
    casters.clear();
    if (needsShadows) {
        world.eachReadOnly<Transform, MeshRenderer>(
            [&](Entity e, const Transform& t, const MeshRenderer& r) {
                if (!isActive(world, e) || world.has<Camera>(e) || r.mesh.id >= meshes_.size()) {
                    return;
                }
                const auto& mesh = meshes_[r.mesh.id];
                if (mesh.lines) {
                    return;
                }
                MaterialProperties material;
                if (!r.material.empty()) {
                    try {
                        material = materialCache_.get(materialAssets_, r.material);
                    } catch (const std::exception& e) {
                        materialError = e.what();
                    }
                }
                material = applyMaterialOverrides(material, r.materialValues, r.materialOverrides);
                if (material.shader == 2) {
                    return; // alpha-blended surfaces do not cast opaque shadow silhouettes
                }

                casters.push_back(
                    {&mesh,
                     transformMatrix(t.position, t.rotation, t.scale),
                     t.position,
                     mesh.radius *
                         std::max({std::abs(t.scale.x), std::abs(t.scale.y), std::abs(t.scale.z)}),
                     t.scale.x * t.scale.y * t.scale.z < 0});
            });
    }
    std::array<Mat4, Lighting::maxShadowLayers> matrices{};
    if (needsShadows) {
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFramebuffer_);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadowTexture_, 0, 0);
        if (glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            SDL_LogError(SDL_LOG_CATEGORY_RENDER,
                         "Shadow framebuffer incomplete; rendering lights without shadows");
            for (auto& l : lights) {
                l.shadowLayer = -1;
            }
        }
        glUseProgram(unlitProgram_);
        glViewport(0, 0, shadowAtlasResolution_, shadowAtlasResolution_);
        glPolygonOffset(2, 4);
        for (std::size_t lightIndex = 0; lightIndex < lights.size(); ++lightIndex) {
            const auto& light = lights[lightIndex];
            if (light.shadowLayer >= 0) {
                for (int face = 0; face < light.shadowCount; ++face) {
                    const int resolution =
                        shadowSettings_.lightResolution(light.settings.shadowQuality);
                    glViewport(0, 0, resolution, resolution);
                    const int layer = light.shadowLayer + face;
                    const Mat4 vp =
                        light.type == 0 && light.shadowCount == 4
                            ? cascades[lightIndex].matrices[face]
                            : Lighting::shadowMatrix(light, cameraPosition, face, resolution);
                    matrices[layer] = vp;
                    glFramebufferTextureLayer(
                        GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, shadowTexture_, 0, layer);
                    glClear(GL_DEPTH_BUFFER_BIT);
                    const ViewFrustum shadowFrustum(vp);
                    for (const auto& caster : casters) {
                        if (!shadowFrustum.visible(caster.position, caster.radius)) {
                            continue;
                        }
                        const Mat4 mvp = multiply(vp, caster.model);
                        glUniformMatrix4fv(unlitModelViewProjection_, 1, GL_FALSE, mvp.m);
                        glFrontFace(caster.mirrored ? GL_CW : GL_CCW);
                        glBindVertexArray(caster.mesh->vertexArray);
                        glDrawArrays(GL_TRIANGLES, 0, caster.mesh->vertexCount);
                    }
                }
            }
        }
        glPolygonOffset(1, 1);
        glFrontFace(frontFace);
        glBindVertexArray(0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
    }
    casters.clear();
    glUseProgram(litProgram_);
    glUniform1i(lightCountLocation_, static_cast<int>(lights.size()));
    glUniform1i(shadowSamplerLocation_, 1);
    glUniform1i(softShadowLocation_, shadowSettings_.soft);
    if (shadowTexture_) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D_ARRAY, shadowTexture_);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,
                        GL_TEXTURE_MIN_FILTER,
                        shadowSettings_.soft ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY,
                        GL_TEXTURE_MAG_FILTER,
                        shadowSettings_.soft ? GL_LINEAR : GL_NEAREST);
        glActiveTexture(GL_TEXTURE0);
    }
    glUniform4f(shadowViewLocation_,
                shadowForward.x,
                shadowForward.y,
                shadowForward.z,
                -dot(shadowOrigin, shadowForward));
    for (std::size_t i = 0; i < lights.size(); ++i) {
        const auto& l = lights[i];
        const auto& s = l.settings;
        const bool csm = l.type == 0 && l.shadowCount == 4 && l.shadowLayer >= 0;
        glUniform1i(cascadeCountLocations_[i], csm ? 4 : 1);
        const auto& splits = cascades[i].splits;
        glUniform4f(cascadeSplitLocations_[i], splits[0], splits[1], splits[2], splits[3]);
        glUniform4f(lightLocations_[i][0], l.position.x, l.position.y, l.position.z, float(l.type));
        glUniform4f(lightLocations_[i][1],
                    s.color.x * s.intensity,
                    s.color.y * s.intensity,
                    s.color.z * s.intensity,
                    s.range);
        glUniform4f(lightLocations_[i][2],
                    l.direction.x,
                    l.direction.y,
                    l.direction.z,
                    std::cos(s.outerAngle * kPi / 180));
        glUniform4f(lightLocations_[i][3],
                    float(l.shadowLayer),
                    s.shadowBias,
                    std::cos(s.innerAngle * kPi / 180),
                    float(shadowSettings_.lightResolution(s.shadowQuality)) /
                        shadowAtlasResolution_);
    }
    for (int i = 0; i < Lighting::maxShadowLayers; ++i) {
        glUniformMatrix4fv(shadowMatrixLocations_[i], 1, GL_FALSE, matrices[i].m);
    }
}

void Renderer::drawEditorGrid(const GridSettings& settings, const Mat4& vp) {
    if (!settings.visible || !settings.valid()) {
        return;
    }
    if (gridCells_ != settings.cells || gridSpacing_ != settings.spacing) {
        auto data = makeGrid(settings.cells, settings.spacing);
        if (editorGrid_.id == UINT32_MAX) {
            editorGrid_ = createMesh(data);
        } else {
            auto& mesh = meshes_[editorGrid_.id];
            glBindBuffer(GL_ARRAY_BUFFER, mesh.vertexBuffer);
            glBufferData(GL_ARRAY_BUFFER,
                         data.vertices.size() * sizeof(Vertex),
                         data.vertices.data(),
                         GL_STATIC_DRAW);
            mesh.vertexCount = int(data.vertices.size());
        }
        gridCells_ = settings.cells;
        gridSpacing_ = settings.spacing;
    }
    Quat rotation{};
    if (settings.plane == 1) {
        rotation = axisAngle({1, 0, 0}, kPi * .5f);
    } else if (settings.plane == 2) {
        rotation = axisAngle({0, 0, 1}, kPi * .5f);
    }
    const auto mvp = multiply(vp, transformMatrix(settings.position, rotation, {1, 1, 1}));
    glUseProgram(unlitProgram_);
    glUniformMatrix4fv(unlitModelViewProjection_, 1, GL_FALSE, mvp.m);
    glUniform3f(unlitColor_, settings.color.x, settings.color.y, settings.color.z);
    glDepthMask(GL_FALSE);
    glBindVertexArray(meshes_[editorGrid_.id].vertexArray);
    glDrawArrays(GL_LINES, 0, meshes_[editorGrid_.id].vertexCount);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
}

void Renderer::updateSkyboxFades(World& world, float dt) {
    world.each<SkyboxTransition>([&](Entity entity, const SkyboxTransition& fade) {
        if (!skyboxFadeRenderer_.prepare(fade.to, materialAssets_)) {
            world.remove<SkyboxTransition>(entity);
        }
    });
    updateSkyboxTransitions(world, dt);
}
