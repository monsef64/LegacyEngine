// Use the same SDL-resolved OpenGL dispatch as the renderer. The editor,
// loading overlay each own an ImGui backend, but share one
// SDL GL context. ImGui's embedded loader has process-global dlopen/dlclose
// lifetime: shutting down a temporary backend must not unload GL entry points
// while the editor (or another backend) still uses them.
#include <glad/gl.h>
#define IMGUI_IMPL_OPENGL_LOADER_CUSTOM
#include <imgui_impl_opengl3.cpp>
