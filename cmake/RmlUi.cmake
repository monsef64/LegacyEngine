# Pinned upstream sources; static OpenGL integration, no sample window or Vulkan.
set(BUILD_SHARED_LIBS OFF)
set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
FetchContent_Declare(freetype URL https://github.com/freetype/freetype/archive/refs/tags/VER-2-13-3.tar.gz)
FetchContent_MakeAvailable(freetype)
if(NOT TARGET Freetype::Freetype)
  add_library(Freetype::Freetype ALIAS freetype)
endif()
set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
set(RMLUI_TESTS OFF CACHE BOOL "" FORCE)
set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
set(RMLUI_FONT_ENGINE freetype CACHE STRING "" FORCE)
FetchContent_Declare(rmlui URL https://github.com/mikke89/RmlUi/archive/58c751531f4c1a1eb2dc56f409b8e8a323a0c9ad.tar.gz)
FetchContent_MakeAvailable(rmlui)

# RmlUi 6.1's bundled robin_hood.h uses SIZE_MAX/UINT*_MAX, UINT64_C and
# uint*_t without including <cstdint>. Recent MinGW libstdc++ headers no longer
# supply these transitively, causing a misleading "Unsupported bitness" error.
# Fix the header itself so core, backends and engine consumers all see the same
# self-contained header, including translation units built without RmlUi's PCH.
set(RML_HASH_HEADER "${rmlui_SOURCE_DIR}/Include/RmlUi/Core/Containers/robin_hood.h")
file(READ "${RML_HASH_HEADER}" RML_HASH_SOURCE)
if(NOT RML_HASH_SOURCE MATCHES "#include <cstdint>")
  if(NOT RML_HASH_SOURCE MATCHES "#include <limits>")
    message(FATAL_ERROR "RmlUi integer-header adaptation no longer matches the pinned header")
  endif()
  string(REPLACE "#include <limits>" "#include <cstdint>\n#include <limits>"
    RML_HASH_SOURCE "${RML_HASH_SOURCE}")
  file(WRITE "${RML_HASH_HEADER}" "${RML_HASH_SOURCE}")
endif()

# Upstream GL3 renders its final composition to framebuffer 0. Our Game view is
# an offscreen framebuffer. Adapt only that final destination, retaining all
# upstream filters, clipping, texture support and license notices.
file(READ "${rmlui_SOURCE_DIR}/Backends/RmlUi_Renderer_GL3.cpp" RML_RENDERER)
string(REPLACE "void RenderInterface_GL3::BeginFrame()\n{"
  "static GLint legacy_ui_destination = 0;\nvoid RenderInterface_GL3::BeginFrame()\n{\n glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &legacy_ui_destination);" RML_RENDERER "${RML_RENDERER}")
string(REPLACE "// Draw to backbuffer\n\tglBindFramebuffer(GL_FRAMEBUFFER, 0);"
  "// Composite into the engine Game framebuffer.\n\tglBindFramebuffer(GL_FRAMEBUFFER, legacy_ui_destination);" RML_RENDERER "${RML_RENDERER}")
if(NOT RML_RENDERER MATCHES "static GLint legacy_ui_destination" OR
   NOT RML_RENDERER MATCHES "glBindFramebuffer\\(GL_FRAMEBUFFER, legacy_ui_destination\\)")
  message(FATAL_ERROR "RmlUi offscreen framebuffer adaptation no longer matches the pinned backend")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/rmlui_renderer.cpp" "${RML_RENDERER}")
add_library(legacy_game_ui STATIC
  src/ui/game_ui.cpp
  "${CMAKE_CURRENT_BINARY_DIR}/generated/rmlui_renderer.cpp"
  "${rmlui_SOURCE_DIR}/Backends/RmlUi_Platform_SDL.cpp")
target_include_directories(legacy_game_ui PUBLIC src PRIVATE
  "${rmlui_SOURCE_DIR}/Backends" "${CMAKE_CURRENT_BINARY_DIR}/generated" third_party/stb)
target_compile_definitions(legacy_game_ui PRIVATE
  RMLUI_SDL_VERSION_MAJOR=3 RMLUI_GL3_CUSTOM_LOADER="glad/gl.h")
target_link_libraries(legacy_game_ui PUBLIC RmlUi::Core SDL3::SDL3 PRIVATE glad legacy_thumbnails)
