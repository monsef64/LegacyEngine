#include "collider_debug.hpp"
#include "renderer.hpp"

#include "components.hpp"

#include <glad/gl.h>

#include <SDL3/SDL.h>

#include <cstddef>

namespace {

const char* const kUnlitVertexShader = R"glsl(#version 330 core
layout(location = 0) in vec3 aPosition;

uniform mat4 uModelViewProjection;

void main()
{
    gl_Position = uModelViewProjection * vec4(aPosition, 1.0);
}
)glsl";

const char* const kUnlitFragmentShader = R"glsl(#version 330 core
uniform vec3 uColor;

out vec4 fragColor;

void main()
{
    fragColor = vec4(uColor, 1.0);
}
)glsl";

const char* const kLitVertexShader = R"glsl(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

uniform mat4 uModelViewProjection;
uniform mat3 uNormalMatrix;
uniform mat4 uModel;

out vec3 vNormal;
out vec3 vWorldPosition;
out vec2 vCheckerUV;

void main()
{
    vNormal = uNormalMatrix * aNormal;
    vWorldPosition = (uModel * vec4(aPosition, 1.0)).xyz;
    // A 2x2 repeat covers 2 metres: each texel is one checker square.
    vCheckerUV = aPosition.xz * 0.5;
    gl_Position = uModelViewProjection * vec4(aPosition, 1.0);
}
)glsl";

const char* const kLitFragmentShader = R"glsl(#version 330 core
in vec3 vNormal;
in vec3 vWorldPosition;
in vec2 vCheckerUV;

uniform bool uCheckerboard;
uniform sampler2D uCheckerTexture;
uniform float uSpecular;
uniform vec3 uCameraPosition;
uniform vec3 uColor;
uniform vec3 uLightDirection; // unit vector pointing towards the light

out vec4 fragColor;

void main()
{
    vec3 normal = normalize(vNormal);
    float diffuse = max(dot(normal, uLightDirection), 0.0);
    vec3 albedo = uColor;
    if (uCheckerboard) albedo *= texture(uCheckerTexture, vCheckerUV).rgb;
    vec3 viewDirection = normalize(uCameraPosition - vWorldPosition);
    vec3 halfway = normalize(uLightDirection + viewDirection);
    float highlight = diffuse > 0.0 ? pow(max(dot(normal, halfway), 0.0), 64.0) : 0.0;
    fragColor = vec4(albedo * (0.35 + 0.65 * diffuse) + vec3(uSpecular * highlight), 1.0);
}
)glsl";

GLuint compileShader(GLenum type, const char* source)
{
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
GLuint createProgram(const char* vertexSource, const char* fragmentSource)
{
    const GLuint vertexShader   = compileShader(GL_VERTEX_SHADER, vertexSource);
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

bool Renderer::init()
{
    if (!gladLoadGL(SDL_GL_GetProcAddress)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not load the OpenGL functions");
        return false;
    }
    SDL_Log("OpenGL %s on %s", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    unlitProgram_ = createProgram(kUnlitVertexShader, kUnlitFragmentShader);
    litProgram_   = createProgram(kLitVertexShader, kLitFragmentShader);
    if (!unlitProgram_ || !litProgram_) {
        shutdown();
        return false;
    }

    unlitModelViewProjection_ = glGetUniformLocation(unlitProgram_, "uModelViewProjection");
    unlitColor_               = glGetUniformLocation(unlitProgram_, "uColor");
    litModel_ = glGetUniformLocation(litProgram_, "uModel");
    litCameraPosition_ = glGetUniformLocation(litProgram_, "uCameraPosition");
    litCheckerboard_ = glGetUniformLocation(litProgram_, "uCheckerboard");
    litCheckerTexture_ = glGetUniformLocation(litProgram_, "uCheckerTexture");
    litSpecular_ = glGetUniformLocation(litProgram_, "uSpecular");
    litModelViewProjection_   = glGetUniformLocation(litProgram_, "uModelViewProjection");
    litNormalMatrix_          = glGetUniformLocation(litProgram_, "uNormalMatrix");
    litColor_                 = glGetUniformLocation(litProgram_, "uColor");
    litLightDirection_        = glGetUniformLocation(litProgram_, "uLightDirection");

    // Small generated texture: no external asset paths, with mipmaps to reduce
    // shimmer at distance and nearest magnification for crisp checker edges.
    const unsigned char checks[]{0,0,0,255,255,255,255,255,255,0,0,0};
    glGenTextures(1, &checkerTexture_);
    glBindTexture(GL_TEXTURE_2D, checkerTexture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGB8,2,2,0,GL_RGB,GL_UNSIGNED_BYTE,checks);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
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

void Renderer::shutdown()
{
    for (GpuMesh& mesh : meshes_) {
        glDeleteBuffers(1, &mesh.vertexBuffer);
        glDeleteVertexArrays(1, &mesh.vertexArray);
    }
    meshes_.clear();
    glDeleteTextures(1, &checkerTexture_);
    checkerTexture_ = 0;
    glDeleteBuffers(1, &colliderVertexBuffer_);
    glDeleteVertexArrays(1, &colliderVertexArray_);
    colliderVertexBuffer_ = colliderVertexArray_ = 0;

    glDeleteProgram(litProgram_);
    glDeleteProgram(unlitProgram_);
    litProgram_   = 0;
    unlitProgram_ = 0;
}

MeshHandle Renderer::createMesh(const MeshData& data)
{
    GpuMesh mesh;
    mesh.vertexCount = static_cast<int>(data.vertices.size());
    mesh.lines       = data.topology == MeshData::Topology::Lines;

    glGenVertexArrays(1, &mesh.vertexArray);
    glBindVertexArray(mesh.vertexArray);

    glGenBuffers(1, &mesh.vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(data.vertices.size() * sizeof(Vertex)),
                 data.vertices.data(), GL_STATIC_DRAW);

    // Attribute 0 is the position and attribute 1 the normal, as the shaders
    // expect.
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<const void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          reinterpret_cast<const void*>(offsetof(Vertex, normal)));

    glBindVertexArray(0);

    meshes_.push_back(mesh);
    return {static_cast<std::uint32_t>(meshes_.size() - 1)};
}

void Renderer::draw(World& world, const Mat4& viewProjection, int width, int height, Vec3 cameraPosition, int x, int y, bool editorView)
{
    glDisable(GL_SCISSOR_TEST);
    glViewport(x, y, width, height);
    glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Light from the far side places a visible floor highlight in the starting view.
    const Vec3 lightDirection = normalize(Vec3{-0.4f, 1.0f, -0.6f});
    glUseProgram(litProgram_);
    glUniform3f(litLightDirection_, lightDirection.x, lightDirection.y, lightDirection.z);
    glUniform3f(litCameraPosition_, cameraPosition.x, cameraPosition.y, cameraPosition.z);
    glUniform1i(litCheckerTexture_, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, checkerTexture_);

    auto drawMesh=[&](const Transform& transform,const MeshRenderer& renderer) {
            if (renderer.mesh.id >= meshes_.size()) {
                return; // no mesh to draw
            }
            const GpuMesh& mesh = meshes_[renderer.mesh.id];

            const Mat4 model =
                transformMatrix(transform.position, transform.rotation, transform.scale);
            const Mat4 modelViewProjection = multiply(viewProjection, model);

            if (mesh.lines) {
                glUseProgram(unlitProgram_);
                glUniformMatrix4fv(unlitModelViewProjection_, 1, GL_FALSE, modelViewProjection.m);
                glUniform3f(unlitColor_, renderer.color.x, renderer.color.y, renderer.color.z);
            } else {
                // A negative scale on one axis (or on all three) mirrors the
                // object, which turns the outside of its triangles from
                // counter-clockwise to clockwise.
                const Vec3& scale    = transform.scale;
                const bool  mirrored = scale.x * scale.y * scale.z < 0.0f;
                glFrontFace(mirrored ? GL_CW : GL_CCW);

                const Mat3 normals = normalMatrix(transform.rotation, transform.scale);
                glUseProgram(litProgram_);
                glUniformMatrix4fv(litModel_, 1, GL_FALSE, model.m);
                glUniform1i(litCheckerboard_, renderer.checkerboard ? 1 : 0);
                glUniform1f(litSpecular_, renderer.specular);
                glUniformMatrix4fv(litModelViewProjection_, 1, GL_FALSE, modelViewProjection.m);
                glUniformMatrix3fv(litNormalMatrix_, 1, GL_FALSE, normals.m);
                glUniform3f(litColor_, renderer.color.x, renderer.color.y, renderer.color.z);
            }

            glBindVertexArray(mesh.vertexArray);
            glDrawArrays(mesh.lines ? GL_LINES : GL_TRIANGLES, 0, mesh.vertexCount);
        };
    world.each<Transform, MeshRenderer>([&](Entity e,const Transform& t,const MeshRenderer& r){
        if(!world.has<Camera>(e))drawMesh(t,r);
    });
    if(editorView) {
        if(cameraModel_.id==UINT32_MAX)cameraModel_=createMesh(makeCameraModel());
        world.each<Camera,Transform>([&](Entity,const Camera&,const Transform& t){
            drawMesh(t,MeshRenderer{cameraModel_,{.28f,.50f,.72f},false,.4f});
        });
    }
}

void Renderer::drawCollider(const Transform& transform, const Collider& collider, const Mat4& viewProjection)
{
    const MeshData wire = colliderWireframe(collider, transform.scale);
    if (!colliderVertexArray_) {
        glGenVertexArrays(1, &colliderVertexArray_);
        glGenBuffers(1, &colliderVertexBuffer_);
        glBindVertexArray(colliderVertexArray_);
        glBindBuffer(GL_ARRAY_BUFFER, colliderVertexBuffer_);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                              reinterpret_cast<const void*>(offsetof(Vertex, position)));
    }
    glBindVertexArray(colliderVertexArray_);
    glBindBuffer(GL_ARRAY_BUFFER, colliderVertexBuffer_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(wire.vertices.size() * sizeof(Vertex)),
                 wire.vertices.data(), GL_STREAM_DRAW);
    const Mat4 mvp = multiply(viewProjection, transformMatrix(transform.position, transform.rotation, {1, 1, 1}));
    glUseProgram(unlitProgram_);
    glUniformMatrix4fv(unlitModelViewProjection_, 1, GL_FALSE, mvp.m);
    glUniform3f(unlitColor_, 0.0f, 1.0f, 0.0f);
    // Show the entire collider, including the back and the plane's hidden slab.
    const bool depthTest = glIsEnabled(GL_DEPTH_TEST);
    glDisable(GL_DEPTH_TEST);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(wire.vertices.size()));
    if (depthTest) glEnable(GL_DEPTH_TEST);
    glBindVertexArray(0);
}
