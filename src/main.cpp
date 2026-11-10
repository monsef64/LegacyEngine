// LegacyEngine - first scene: a 7 x 7 m ground grid with a 1 m box in the middle.
//
// World conventions:
//   * 1 unit = 1 metre
//   * +Y is up, the ground is the XZ plane (y = 0)
//   * right-handed coordinates (the OpenGL convention)

#include <glad/gl.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// Scene description
// ---------------------------------------------------------------------------

constexpr int   kGridCells = 7;    // grid blocks per side
constexpr float kCellSize  = 1.0f; // metres per grid block
constexpr float kBoxSize   = 1.0f; // metres per box edge

// ---------------------------------------------------------------------------
// Minimal math
// ---------------------------------------------------------------------------

constexpr float kPi = 3.14159265358979f;
constexpr float radians(float degrees) { return degrees * kPi / 180.0f; }

struct Vec3 {
    float x, y, z;
};

static Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static Vec3 operator*(Vec3 v, float s) { return {v.x * s, v.y * s, v.z * s}; }
static float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static Vec3 normalize(Vec3 v) { return v * (1.0f / std::sqrt(dot(v, v))); }

// 4x4 matrix stored column by column, which is the layout OpenGL expects:
// element (row, column) lives at m[column * 4 + row].
struct Mat4 {
    float m[16];
};

static Mat4 multiply(const Mat4& a, const Mat4& b) // a * b
{
    Mat4 result{};
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            for (int k = 0; k < 4; ++k) {
                result.m[column * 4 + row] += a.m[k * 4 + row] * b.m[column * 4 + k];
            }
        }
    }
    return result;
}

static Mat4 translation(Vec3 offset)
{
    Mat4 result{};
    result.m[0] = result.m[5] = result.m[10] = result.m[15] = 1.0f;
    result.m[12] = offset.x;
    result.m[13] = offset.y;
    result.m[14] = offset.z;
    return result;
}

static Mat4 perspective(float verticalFov, float aspect, float zNear, float zFar)
{
    const float f = 1.0f / std::tan(verticalFov * 0.5f);
    Mat4 result{};
    result.m[0]  = f / aspect;
    result.m[5]  = f;
    result.m[10] = (zFar + zNear) / (zNear - zFar);
    result.m[11] = -1.0f;
    result.m[14] = (2.0f * zFar * zNear) / (zNear - zFar);
    return result;
}

static Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up)
{
    const Vec3 forward = normalize(target - eye);
    const Vec3 right   = normalize(cross(forward, up));
    const Vec3 trueUp  = cross(right, forward);

    Mat4 result{};
    result.m[0]  = right.x;
    result.m[4]  = right.y;
    result.m[8]  = right.z;
    result.m[12] = -dot(right, eye);
    result.m[1]  = trueUp.x;
    result.m[5]  = trueUp.y;
    result.m[9]  = trueUp.z;
    result.m[13] = -dot(trueUp, eye);
    result.m[2]  = -forward.x;
    result.m[6]  = -forward.y;
    result.m[10] = -forward.z;
    result.m[14] = dot(forward, eye);
    result.m[15] = 1.0f;
    return result;
}

// ---------------------------------------------------------------------------
// Camera: orbits around a target point
// ---------------------------------------------------------------------------

struct OrbitCamera {
    Vec3  target   = {0.0f, kBoxSize * 0.5f, 0.0f}; // centre of the box
    float yaw      = radians(35.0f);                // rotation around +Y
    float pitch    = radians(35.0f);                // height above the horizon
    float distance = 13.0f;                         // metres from the target

    Vec3 position() const
    {
        const Vec3 direction = {std::cos(pitch) * std::sin(yaw), std::sin(pitch),
                                std::cos(pitch) * std::cos(yaw)};
        return target + direction * distance;
    }
};

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

// Lines of a square grid lying on the ground, centred on the origin.
// Each vertex is a position (x, y, z); every two vertices form one line.
static std::vector<float> buildGrid(int cells, float cellSize)
{
    const float half = static_cast<float>(cells) * cellSize * 0.5f;

    std::vector<float> vertices;
    for (int i = 0; i <= cells; ++i) {
        const float p = -half + static_cast<float>(i) * cellSize;
        vertices.insert(vertices.end(), {p, 0.0f, -half, p, 0.0f, half}); // along Z
        vertices.insert(vertices.end(), {-half, 0.0f, p, half, 0.0f, p}); // along X
    }
    return vertices;
}

// A box centred on its own origin. Each vertex is a position (x, y, z) followed
// by the normal of its face; every three vertices form one triangle, wound
// counter-clockwise when seen from outside the box.
static std::vector<float> buildBox(float size)
{
    struct Face {
        Vec3 normal, right, up; // right x up = normal
    };
    const Face faces[6] = {
        {{1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}},  // +X
        {{-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},  // -X
        {{0.0f, 1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}},  // +Y
        {{0.0f, -1.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}},  // -Y
        {{0.0f, 0.0f, 1.0f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},   // +Z
        {{0.0f, 0.0f, -1.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}}, // -Z
    };

    const float half = size * 0.5f;

    std::vector<float> vertices;
    for (const Face& face : faces) {
        const Vec3 corners[4] = {
            (face.normal - face.right - face.up) * half,
            (face.normal + face.right - face.up) * half,
            (face.normal + face.right + face.up) * half,
            (face.normal - face.right + face.up) * half,
        };
        for (int index : {0, 1, 2, 0, 2, 3}) {
            const Vec3 p = corners[index];
            vertices.insert(vertices.end(),
                            {p.x, p.y, p.z, face.normal.x, face.normal.y, face.normal.z});
        }
    }
    return vertices;
}

struct Mesh {
    GLuint  vertexArray  = 0;
    GLuint  vertexBuffer = 0;
    GLsizei vertexCount  = 0;
};

// Uploads vertices to the GPU. Attribute 0 is the position; when hasNormals is
// set, attribute 1 is the normal that follows each position.
static Mesh createMesh(const std::vector<float>& vertices, bool hasNormals)
{
    const GLsizei floatsPerVertex = hasNormals ? 6 : 3;
    const GLsizei stride = floatsPerVertex * static_cast<GLsizei>(sizeof(float));

    Mesh mesh;
    mesh.vertexCount = static_cast<GLsizei>(vertices.size()) / floatsPerVertex;

    glGenVertexArrays(1, &mesh.vertexArray);
    glBindVertexArray(mesh.vertexArray);

    glGenBuffers(1, &mesh.vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, mesh.vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
                 vertices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, nullptr);
    if (hasNormals) {
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(3 * sizeof(float)));
    }

    glBindVertexArray(0);
    return mesh;
}

static void destroyMesh(Mesh& mesh)
{
    glDeleteBuffers(1, &mesh.vertexBuffer);
    glDeleteVertexArrays(1, &mesh.vertexArray);
    mesh = {};
}

// ---------------------------------------------------------------------------
// Shaders
// ---------------------------------------------------------------------------

// Flat colour, used for the grid lines.
static const char* const kUnlitVertexShader = R"glsl(#version 330 core
layout(location = 0) in vec3 aPosition;

uniform mat4 uViewProjection;

void main()
{
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
)glsl";

static const char* const kUnlitFragmentShader = R"glsl(#version 330 core
uniform vec3 uColor;

out vec4 fragColor;

void main()
{
    fragColor = vec4(uColor, 1.0);
}
)glsl";

// Colour shaded by a single directional light, used for the box.
static const char* const kLitVertexShader = R"glsl(#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

uniform mat4 uViewProjection;
uniform mat4 uModel;

out vec3 vNormal;

void main()
{
    vNormal = mat3(uModel) * aNormal;
    gl_Position = uViewProjection * uModel * vec4(aPosition, 1.0);
}
)glsl";

static const char* const kLitFragmentShader = R"glsl(#version 330 core
in vec3 vNormal;

uniform vec3 uColor;
uniform vec3 uLightDirection; // unit vector pointing towards the light

out vec4 fragColor;

void main()
{
    float diffuse = max(dot(normalize(vNormal), uLightDirection), 0.0);
    fragColor = vec4(uColor * (0.35 + 0.65 * diffuse), 1.0);
}
)glsl";

static GLuint compileShader(GLenum type, const char* source)
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
static GLuint createProgram(const char* vertexSource, const char* fragmentSource)
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

// ---------------------------------------------------------------------------
// Window
// ---------------------------------------------------------------------------

// Creates the window together with an OpenGL 3.3 core context. On failure both
// outputs are null and SDL_GetError() describes the problem.
static void createWindow(int multisamples, SDL_Window*& window, SDL_GLContext& context)
{
    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, multisamples > 0 ? 1 : 0);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, multisamples);

    window = SDL_CreateWindow("LegacyEngine", 1280, 720,
                              SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                                  SDL_WINDOW_HIGH_PIXEL_DENSITY);
    context = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!context && window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
}

int main(int, char**)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not start SDL: %s", SDL_GetError());
        return 1;
    }

    // Prefer 4x anti-aliasing so the grid lines look smooth, but not every
    // driver offers it: fall back to a plain framebuffer.
    SDL_Window*   window  = nullptr;
    SDL_GLContext context = nullptr;
    createWindow(4, window, context);
    if (!context) {
        createWindow(0, window, context);
    }
    if (!context) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Could not create an OpenGL 3.3 window: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    if (!gladLoadGL(SDL_GL_GetProcAddress)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not load the OpenGL functions");
        SDL_GL_DestroyContext(context);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_Log("OpenGL %s on %s", reinterpret_cast<const char*>(glGetString(GL_VERSION)),
            reinterpret_cast<const char*>(glGetString(GL_RENDERER)));

    SDL_GL_SetSwapInterval(1); // vsync, where the driver allows it

    const GLuint unlitProgram = createProgram(kUnlitVertexShader, kUnlitFragmentShader);
    const GLuint litProgram   = createProgram(kLitVertexShader, kLitFragmentShader);
    Mesh grid = createMesh(buildGrid(kGridCells, kCellSize), false);
    Mesh box  = createMesh(buildBox(kBoxSize), true);

    const GLint unlitViewProjection = glGetUniformLocation(unlitProgram, "uViewProjection");
    const GLint unlitColor          = glGetUniformLocation(unlitProgram, "uColor");
    const GLint litViewProjection   = glGetUniformLocation(litProgram, "uViewProjection");
    const GLint litModel            = glGetUniformLocation(litProgram, "uModel");
    const GLint litColor            = glGetUniformLocation(litProgram, "uColor");
    const GLint litLightDirection   = glGetUniformLocation(litProgram, "uLightDirection");

    // The box mesh is centred on its own origin, so lift it by half its height
    // to make it rest on the ground in the middle of the grid.
    const Mat4 boxTransform   = translation({0.0f, kBoxSize * 0.5f, 0.0f});
    const Vec3 lightDirection = normalize({0.4f, 1.0f, 0.6f});

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    // The grid lines around the centre cell run exactly along the bottom edges
    // of the box. Pushing surfaces a hair back in the depth buffer lets those
    // lines win the depth test instead of showing through in patches.
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);

    // If a shader failed to build, the reason is already logged: skip the loop
    // and exit with an error.
    const bool shadersReady = unlitProgram != 0 && litProgram != 0;

    OrbitCamera camera;
    bool running = shadersReady;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_EVENT_QUIT:
                running = false;
                break;
            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE) {
                    running = false;
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                // Drag with the left button to orbit, as if grabbing the world.
                if (event.motion.state & SDL_BUTTON_LMASK) {
                    camera.yaw -= event.motion.xrel * 0.005f;
                    camera.pitch = std::clamp(camera.pitch + event.motion.yrel * 0.005f,
                                              radians(2.0f), radians(89.0f));
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                // Scroll to zoom in and out.
                camera.distance = std::clamp(camera.distance * std::pow(0.9f, event.wheel.y),
                                             2.0f, 50.0f);
                break;
            default:
                break;
            }
        }

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);
        if (width > 0 && height > 0) {
            glViewport(0, 0, width, height);
            glClearColor(0.10f, 0.11f, 0.13f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            const float aspect = static_cast<float>(width) / static_cast<float>(height);
            const Mat4 projection = perspective(radians(45.0f), aspect, 0.1f, 200.0f);
            const Mat4 view = lookAt(camera.position(), camera.target, {0.0f, 1.0f, 0.0f});
            const Mat4 viewProjection = multiply(projection, view);

            glUseProgram(unlitProgram);
            glUniformMatrix4fv(unlitViewProjection, 1, GL_FALSE, viewProjection.m);
            glUniform3f(unlitColor, 0.55f, 0.58f, 0.62f);
            glBindVertexArray(grid.vertexArray);
            glDrawArrays(GL_LINES, 0, grid.vertexCount);

            glUseProgram(litProgram);
            glUniformMatrix4fv(litViewProjection, 1, GL_FALSE, viewProjection.m);
            glUniformMatrix4fv(litModel, 1, GL_FALSE, boxTransform.m);
            glUniform3f(litColor, 0.90f, 0.47f, 0.13f);
            glUniform3f(litLightDirection, lightDirection.x, lightDirection.y, lightDirection.z);
            glBindVertexArray(box.vertexArray);
            glDrawArrays(GL_TRIANGLES, 0, box.vertexCount);
        }

        SDL_GL_SwapWindow(window);
    }

    destroyMesh(box);
    destroyMesh(grid);
    glDeleteProgram(litProgram);
    glDeleteProgram(unlitProgram);
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return shadersReady ? 0 : 1;
}
