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
  GIT_TAG v1.92.5-docking
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

# --- Lua (embedded, no external interpreter installation needed) -------------
FetchContent_Declare(lua
  GIT_REPOSITORY https://github.com/lua/lua.git
  GIT_TAG v5.4.8
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(lua)
file(GLOB lua_sources CONFIGURE_DEPENDS "${lua_SOURCE_DIR}/*.c")
list(FILTER lua_sources EXCLUDE REGEX "/(lua|luac|onelua|ltests)\\.c$")
add_library(legacy_lua STATIC ${lua_sources})
target_include_directories(legacy_lua SYSTEM PUBLIC ${lua_SOURCE_DIR})
if(UNIX)
  target_link_libraries(legacy_lua PUBLIC m)
endif()
find_package(Threads REQUIRED)
add_library(legacy_miniaudio STATIC src/audio/miniaudio.cpp)
target_include_directories(legacy_miniaudio SYSTEM PUBLIC third_party/miniaudio)
target_link_libraries(legacy_miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX)
  target_link_libraries(legacy_miniaudio PUBLIC m)
endif()
add_library(legacy_audio STATIC src/audio/audio_clip.cpp src/audio/audio_output.cpp)
target_include_directories(legacy_audio PUBLIC src)
target_link_libraries(legacy_audio PUBLIC legacy_miniaudio)

add_library(lua_console STATIC src/scripting/lua_console.cpp src/scripting/lua_scene.cpp src/scripting/lua_input.cpp)
target_include_directories(lua_console PUBLIC src)
target_link_libraries(lua_console PUBLIC legacy_lua legacy_audio SDL3::SDL3)

add_library(legacy_thumbnails STATIC src/assets/texture_preview.cpp)
target_include_directories(legacy_thumbnails PUBLIC src PRIVATE third_party/stb)
target_link_libraries(legacy_thumbnails PUBLIC SDL3::SDL3 glad)
target_link_libraries(legacy_imgui PUBLIC legacy_thumbnails)

# SIMD parser with a compatibility DOM bridge (no second text parse).
add_library(legacy_simdjson STATIC third_party/simdjson/simdjson.cpp)
target_include_directories(legacy_simdjson SYSTEM PUBLIC third_party/simdjson)
add_library(legacy_json STATIC src/project/json_io.cpp)
target_include_directories(legacy_json PUBLIC src)
target_link_libraries(legacy_json PRIVATE legacy_simdjson)
target_link_libraries(lua_console PUBLIC legacy_json)
target_link_libraries(legacy_imgui PUBLIC legacy_json)

# --- LegacyEngine -----------------------------------------------------------

add_executable(LegacyEngine
  src/app/main.cpp
  src/rendering/mesh.cpp
  src/physics/physics.cpp
  src/rendering/renderer.cpp
  src/editor/transform_editor.cpp
  src/editor/script_editor.cpp
)
# Embed the UI font so release binaries need no external asset directory.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/roboto/Roboto-Regular.ttf")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/third_party/roboto/Roboto-Regular.ttf" LEGACY_FONT_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," LEGACY_FONT_BYTES "${LEGACY_FONT_HEX}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/editor_font.hpp"
  "#pragma once\ninline const unsigned char editorFontData[] = {${LEGACY_FONT_BYTES}};\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fontawesome/fa-solid-900.ttf")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/third_party/fontawesome/fa-solid-900.ttf" LEGACY_ICON_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1," LEGACY_ICON_BYTES "${LEGACY_ICON_HEX}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/generated/editor_icons.hpp" "#pragma once\ninline const unsigned char editorIconData[] = {${LEGACY_ICON_BYTES}};\n")
target_include_directories(LegacyEngine PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(LegacyEngine PRIVATE SDL3::SDL3 ReactPhysics3D::reactphysics3d glad legacy_imgui imguizmo lua_console)

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
  add_executable(shape_tests tests/shapes.cpp src/rendering/mesh.cpp src/physics/physics.cpp)
  target_include_directories(shape_tests PRIVATE src)
  target_link_libraries(shape_tests PRIVATE ReactPhysics3D::reactphysics3d)
  add_test(NAME shapes COMMAND shape_tests)
endif()

if(BUILD_TESTING)
  add_executable(transform_editor_tests tests/transform_editor.cpp src/editor/transform_editor.cpp)
  target_include_directories(transform_editor_tests PRIVATE src)
  target_link_libraries(transform_editor_tests PRIVATE imguizmo)
  add_test(NAME transform_editor COMMAND transform_editor_tests)
endif()

if(BUILD_TESTING)
  add_executable(lua_console_tests tests/lua_console.cpp)
  target_link_libraries(lua_console_tests PRIVATE lua_console)
  add_test(NAME lua_console COMMAND lua_console_tests)
  set_tests_properties(lua_console PROPERTIES TIMEOUT 15)
endif()

if(BUILD_TESTING)
  add_executable(script_editor_tests tests/script_editor.cpp src/editor/script_editor.cpp)
  target_link_libraries(script_editor_tests PRIVATE lua_console legacy_imgui)
  add_test(NAME script_editor COMMAND script_editor_tests)
endif()

if(BUILD_TESTING)
  add_executable(level_objects_tests tests/level_objects.cpp)
  target_include_directories(level_objects_tests PRIVATE src)
  target_link_libraries(level_objects_tests PRIVATE legacy_imgui)
  add_test(NAME level_objects COMMAND level_objects_tests)
endif()

if(BUILD_TESTING)
  add_executable(lua_scene_tests tests/lua_scene.cpp src/physics/physics.cpp)
  target_link_libraries(lua_scene_tests PRIVATE lua_console ReactPhysics3D::reactphysics3d)
  add_test(NAME lua_scene COMMAND lua_scene_tests)
  set_tests_properties(lua_scene PROPERTIES TIMEOUT 15)
endif()

if(BUILD_TESTING)
  add_executable(create_hierarchy_tests tests/create_hierarchy.cpp src/physics/physics.cpp src/rendering/mesh.cpp)
  target_include_directories(create_hierarchy_tests PRIVATE src)
  target_link_libraries(create_hierarchy_tests PRIVATE legacy_imgui ReactPhysics3D::reactphysics3d)
  add_test(NAME create_hierarchy COMMAND create_hierarchy_tests)
endif()

if(BUILD_TESTING)
  add_executable(editor_layout_tests tests/editor_layout.cpp src/physics/physics.cpp)
  target_include_directories(editor_layout_tests PRIVATE src)
  target_link_libraries(editor_layout_tests PRIVATE imguizmo ReactPhysics3D::reactphysics3d)
  add_test(NAME editor_layout COMMAND editor_layout_tests)
endif()

if(BUILD_TESTING)
  add_executable(editor_theme_tests tests/editor_theme.cpp)
  target_include_directories(editor_theme_tests PRIVATE src "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_link_libraries(editor_theme_tests PRIVATE legacy_imgui)
  # Keep assertions active in Release builds, as with the other test targets.
  if(MSVC)
    target_compile_options(editor_theme_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(editor_theme_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME editor_theme COMMAND editor_theme_tests)
endif()

if(BUILD_TESTING)
  add_executable(play_mode_tests tests/play_mode.cpp src/physics/physics.cpp)
  target_include_directories(play_mode_tests PRIVATE src)
  target_link_libraries(play_mode_tests PRIVATE legacy_imgui ReactPhysics3D::reactphysics3d)
  if(MSVC)
    target_compile_options(play_mode_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(play_mode_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME play_mode COMMAND play_mode_tests)
endif()

if(BUILD_TESTING)
  add_executable(editor_history_tests tests/editor_history.cpp)
  target_include_directories(editor_history_tests PRIVATE src)
  target_link_libraries(editor_history_tests PRIVATE legacy_imgui)
  if(MSVC)
    target_compile_options(editor_history_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(editor_history_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME editor_history COMMAND editor_history_tests)
endif()

if(BUILD_TESTING)
  add_executable(project_io_tests tests/project_io.cpp)
  target_include_directories(project_io_tests PRIVATE src)
  if(MSVC)
    target_compile_options(project_io_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(project_io_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME project_io COMMAND project_io_tests)
endif()

if(BUILD_TESTING)
  add_executable(editor_document_tests tests/editor_document.cpp)
  target_include_directories(editor_document_tests PRIVATE src)
  target_link_libraries(editor_document_tests PRIVATE SDL3::SDL3)
  if(MSVC)
    target_compile_options(editor_document_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(editor_document_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME editor_document COMMAND editor_document_tests)
endif()

if(BUILD_TESTING)
  add_executable(assets_scripts_tests tests/assets_scripts.cpp)
  target_include_directories(assets_scripts_tests PRIVATE src)
  target_link_libraries(assets_scripts_tests PRIVATE lua_console legacy_imgui)
  if(MSVC)
    target_compile_options(assets_scripts_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(assets_scripts_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME assets_scripts COMMAND assets_scripts_tests)
endif()

if(BUILD_TESTING)
  add_executable(asset_trash_tests tests/asset_trash.cpp src/editor/script_editor.cpp)
  target_include_directories(asset_trash_tests PRIVATE src)
  target_link_libraries(asset_trash_tests PRIVATE lua_console legacy_imgui)
  if(MSVC)
    target_compile_options(asset_trash_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(asset_trash_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME asset_trash COMMAND asset_trash_tests)
endif()

if(BUILD_TESTING)
  add_executable(camera_tests tests/cameras.cpp src/rendering/mesh.cpp)
  target_include_directories(camera_tests PRIVATE src)
  target_link_libraries(camera_tests PRIVATE lua_console legacy_imgui)
  if(MSVC)
    target_compile_options(camera_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(camera_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME cameras COMMAND camera_tests)
endif()

if(BUILD_TESTING)
  add_executable(oop_project_assets_tests tests/oop_project_assets.cpp)
  target_include_directories(oop_project_assets_tests PRIVATE src)
  target_link_libraries(oop_project_assets_tests PRIVATE lua_console legacy_imgui)
  if(MSVC)
    target_compile_options(oop_project_assets_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(oop_project_assets_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME oop_project_assets COMMAND oop_project_assets_tests)
endif()

if(BUILD_TESTING)
  add_executable(editor_polish_tests tests/editor_polish.cpp src/rendering/mesh.cpp src/editor/script_editor.cpp)
  target_include_directories(editor_polish_tests PRIVATE src)
  target_link_libraries(editor_polish_tests PRIVATE lua_console legacy_imgui)
  if(MSVC)
    target_compile_options(editor_polish_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(editor_polish_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME editor_polish COMMAND editor_polish_tests)
endif()

if(BUILD_TESTING)
  add_executable(audio_editor_tests tests/audio_editor.cpp)
  target_link_libraries(audio_editor_tests PRIVATE lua_console SDL3::SDL3)
  if(MSVC)
    target_compile_options(audio_editor_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(audio_editor_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME audio_editor COMMAND audio_editor_tests)
endif()

if(BUILD_TESTING)
  add_executable(script_exports_tests tests/script_exports.cpp src/editor/script_editor.cpp)
  target_link_libraries(script_exports_tests PRIVATE lua_console legacy_imgui)
  if(MSVC)
    target_compile_options(script_exports_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(script_exports_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME script_exports COMMAND script_exports_tests)
endif()

if(BUILD_TESTING)
  add_executable(input_audio_tests tests/input_audio.cpp)
  target_link_libraries(input_audio_tests PRIVATE lua_console legacy_imgui)
  target_compile_definitions(input_audio_tests PRIVATE TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
  if(MSVC)
    target_compile_options(input_audio_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(input_audio_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME input_audio COMMAND input_audio_tests)
endif()

if(BUILD_TESTING)
  add_executable(lighting_tests tests/lighting.cpp src/rendering/mesh.cpp)
  target_link_libraries(lighting_tests PRIVATE lua_console legacy_imgui)
  add_executable(lighting_render_tests tests/lighting_render.cpp src/rendering/renderer.cpp src/rendering/mesh.cpp)
  target_link_libraries(lighting_render_tests PRIVATE SDL3::SDL3 glad lua_console legacy_thumbnails)
  target_include_directories(lighting_render_tests PRIVATE src)
  foreach(test_target lighting_tests lighting_render_tests)
    if(MSVC)
      target_compile_options(${test_target} PRIVATE /UNDEBUG)
    else()
      target_compile_options(${test_target} PRIVATE -UNDEBUG)
    endif()
  endforeach()
  add_test(NAME lighting COMMAND lighting_tests)
  # lighting_render_tests needs a real GL context; CI invokes it under Xvfb.
endif()

# Standalone project tests do not otherwise link an engine subsystem.
if(BUILD_TESTING)
  target_link_libraries(project_io_tests PRIVATE legacy_json)
  target_link_libraries(editor_document_tests PRIVATE legacy_json)
endif()

if(BUILD_TESTING)
  add_executable(asset_thumbnail_tests tests/asset_thumbnails.cpp)
  target_link_libraries(asset_thumbnail_tests PRIVATE legacy_thumbnails legacy_json)
  if(MSVC)
    target_compile_options(asset_thumbnail_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(asset_thumbnail_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME asset_thumbnails COMMAND asset_thumbnail_tests)
endif()
