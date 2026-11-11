cmake_minimum_required(VERSION 3.16...3.31)

project(LegacyEngine LANGUAGES C CXX)

# Single-config generators (Makefiles, Ninja) build unoptimised code unless a
# build type is chosen, so default to Release.
get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT is_multi_config AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# --- SDL3 (window, input, OpenGL context) -----------------------------------
#
# An installed SDL3 is used when there is one. Otherwise, or when
# LEGACY_VENDORED_SDL is ON, SDL3 is downloaded and linked statically, which
# gives an executable that does not need SDL3 installed to run.

option(LEGACY_VENDORED_SDL "Download SDL3 and link it statically, even if SDL3 is installed" OFF)
set(LEGACY_SDL_TAG "release-3.4.20" CACHE STRING "SDL3 git tag to download when SDL3 is vendored")

if(NOT LEGACY_VENDORED_SDL)
  find_package(SDL3 CONFIG QUIET)
endif()

if(SDL3_FOUND)
  message(STATUS "LegacyEngine: using the installed SDL3 ${SDL3_VERSION}")
else()
  message(STATUS "LegacyEngine: downloading SDL3 ${LEGACY_SDL_TAG} (this takes a moment)")
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG ${LEGACY_SDL_TAG}
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(SDL3)
endif()

# --- ReactPhysics3D (physics) -----------------------------------------------
#
# Always downloaded and linked statically. It is fetched as an archive rather
# than with git: the archive is a fraction of the size, and it leaves out the
# git submodules, which only the demo program of ReactPhysics3D needs.

set(LEGACY_REACTPHYSICS3D_TAG "v0.10.2" CACHE STRING "ReactPhysics3D git tag to download")

message(STATUS "LegacyEngine: downloading ReactPhysics3D ${LEGACY_REACTPHYSICS3D_TAG}")
FetchContent_Declare(
  ReactPhysics3D
  URL https://github.com/DanielChappuis/reactphysics3d/archive/refs/tags/${LEGACY_REACTPHYSICS3D_TAG}.tar.gz
)
set(BUILD_SHARED_LIBS OFF) # ReactPhysics3D follows this variable
FetchContent_MakeAvailable(ReactPhysics3D)

# The warnings of its code are not ours to fix: none while it is compiled, and
# none from its headers when our code includes them.
if(MSVC)
  target_compile_options(reactphysics3d PRIVATE /w)
else()
  target_compile_options(reactphysics3d PRIVATE -w)
endif()
get_target_property(reactphysics3d_includes reactphysics3d INTERFACE_INCLUDE_DIRECTORIES)
set_target_properties(reactphysics3d PROPERTIES
  INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${reactphysics3d_includes}")

# --- GLAD (OpenGL function loader) ------------------------------------------
#
# Generated code, see third_party/glad/README.md.

add_library(glad STATIC third_party/glad/src/gl.c)
target_include_directories(glad SYSTEM PUBLIC third_party/glad/include)

# --- Dear ImGui (SDL3 + OpenGL3 backends) -----------------------------------
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.92.5
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)
add_library(legacy_imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(legacy_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(legacy_imgui PUBLIC SDL3::SDL3 ${CMAKE_DL_LIBS})

# --- ImGuizmo (transform handles, pinned for reproducible builds) -----------
FetchContent_Declare(imguizmo
  GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
  GIT_TAG 105e4fde45e4e74aae8973d98750820ef7aca7e2)
set(IMGUIZMO_BUILD_EXAMPLE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(imguizmo)
target_link_libraries(imguizmo PUBLIC legacy_imgui)

# --- LegacyEngine -----------------------------------------------------------

add_executable(LegacyEngine
  src/main.cpp
  src/mesh.cpp
  src/physics.cpp
  src/renderer.cpp
  src/transform_editor.cpp
)
target_link_libraries(LegacyEngine PRIVATE SDL3::SDL3 ReactPhysics3D::reactphysics3d glad legacy_imgui imguizmo)

if(MSVC)
  target_compile_options(LegacyEngine PRIVATE /W4)
else()
  # Components are structs whose members have default values. Leaving the last
  # ones out, as in Transform{position}, is intended.
  target_compile_options(LegacyEngine PRIVATE
    -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(shape_tests tests/shapes.cpp src/mesh.cpp src/physics.cpp)
  target_include_directories(shape_tests PRIVATE src)
  target_link_libraries(shape_tests PRIVATE ReactPhysics3D::reactphysics3d)
  add_test(NAME shapes COMMAND shape_tests)
endif()

if(BUILD_TESTING)
  add_executable(transform_editor_tests tests/transform_editor.cpp src/transform_editor.cpp)
  target_include_directories(transform_editor_tests PRIVATE src)
  target_link_libraries(transform_editor_tests PRIVATE imguizmo)
  add_test(NAME transform_editor COMMAND transform_editor_tests)
endif()
