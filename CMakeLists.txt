cmake_minimum_required(VERSION 3.22...3.31)

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

# Preserve every feature while allowing the linker to discard unreachable code
# inside static dependency objects. Debug builds retain their normal layout.
option(LEGACY_SMALL_RELEASE "Remove unused code/data from optimized GNU/Clang builds" ON)
if(LEGACY_SMALL_RELEASE AND NOT MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND
   (WIN32 OR CMAKE_SYSTEM_NAME STREQUAL "Linux"))
  add_compile_options(
    "$<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:-ffunction-sections>"
    "$<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:-fdata-sections>")
  add_link_options("$<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:-Wl,--gc-sections>")
endif()

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

# --- Jolt physics (static, portable CPU baseline) ----------------------------
set(BUILD_SHARED_LIBS OFF)
set(GENERATE_DEBUG_SYMBOLS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(USE_SSE4_1 OFF CACHE BOOL "" FORCE)
set(USE_SSE4_2 OFF CACHE BOOL "" FORCE)
set(USE_F16C OFF CACHE BOOL "" FORCE)
set(USE_LZCNT OFF CACHE BOOL "" FORCE)
set(USE_TZCNT OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(USE_AVX OFF CACHE BOOL "" FORCE)
set(USE_AVX2 OFF CACHE BOOL "" FORCE)
set(USE_AVX512 OFF CACHE BOOL "" FORCE)
set(USE_FMADD OFF CACHE BOOL "" FORCE)
set(JPH_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(JPH_BUILD_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(jolt
  URL https://github.com/jrouwe/JoltPhysics/archive/refs/tags/v5.3.0.tar.gz
  SOURCE_SUBDIR Build)
FetchContent_MakeAvailable(jolt)

# --- Assimp: static OBJ and glTF-only model import --------------------------
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
get_cmake_property(legacy_cache_variables CACHE_VARIABLES)
foreach(variable IN LISTS legacy_cache_variables)
  if(variable MATCHES "^ASSIMP_BUILD_.*_IMPORTER$" AND NOT variable MATCHES "^ASSIMP_BUILD_(OBJ|GLTF)_IMPORTER$")
    set(${variable} OFF CACHE BOOL "" FORCE)
  endif()
endforeach()
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_BLEND_IMPORTER OFF CACHE BOOL "" FORCE)
FetchContent_Declare(assimp
  URL https://github.com/assimp/assimp/archive/refs/tags/v5.4.3.tar.gz)
FetchContent_MakeAvailable(assimp)
set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
set(ZSTD_BUILD_STATIC ON CACHE BOOL "" FORCE)
FetchContent_Declare(zstd
  URL https://github.com/facebook/zstd/archive/refs/tags/v1.5.7.tar.gz
  SOURCE_SUBDIR build/cmake)
FetchContent_MakeAvailable(zstd)

add_library(legacy_models STATIC src/models/model_import.cpp)
target_include_directories(legacy_models PUBLIC src)
target_link_libraries(legacy_models PUBLIC assimp::assimp legacy_json)

# --- GLAD (OpenGL function loader) ------------------------------------------
#
# Generated code, see third_party/glad/README.md.

add_library(glad STATIC third_party/glad/src/gl.c)
target_include_directories(glad SYSTEM PUBLIC third_party/glad/include)

# --- Qt 6 Widgets (native editor UI) ----------------------------------------
#
# The editor's widget layer is Qt 6. Widgets come from the system SDK on Linux
# (qt6-base-dev) and from the official installer / Homebrew elsewhere; nothing
# is vendored, so builds stay small and the platform look stays native.
find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)
qt_standard_project_setup()

# --- The Qt editor layer ----------------------------------------------------
#
# All editor-only UI code builds into one static library so the engine and the
# tests link the same Qt glue. Qt's headers use signals/slots/emit as ordinary
# identifiers, so QT_NO_KEYWORDS keeps them out of the engine's namespace.
# Globs the directory so a new panel never has to be added here by hand; the
# CONFIGURE_DEPENDS re-run makes that safe.
file(GLOB LEGACY_QT_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/qt/*.cpp")
# The headers go into the target as well: several Q_OBJECT classes live in a
# header that has no .cpp of the same name (qt_panels.hpp holds the panels,
# each implemented in its own file), and AUTOMOC only scans headers it can see
# in the target's sources. Listing them is what gets them their moc output.
file(GLOB LEGACY_QT_HEADERS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/ui/qt/*.hpp")
list(APPEND LEGACY_QT_SOURCES src/editor/editor_ui.cpp ${LEGACY_QT_HEADERS})
set(CMAKE_AUTOMOC ON)
add_library(legacy_editor STATIC ${LEGACY_QT_SOURCES})
target_include_directories(legacy_editor PUBLIC src "${CMAKE_CURRENT_BINARY_DIR}/generated"
  PRIVATE third_party/stb)
target_compile_definitions(legacy_editor PUBLIC QT_NO_KEYWORDS)
target_link_libraries(legacy_editor PUBLIC SDL3::SDL3 glad Qt6::Widgets
  legacy_thumbnails legacy_json legacy_levels legacy_material_textures legacy_models
  ${CMAKE_DL_LIBS})

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

add_library(lua_console STATIC src/scripting/lua_console.cpp src/scripting/lua_scene.cpp src/scripting/lua_input.cpp src/scripting/lua_levels.cpp src/scripting/lua_events.cpp)
target_include_directories(lua_console PUBLIC src)
target_link_libraries(lua_console PUBLIC legacy_lua legacy_audio SDL3::SDL3)

add_library(legacy_thumbnails STATIC src/assets/texture_preview.cpp)
target_include_directories(legacy_thumbnails PUBLIC src PRIVATE third_party/stb)
target_link_libraries(legacy_thumbnails PUBLIC SDL3::SDL3 glad Threads::Threads)

# SIMD parser with a compatibility DOM bridge (no second text parse).
add_library(legacy_simdjson STATIC third_party/simdjson/simdjson.cpp)
target_include_directories(legacy_simdjson SYSTEM PUBLIC third_party/simdjson)
add_library(legacy_json STATIC src/project/json_io.cpp)
target_sources(legacy_json PRIVATE src/assets/image_write.cpp)
target_include_directories(legacy_json PUBLIC src PRIVATE ${assimp_SOURCE_DIR}/contrib/zlib ${assimp_BINARY_DIR}/contrib/zlib)
target_link_libraries(legacy_json PRIVATE legacy_simdjson zlibstatic libzstd_static)
# Level storage/transitions have no dependency on Lua or the editor.
add_library(legacy_levels STATIC src/levels/level_system.cpp)
target_include_directories(legacy_levels PUBLIC src)
target_link_libraries(legacy_levels PUBLIC legacy_json Threads::Threads)
target_link_libraries(lua_console PUBLIC legacy_levels)

add_library(legacy_material_textures STATIC src/rendering/material_textures.cpp src/rendering/skybox_renderer.cpp)
target_include_directories(legacy_material_textures PUBLIC src PRIVATE third_party/stb)
target_link_libraries(legacy_material_textures PUBLIC legacy_thumbnails)

# --- LegacyEngine -----------------------------------------------------------

add_executable(LegacyEngine
  src/app/main.cpp
  src/rendering/mesh.cpp
  src/physics/physics.cpp
  src/rendering/renderer.cpp
  src/editor/transform_editor.cpp
  src/editor/script_editor.cpp
)
# The Qt shell takes its fonts and icons from the platform theme, so the
# engine no longer embeds a UI font and icon atlas.
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_include_directories(LegacyEngine PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_link_libraries(LegacyEngine PRIVATE SDL3::SDL3 Jolt glad legacy_editor lua_console legacy_material_textures legacy_models)

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
  target_link_libraries(shape_tests PRIVATE Jolt legacy_json)
  add_test(NAME shapes COMMAND shape_tests)
endif()

if(BUILD_TESTING)
  add_executable(transform_editor_tests tests/transform_editor.cpp src/editor/transform_editor.cpp)
  target_include_directories(transform_editor_tests PRIVATE src)
  target_link_libraries(transform_editor_tests PRIVATE legacy_editor)
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
  target_link_libraries(script_editor_tests PRIVATE lua_console legacy_editor)
  add_test(NAME script_editor COMMAND script_editor_tests)
endif()

if(BUILD_TESTING)
  add_executable(level_objects_tests tests/level_objects.cpp)
  target_include_directories(level_objects_tests PRIVATE src)
  target_link_libraries(level_objects_tests PRIVATE legacy_editor)
  add_test(NAME level_objects COMMAND level_objects_tests)
endif()

if(BUILD_TESTING)
  add_executable(lua_scene_tests tests/lua_scene.cpp src/physics/physics.cpp)
  target_link_libraries(lua_scene_tests PRIVATE lua_console Jolt)
  add_test(NAME lua_scene COMMAND lua_scene_tests)
  set_tests_properties(lua_scene PROPERTIES TIMEOUT 15)
endif()

if(BUILD_TESTING)
  add_executable(create_hierarchy_tests tests/create_hierarchy.cpp src/physics/physics.cpp src/rendering/mesh.cpp)
  target_include_directories(create_hierarchy_tests PRIVATE src)
  target_link_libraries(create_hierarchy_tests PRIVATE legacy_editor Jolt)
  add_test(NAME create_hierarchy COMMAND create_hierarchy_tests)
endif()

if(BUILD_TESTING)
  # Builds the whole Qt widget tree offscreen: catches a dock that never gets
  # its widget, a panel pointer left null, or a signal that was never connected.
  add_executable(qt_editor_shell_tests tests/qt_editor_shell.cpp)
  target_link_libraries(qt_editor_shell_tests PRIVATE legacy_editor)
  add_test(NAME qt_editor_shell COMMAND qt_editor_shell_tests)
  set_tests_properties(qt_editor_shell PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
endif()

if(BUILD_TESTING)
  add_executable(play_mode_tests tests/play_mode.cpp src/physics/physics.cpp)
  target_include_directories(play_mode_tests PRIVATE src)
  target_link_libraries(play_mode_tests PRIVATE legacy_editor Jolt)
  target_compile_definitions(play_mode_tests PRIVATE JPH_NO_DEBUG)
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
  target_link_libraries(editor_history_tests PRIVATE legacy_editor)
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
  target_link_libraries(assets_scripts_tests PRIVATE lua_console legacy_editor)
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
  target_link_libraries(asset_trash_tests PRIVATE lua_console legacy_editor legacy_models)
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
  target_link_libraries(camera_tests PRIVATE lua_console legacy_editor)
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
  target_link_libraries(oop_project_assets_tests PRIVATE lua_console legacy_editor)
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
  target_link_libraries(editor_polish_tests PRIVATE lua_console legacy_editor legacy_models)
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
  target_link_libraries(script_exports_tests PRIVATE lua_console legacy_editor)
  if(MSVC)
    target_compile_options(script_exports_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(script_exports_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME script_exports COMMAND script_exports_tests)
endif()

if(BUILD_TESTING)
  add_executable(input_audio_tests tests/input_audio.cpp)
  target_link_libraries(input_audio_tests PRIVATE lua_console legacy_editor)
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
  target_link_libraries(lighting_tests PRIVATE lua_console legacy_editor)
  add_executable(lighting_render_tests tests/lighting_render.cpp src/rendering/renderer.cpp src/rendering/mesh.cpp)
  target_link_libraries(lighting_render_tests PRIVATE SDL3::SDL3 glad lua_console legacy_thumbnails legacy_material_textures)
  target_include_directories(lighting_render_tests PRIVATE src)
  foreach(test_target lighting_tests lighting_render_tests)
    if(MSVC)
      target_compile_options(${test_target} PRIVATE /UNDEBUG)
    else()
      target_compile_options(${test_target} PRIVATE -UNDEBUG)
    endif()
  endforeach()
  add_test(NAME lighting COMMAND lighting_tests)
  # lighting_render_tests needs a real GL context; run it manually under Xvfb.
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

if(BUILD_TESTING)
  add_executable(material_tests tests/materials.cpp)
  target_link_libraries(material_tests PRIVATE lua_console legacy_json)
  target_include_directories(material_tests PRIVATE src)
  if(MSVC)
    target_compile_options(material_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(material_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME materials COMMAND material_tests)
endif()

if(BUILD_TESTING)
  add_executable(skybox_tests tests/skybox.cpp)
  target_link_libraries(skybox_tests PRIVATE lua_console legacy_json legacy_editor)
  target_include_directories(skybox_tests PRIVATE src)
  if(MSVC)
    target_compile_options(skybox_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(skybox_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME skybox COMMAND skybox_tests)
endif()

if(BUILD_TESTING)
  add_executable(engine_log_probe tests/engine_log.cpp)
  target_include_directories(engine_log_probe PRIVATE src)
  target_link_libraries(engine_log_probe PRIVATE SDL3::SDL3)
  add_test(NAME engine_log COMMAND ${CMAKE_COMMAND} -DPROBE=$<TARGET_FILE:engine_log_probe> -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/engine_log.cmake)
endif()

if(BUILD_TESTING)
  add_executable(profiler_tests tests/profiler.cpp)
  target_include_directories(profiler_tests PRIVATE src)
  if(MSVC)
    target_compile_options(profiler_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(profiler_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME profiler COMMAND profiler_tests)
endif()

# Engine-owned GLSL lives in data/, beside release binaries and test executables.
add_custom_target(engine_data ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/data" "${CMAKE_CURRENT_BINARY_DIR}/data")
add_dependencies(LegacyEngine engine_data)
if(BUILD_TESTING)
  add_dependencies(lighting_render_tests engine_data)
endif()

if(BUILD_TESTING)
  add_executable(level_tests tests/levels.cpp)
  target_link_libraries(level_tests PRIVATE lua_console legacy_json)
  target_include_directories(level_tests PRIVATE src)
  if(MSVC)
    target_compile_options(level_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(level_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME levels COMMAND level_tests)
  add_dependencies(level_tests engine_data)
endif()

# Multi-config generators place executables in a configuration subdirectory.
foreach(runtime_target LegacyEngine lighting_render_tests level_tests)
  if(TARGET ${runtime_target})
    add_custom_command(TARGET ${runtime_target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/data" "$<TARGET_FILE_DIR:${runtime_target}>/data")
  endif()
endforeach()

# Opt-in CPU benchmark; not part of runtime packaging or the regression suite.
option(LEGACY_BUILD_BENCHMARKS "Build CPU render-setup benchmarks" OFF)
if(LEGACY_BUILD_BENCHMARKS)
  add_executable(render_setup_benchmark benchmarks/render_setup.cpp)
  target_include_directories(render_setup_benchmark PRIVATE src)
endif()

if(BUILD_TESTING)
  add_executable(ecs_iteration_tests tests/ecs_iteration.cpp)
  target_include_directories(ecs_iteration_tests PRIVATE src)
  if(MSVC)
    target_compile_options(ecs_iteration_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(ecs_iteration_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME ecs_iteration COMMAND ecs_iteration_tests)
endif()

if(BUILD_TESTING)
  add_executable(script_events_tests tests/script_events.cpp)
  target_link_libraries(script_events_tests PRIVATE lua_console)
  target_include_directories(script_events_tests PRIVATE src)
  if(MSVC)
    target_compile_options(script_events_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(script_events_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME script_events COMMAND script_events_tests)
endif()
