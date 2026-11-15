cmake_minimum_required(VERSION 3.20...3.31)

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

# --- SDL3 (window and input) -------------------------------------------------
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
  set(SDL_OPENGL OFF CACHE BOOL "" FORCE)
  set(SDL_OPENGLES OFF CACHE BOOL "" FORCE)

  FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG ${LEGACY_SDL_TAG}
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(SDL3)
endif()

# --- Jolt Physics ------------------------------------------------------------
# Keep Jolt compatible with baseline x86-64 CPUs and the engine's toolchain.
set(BUILD_SHARED_LIBS OFF)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "" FORCE)
set(ENABLE_OBJECT_STREAM OFF CACHE BOOL "" FORCE)
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
foreach(feature USE_SSE4_1 USE_SSE4_2 USE_AVX USE_AVX2 USE_AVX512 USE_LZCNT USE_TZCNT USE_F16C USE_FMADD)
  set(${feature} OFF CACHE BOOL "" FORCE)
endforeach()
set(LEGACY_JOLT_TAG "v5.3.0" CACHE STRING "Jolt Physics release tag")
FetchContent_Declare(jolt
  URL https://github.com/jrouwe/JoltPhysics/archive/refs/tags/${LEGACY_JOLT_TAG}.tar.gz
  SOURCE_SUBDIR Build)
FetchContent_MakeAvailable(jolt)

# --- Assimp: OBJ and animated/static glTF import only -----------------------
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
# Clear any importer overrides left in an incremental build cache.
get_cmake_property(legacy_cached_variables CACHE_VARIABLES)
foreach(variable IN LISTS legacy_cached_variables)
  if(variable MATCHES "^ASSIMP_BUILD_.*_IMPORTER$" AND
     NOT variable STREQUAL "ASSIMP_BUILD_OBJ_IMPORTER" AND
     NOT variable STREQUAL "ASSIMP_BUILD_GLTF_IMPORTER")
    set(${variable} OFF CACHE BOOL "" FORCE)
  endif()
endforeach()
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
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

# --- Dear ImGui core and platform backend ------------------------------------
FetchContent_Declare(imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG v1.92.5-docking
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(imgui)
add_library(legacy_imgui STATIC
  ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp)
target_include_directories(legacy_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(legacy_imgui PUBLIC SDL3::SDL3)
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

add_library(lua_console STATIC src/scripting/lua_console.cpp src/scripting/lua_scene.cpp src/scripting/lua_input.cpp src/scripting/lua_levels.cpp src/scripting/lua_events.cpp)
target_include_directories(lua_console PUBLIC src)
target_link_libraries(lua_console PUBLIC legacy_lua legacy_audio SDL3::SDL3)

# CPU thumbnail decoding is graphics-API independent.
add_library(legacy_thumbnail_data STATIC src/assets/texture_thumbnail.cpp)
target_include_directories(legacy_thumbnail_data PUBLIC src PRIVATE third_party/stb)
target_link_libraries(legacy_imgui PUBLIC legacy_thumbnail_data)

# SIMD parser with a compatibility DOM bridge (no second text parse).
add_library(legacy_simdjson STATIC third_party/simdjson/simdjson.cpp)
target_include_directories(legacy_simdjson SYSTEM PUBLIC third_party/simdjson)
add_library(legacy_json STATIC src/project/json_io.cpp)
target_include_directories(legacy_json PUBLIC src PRIVATE ${assimp_SOURCE_DIR}/contrib/zlib ${assimp_BINARY_DIR}/contrib/zlib)
target_link_libraries(legacy_json PRIVATE legacy_simdjson zlibstatic libzstd_static)
# Compile the backend once with the same configuration macros as Jolt. Tests
# may undefine NDEBUG for their own assertions without changing Jolt's ABI.
add_library(legacy_physics STATIC src/physics/physics.cpp)
target_include_directories(legacy_physics PUBLIC src)
target_link_libraries(legacy_physics PRIVATE Jolt PUBLIC legacy_json)
# Level storage/transitions have no dependency on Lua or the editor.
add_library(legacy_levels STATIC src/levels/level_system.cpp)
target_include_directories(legacy_levels PUBLIC src)
target_link_libraries(legacy_levels PUBLIC legacy_json Threads::Threads)
target_link_libraries(lua_console PUBLIC legacy_levels)
target_link_libraries(legacy_imgui PUBLIC legacy_json)

# --- LegacyEngine -----------------------------------------------------------

add_executable(LegacyEngine
  src/app/main.cpp
  src/rendering/mesh.cpp
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
target_link_libraries(LegacyEngine PRIVATE SDL3::SDL3 legacy_physics imguizmo lua_console legacy_models)
target_include_directories(LegacyEngine PRIVATE third_party/stb)

if(MSVC)
  target_compile_options(LegacyEngine PRIVATE /W4)
else()
  # Components are structs whose members have default values. Leaving the last
  # ones out, as in Transform{position}, is intended.
  target_compile_options(LegacyEngine PRIVATE
    -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()

include(CTest)
option(LEGACY_TEST_EDITOR_SMOKE "Run the Vulkan editor's 120-frame graphical smoke test in CTest" OFF)
if(BUILD_TESTING AND LEGACY_TEST_EDITOR_SMOKE)
  add_test(NAME vulkan_editor_smoke COMMAND LegacyEngine --smoke-test)
  set_tests_properties(vulkan_editor_smoke PROPERTIES TIMEOUT 180)
endif()

# The production editor requires the Vulkan context and compiled shaders.
set(LEGACY_BUILD_VULKAN_PROTOTYPE ON)
set(LEGACY_BUILD_VULKAN_SHADERS ON)
if(LEGACY_BUILD_VULKAN_PROTOTYPE OR LEGACY_BUILD_VULKAN_SHADERS)
  find_package(Vulkan REQUIRED)
endif()
if(LEGACY_BUILD_VULKAN_PROTOTYPE)
  add_library(legacy_vulkan_context STATIC src/rendering/vulkan_context.cpp)
  target_include_directories(legacy_vulkan_context PUBLIC src)
  target_link_libraries(legacy_vulkan_context PUBLIC SDL3::SDL3 Vulkan::Vulkan)
  add_executable(vulkan_presentation_smoke tests/vulkan_smoke.cpp)
  target_link_libraries(vulkan_presentation_smoke PRIVATE legacy_vulkan_context)
  if(BUILD_TESTING)
    add_executable(vulkan_context_mock_tests tests/vulkan_context_mock.cpp src/rendering/vulkan_context.cpp)
    target_include_directories(vulkan_context_mock_tests PRIVATE src ${Vulkan_INCLUDE_DIRS})
    # Include SDL headers but use fake SDL and Vulkan symbols, without linking
    # either runtime. This exercises the context's lifecycle without a GPU.
    target_include_directories(vulkan_context_mock_tests SYSTEM PRIVATE $<TARGET_PROPERTY:SDL3::SDL3,INTERFACE_INCLUDE_DIRECTORIES>)
    if(MSVC)
      target_compile_options(vulkan_context_mock_tests PRIVATE /UNDEBUG)
    else()
      target_compile_options(vulkan_context_mock_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME vulkan_context_mock COMMAND vulkan_context_mock_tests)
  endif()
endif()

if(LEGACY_BUILD_VULKAN_SHADERS)
  find_program(LEGACY_GLSLANG NAMES glslangValidator glslang
    HINTS "$ENV{VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/Bin")
  if(NOT LEGACY_GLSLANG)
    message(FATAL_ERROR "Vulkan shader builds require glslangValidator or glslang")
  endif()
  set(legacy_vulkan_shader_outputs)
  foreach(shader IN ITEMS lit.vert lit.frag unlit.vert unlit.frag shadow.vert skybox.vert skybox.frag skinning.comp motion.vert motion.frag)
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/vulkan/${shader}")
    set(output "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/${shader}.spv")
    add_custom_command(OUTPUT "${output}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders"
      COMMAND "${LEGACY_GLSLANG}" -V --target-env vulkan1.0 "${source}" -o "${output}"
      DEPENDS "${source}"
      VERBATIM)
    list(APPEND legacy_vulkan_shader_outputs "${output}")
  endforeach()
  # Preview surfaces use the same direct lighting without a shadow atlas.
  set(legacy_vulkan_lit_no_shadow "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/lit_noshadow.frag.spv")
  add_custom_command(OUTPUT "${legacy_vulkan_lit_no_shadow}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders"
    COMMAND "${LEGACY_GLSLANG}" -V --target-env vulkan1.0 -DLEGACY_NO_SHADOW=1
      "${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/vulkan/lit.frag" -o "${legacy_vulkan_lit_no_shadow}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/vulkan/lit.frag"
    VERBATIM)
  list(APPEND legacy_vulkan_shader_outputs "${legacy_vulkan_lit_no_shadow}")
  add_custom_target(vulkan_shaders ALL DEPENDS ${legacy_vulkan_shader_outputs})
  if(BUILD_TESTING)
    add_executable(vulkan_skinning_compute_tests tests/vulkan_skinning_compute.cpp)
    target_include_directories(vulkan_skinning_compute_tests PRIVATE src)
    target_link_libraries(vulkan_skinning_compute_tests PRIVATE Vulkan::Vulkan)
    add_dependencies(vulkan_skinning_compute_tests vulkan_shaders)
    add_test(NAME vulkan_skinning_compute COMMAND vulkan_skinning_compute_tests
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/skinning.comp.spv")
    set_tests_properties(vulkan_skinning_compute PROPERTIES SKIP_RETURN_CODE 77)
    foreach(pipeline IN ITEMS lit unlit skybox motion)
      add_test(NAME vulkan_shader_link_${pipeline} COMMAND "${LEGACY_GLSLANG}" -V -l
        "${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/vulkan/${pipeline}.vert"
        "${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/vulkan/${pipeline}.frag"
        -o "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/test-link-${pipeline}.spv")
    endforeach()
    add_test(NAME vulkan_shader_link_imgui_srgb COMMAND "${LEGACY_GLSLANG}" -V -l
      "${imgui_SOURCE_DIR}/backends/vulkan/glsl_shader.vert"
      "${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/vulkan/imgui_srgb.frag"
      -o "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/test-link-imgui-srgb.spv")
  endif()
endif()

if(LEGACY_BUILD_VULKAN_PROTOTYPE AND LEGACY_BUILD_VULKAN_SHADERS)
  add_library(legacy_vulkan_scene STATIC src/rendering/mesh.cpp src/rendering/vulkan_scene_surface.cpp src/rendering/vulkan_motion_vectors.cpp src/rendering/vulkan_scene_shadows.cpp src/rendering/vulkan_scene_sky.cpp src/rendering/vulkan_texture.cpp src/rendering/streamed_texture.cpp src/rendering/vulkan_material_textures.cpp src/rendering/vulkan_skybox_cache.cpp)
  target_include_directories(legacy_vulkan_scene PUBLIC src PRIVATE third_party/stb)
  target_link_libraries(legacy_vulkan_scene PUBLIC Vulkan::Vulkan legacy_json legacy_thumbnail_data Threads::Threads)
  # Embed the sRGB-attachment fallback shader in the backend so every consumer
  # (including tests and secondary windows) has it without runtime data paths.
  set(imgui_srgb_spv "${CMAKE_CURRENT_BINARY_DIR}/generated/imgui_srgb.inc")
  add_custom_command(OUTPUT "${imgui_srgb_spv}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
    COMMAND "${LEGACY_GLSLANG}" -V --target-env vulkan1.0 -x
      "${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/vulkan/imgui_srgb.frag" -o "${imgui_srgb_spv}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/data/shaders/vulkan/imgui_srgb.frag"
    VERBATIM)
  add_custom_target(imgui_srgb_shader DEPENDS "${imgui_srgb_spv}")
  # Share one ImGui core between the editor, ImGuizmo and Vulkan backend.
  add_library(legacy_vulkan_imgui STATIC
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
    src/rendering/vulkan_imgui.cpp)
  add_dependencies(legacy_vulkan_imgui imgui_srgb_shader)
  target_include_directories(legacy_vulkan_imgui PUBLIC src PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
  target_link_libraries(legacy_vulkan_imgui PUBLIC legacy_imgui Vulkan::Vulkan legacy_vulkan_context legacy_vulkan_scene)
  add_library(legacy_vulkan_thumbnails STATIC src/assets/vulkan_thumbnail_cache.cpp)
  target_include_directories(legacy_vulkan_thumbnails PUBLIC src)
  target_link_libraries(legacy_vulkan_thumbnails PUBLIC legacy_vulkan_imgui legacy_thumbnail_data)
  target_link_libraries(LegacyEngine PRIVATE legacy_vulkan_thumbnails)
  add_dependencies(LegacyEngine vulkan_shaders)
  # Re-link and refresh the runtime copy when only a shader changes.
  set_property(TARGET LegacyEngine APPEND PROPERTY LINK_DEPENDS ${legacy_vulkan_shader_outputs})
  add_custom_command(TARGET LegacyEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:LegacyEngine>/data/shaders/vulkan/compiled"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${legacy_vulkan_shader_outputs}
      "$<TARGET_FILE_DIR:LegacyEngine>/data/shaders/vulkan/compiled"
    VERBATIM)
  set(LEGACY_FSR3_EXPERIMENT_DIR "" CACHE PATH "Optional built Linux FSR SDK experiment directory")
  option(LEGACY_FSR3_GAME "Build experimental FSR 3 support for the Game view (Linux only)" OFF)
  if(LEGACY_FSR3_GAME)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR
       NOT EXISTS "${LEGACY_FSR3_EXPERIMENT_DIR}/build/libffx_fsr3upscaler_vk.a")
      message(FATAL_ERROR "Game FSR requires the Linux SDK experiment built by tools/build_fsr3_experiment.sh")
    endif()
    add_library(legacy_fsr3_game SHARED src/rendering/vulkan_fsr3_game.cpp)
    target_include_directories(legacy_fsr3_game PUBLIC src PRIVATE
      "${LEGACY_FSR3_EXPERIMENT_DIR}/port/fsr3-linux" "${LEGACY_FSR3_EXPERIMENT_DIR}/sdk/sdk/include")
    target_compile_definitions(legacy_fsr3_game PRIVATE FFX_GCC "FFX_SDK_DEFAULT_CONTEXT_SIZE=(1024 * 256)")
    target_link_libraries(legacy_fsr3_game PRIVATE "${LEGACY_FSR3_EXPERIMENT_DIR}/build/libffx_fsr3upscaler_vk.a" Vulkan::Vulkan)
    # Restrict linker wrapping and SDK symbols to this shared library; wrapping
    # the editor itself intercepts its Vulkan calls before host initialization.
    target_link_options(legacy_fsr3_game PRIVATE "LINKER:-Bsymbolic" "LINKER:--exclude-libs,ALL")
    foreach(fn vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceFeatures
               vkGetPhysicalDeviceFeatures2 vkGetPhysicalDeviceMemoryProperties vkEnumerateDeviceExtensionProperties
               vkGetDeviceProcAddr vkCreateBuffer)
      target_link_options(legacy_fsr3_game PRIVATE "LINKER:--wrap=${fn}")
    endforeach()
    target_link_libraries(LegacyEngine PRIVATE legacy_fsr3_game)
    target_compile_definitions(LegacyEngine PRIVATE LEGACY_FSR3_GAME=1)
    target_compile_definitions(legacy_vulkan_context PRIVATE LEGACY_FSR3_GAME=1)
  endif()
  if(BUILD_TESTING)
    if(LEGACY_FSR3_GAME)
      add_executable(vulkan_fsr3_game_tests tests/vulkan_fsr3_game.cpp)
      target_link_libraries(vulkan_fsr3_game_tests PRIVATE legacy_fsr3_game legacy_vulkan_scene legacy_vulkan_context)
      add_dependencies(vulkan_fsr3_game_tests vulkan_shaders)
      add_test(NAME vulkan_fsr3_game COMMAND vulkan_fsr3_game_tests
        "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/unlit.vert.spv"
        "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/unlit.frag.spv"
        "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/motion.vert.spv"
        "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/motion.frag.spv")
      set_tests_properties(vulkan_fsr3_game PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 180)
      if(LEGACY_TEST_EDITOR_SMOKE)
        add_test(NAME vulkan_editor_fsr_smoke COMMAND LegacyEngine --smoke-test)
        set_tests_properties(vulkan_editor_fsr_smoke PROPERTIES
          ENVIRONMENT "LEGACY_SMOKE_FSR=1" TIMEOUT 300)
      endif()
    endif()
    # The standalone test links the port only into its own process.
    if(LEGACY_FSR3_EXPERIMENT_DIR)
      if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "The experimental FSR port test is Linux-only")
      endif()
      set(fsr_root "${LEGACY_FSR3_EXPERIMENT_DIR}")
      if(NOT EXISTS "${fsr_root}/build/libffx_fsr3upscaler_vk.a" OR
         NOT EXISTS "${fsr_root}/port/fsr3-linux/tests/fsr3_selftest.cpp" OR
         NOT EXISTS "${fsr_root}/sdk/sdk/include/FidelityFX/host/ffx_fsr3upscaler.h")
        message(FATAL_ERROR "Build the FSR experiment with tools/build_fsr3_experiment.sh first")
      endif()
      add_executable(fsr3_engine_experiment tests/fsr3_engine_experiment.cpp)
      target_link_libraries(fsr3_engine_experiment PRIVATE legacy_vulkan_scene "${fsr_root}/build/libffx_fsr3upscaler_vk.a" Vulkan::Vulkan)
      target_include_directories(fsr3_engine_experiment PRIVATE "${fsr_root}/port/fsr3-linux" "${fsr_root}/sdk/sdk/include")
      target_compile_definitions(fsr3_engine_experiment PRIVATE FFX_GCC "FFX_SDK_DEFAULT_CONTEXT_SIZE=(1024 * 256)"
        LEGACY_FSR3_SELFTEST_SOURCE="${fsr_root}/port/fsr3-linux/tests/fsr3_selftest.cpp")
      foreach(fn vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties2 vkGetPhysicalDeviceFeatures
                 vkGetPhysicalDeviceFeatures2 vkGetPhysicalDeviceMemoryProperties vkEnumerateDeviceExtensionProperties
                 vkGetDeviceProcAddr vkCreateBuffer)
        target_link_options(fsr3_engine_experiment PRIVATE "LINKER:--wrap=${fn}")
      endforeach()
      add_dependencies(fsr3_engine_experiment vulkan_shaders)
      add_test(NAME fsr3_engine_experiment COMMAND fsr3_engine_experiment
        "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/unlit.vert.spv"
        "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/unlit.frag.spv"
        "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/motion.vert.spv"
        "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/motion.frag.spv")
      set_tests_properties(fsr3_engine_experiment PROPERTIES TIMEOUT 180)
    endif()
    add_executable(vulkan_motion_vectors_tests tests/vulkan_motion_vectors.cpp)
    target_link_libraries(vulkan_motion_vectors_tests PRIVATE legacy_vulkan_scene legacy_vulkan_context legacy_models)
    target_compile_definitions(vulkan_motion_vectors_tests PRIVATE LEGACY_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
    add_dependencies(vulkan_motion_vectors_tests vulkan_shaders)
    add_test(NAME vulkan_motion_vectors COMMAND vulkan_motion_vectors_tests
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/unlit.vert.spv"
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/unlit.frag.spv"
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/motion.vert.spv"
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/motion.frag.spv"
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/lit.vert.spv"
      "${legacy_vulkan_lit_no_shadow}")
    set_tests_properties(vulkan_motion_vectors PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_executable(vulkan_scene_render_tests tests/vulkan_scene_render.cpp)
    target_link_libraries(vulkan_scene_render_tests PRIVATE legacy_vulkan_scene legacy_vulkan_context)
    add_dependencies(vulkan_scene_render_tests vulkan_shaders)
    add_test(NAME vulkan_scene_render COMMAND vulkan_scene_render_tests
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/unlit.vert.spv"
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/unlit.frag.spv")
    set_tests_properties(vulkan_scene_render PROPERTIES SKIP_RETURN_CODE 77)
    add_executable(vulkan_lit_render_tests tests/vulkan_lit_render.cpp)
    target_link_libraries(vulkan_lit_render_tests PRIVATE legacy_vulkan_scene legacy_vulkan_context)
    add_dependencies(vulkan_lit_render_tests vulkan_shaders)
    add_test(NAME vulkan_lit_render COMMAND vulkan_lit_render_tests
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/unlit.vert.spv"
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/unlit.frag.spv"
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/lit.vert.spv"
      "${legacy_vulkan_lit_no_shadow}")
    set_tests_properties(vulkan_lit_render PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_executable(vulkan_shadow_render_tests tests/vulkan_shadow_render.cpp)
    target_link_libraries(vulkan_shadow_render_tests PRIVATE legacy_vulkan_scene legacy_vulkan_context)
    add_dependencies(vulkan_shadow_render_tests vulkan_shaders)
    add_test(NAME vulkan_shadow_render COMMAND vulkan_shadow_render_tests
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/unlit.vert.spv"
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/unlit.frag.spv"
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/lit.vert.spv"
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/lit.frag.spv"
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/shadow.vert.spv")
    set_tests_properties(vulkan_shadow_render PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_executable(vulkan_sky_render_tests tests/vulkan_sky_render.cpp)
    target_link_libraries(vulkan_sky_render_tests PRIVATE legacy_vulkan_scene legacy_vulkan_context)
    add_dependencies(vulkan_sky_render_tests vulkan_shaders)
    add_test(NAME vulkan_sky_render COMMAND vulkan_sky_render_tests
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/unlit.vert.spv"
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/unlit.frag.spv"
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/skybox.vert.spv"
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/skybox.frag.spv")
    set_tests_properties(vulkan_sky_render PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_executable(vulkan_overlay_render_tests tests/vulkan_overlay_render.cpp)
    target_link_libraries(vulkan_overlay_render_tests PRIVATE legacy_vulkan_scene legacy_vulkan_context)
    add_dependencies(vulkan_overlay_render_tests vulkan_shaders)
    add_test(NAME vulkan_overlay_render COMMAND vulkan_overlay_render_tests
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/unlit.vert.spv"
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/unlit.frag.spv"
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/lit.vert.spv"
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/lit_noshadow.frag.spv")
    set_tests_properties(vulkan_overlay_render PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_executable(vulkan_imgui_render_tests tests/vulkan_imgui_render.cpp)
    target_link_libraries(vulkan_imgui_render_tests PRIVATE legacy_vulkan_thumbnails)
    add_dependencies(vulkan_imgui_render_tests vulkan_shaders)
    add_test(NAME vulkan_imgui_render COMMAND vulkan_imgui_render_tests
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/unlit.vert.spv"
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/unlit.frag.spv")
    set_tests_properties(vulkan_imgui_render PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    add_executable(vulkan_editor_preview_tests tests/vulkan_editor_preview.cpp)
    target_link_libraries(vulkan_editor_preview_tests PRIVATE legacy_vulkan_thumbnails imguizmo legacy_audio legacy_models)
    target_include_directories(vulkan_editor_preview_tests PRIVATE third_party/stb)
    add_dependencies(vulkan_editor_preview_tests vulkan_shaders)
    add_test(NAME vulkan_editor_preview COMMAND vulkan_editor_preview_tests
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/unlit.vert.spv"
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/unlit.frag.spv"
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/lit.vert.spv"
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/lit_noshadow.frag.spv"
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/skybox.vert.spv"
      "${CMAKE_CURRENT_BINARY_DIR}/vulkan-shaders/skybox.frag.spv")
    set_tests_properties(vulkan_editor_preview PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
  endif()
endif()

if(BUILD_TESTING)
  add_executable(vulkan_clip_tests tests/vulkan_clip.cpp)
  target_include_directories(vulkan_clip_tests PRIVATE src)
  if(MSVC)
    target_compile_options(vulkan_clip_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(vulkan_clip_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME vulkan_clip COMMAND vulkan_clip_tests)
endif()

if(BUILD_TESTING)
  add_executable(temporal_sampling_tests tests/temporal_sampling.cpp)
  target_include_directories(temporal_sampling_tests PRIVATE src)
  if(MSVC)
    target_compile_options(temporal_sampling_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(temporal_sampling_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME temporal_sampling COMMAND temporal_sampling_tests)
  add_executable(temporal_history_tests tests/temporal_history.cpp)
  target_include_directories(temporal_history_tests PRIVATE src)
  if(MSVC)
    target_compile_options(temporal_history_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(temporal_history_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME temporal_history COMMAND temporal_history_tests)
endif()

if(BUILD_TESTING)
  add_executable(physics_backend_tests tests/physics_backend.cpp)
  target_link_libraries(physics_backend_tests PRIVATE legacy_physics legacy_json)
  target_include_directories(physics_backend_tests PRIVATE src)
  add_test(NAME physics_backend COMMAND physics_backend_tests)
endif()

if(BUILD_TESTING)
  add_executable(shape_tests tests/shapes.cpp src/rendering/mesh.cpp)
  target_include_directories(shape_tests PRIVATE src)
  target_link_libraries(shape_tests PRIVATE legacy_physics legacy_json)
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
  add_executable(lua_scene_tests tests/lua_scene.cpp)
  target_link_libraries(lua_scene_tests PRIVATE lua_console legacy_physics)
  add_test(NAME lua_scene COMMAND lua_scene_tests)
  set_tests_properties(lua_scene PROPERTIES TIMEOUT 15)
endif()

if(BUILD_TESTING)
  add_executable(create_hierarchy_tests tests/create_hierarchy.cpp src/rendering/mesh.cpp)
  target_include_directories(create_hierarchy_tests PRIVATE src)
  target_link_libraries(create_hierarchy_tests PRIVATE legacy_imgui legacy_physics)
  add_test(NAME create_hierarchy COMMAND create_hierarchy_tests)
endif()

if(BUILD_TESTING)
  add_executable(editor_layout_tests tests/editor_layout.cpp)
  target_include_directories(editor_layout_tests PRIVATE src)
  target_link_libraries(editor_layout_tests PRIVATE imguizmo legacy_physics)
  add_test(NAME editor_layout COMMAND editor_layout_tests)
endif()

if(BUILD_TESTING)
  add_executable(asset_streaming_tests tests/asset_streaming.cpp src/rendering/streamed_texture.cpp)
  target_include_directories(asset_streaming_tests PRIVATE src third_party/stb)
  target_link_libraries(asset_streaming_tests PRIVATE legacy_json legacy_thumbnail_data Threads::Threads)
  if(MSVC)
    target_compile_options(asset_streaming_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(asset_streaming_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME asset_streaming COMMAND asset_streaming_tests)
endif()

if(BUILD_TESTING)
  add_executable(collider_menu_tests tests/collider_menu.cpp)
  target_include_directories(collider_menu_tests PRIVATE src)
  if(MSVC)
    target_compile_options(collider_menu_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(collider_menu_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME collider_menu COMMAND collider_menu_tests)
endif()

if(BUILD_TESTING)
  add_executable(model_import_tests tests/model_import.cpp)
  target_link_libraries(model_import_tests PRIVATE legacy_models)
  target_compile_definitions(model_import_tests PRIVATE LEGACY_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
  if(MSVC)
    target_compile_options(model_import_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(model_import_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME model_import COMMAND model_import_tests)
endif()

if(BUILD_TESTING)
  add_executable(search_box_tests tests/search_box.cpp)
  target_include_directories(search_box_tests PRIVATE src)
  target_link_libraries(search_box_tests PRIVATE legacy_imgui)
  if(MSVC)
    target_compile_options(search_box_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(search_box_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME search_box COMMAND search_box_tests)
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
  add_executable(play_mode_tests tests/play_mode.cpp)
  target_include_directories(play_mode_tests PRIVATE src)
  target_link_libraries(play_mode_tests PRIVATE legacy_imgui legacy_physics)
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
  target_link_libraries(asset_trash_tests PRIVATE lua_console legacy_imgui legacy_models)
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
  target_link_libraries(editor_polish_tests PRIVATE lua_console legacy_imgui legacy_models)
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
  if(MSVC)
    target_compile_options(lighting_tests PRIVATE /UNDEBUG)
  else()
    target_compile_options(lighting_tests PRIVATE -UNDEBUG)
  endif()
  add_test(NAME lighting COMMAND lighting_tests)
endif()

# Standalone project tests do not otherwise link an engine subsystem.
if(BUILD_TESTING)
  target_link_libraries(project_io_tests PRIVATE legacy_json)
  target_link_libraries(editor_document_tests PRIVATE legacy_json)
endif()

if(BUILD_TESTING)
  add_executable(asset_thumbnail_tests tests/asset_thumbnails.cpp)
  target_link_libraries(asset_thumbnail_tests PRIVATE legacy_thumbnail_data legacy_json)
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
  target_link_libraries(skybox_tests PRIVATE lua_console legacy_json legacy_imgui)
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
# Remove obsolete shader copies from incremental build directories.
set(legacy_obsolete_shaders lit.frag lit.vert skinning.vert skybox.frag skybox.vert unlit.frag unlit.vert)
set(legacy_obsolete_build_shaders
  "${CMAKE_CURRENT_BINARY_DIR}/data/license/glad.txt"
  "${CMAKE_CURRENT_BINARY_DIR}/data/license/Khronos.txt")
foreach(shader IN LISTS legacy_obsolete_shaders)
  list(APPEND legacy_obsolete_build_shaders "${CMAKE_CURRENT_BINARY_DIR}/data/shaders/${shader}")
endforeach()
add_custom_target(engine_data ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/data" "${CMAKE_CURRENT_BINARY_DIR}/data"
  COMMAND ${CMAKE_COMMAND} -E remove -f ${legacy_obsolete_build_shaders})
add_dependencies(LegacyEngine engine_data)

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
foreach(runtime_target LegacyEngine level_tests)
  if(TARGET ${runtime_target})
    set(legacy_obsolete_runtime_shaders
      "$<TARGET_FILE_DIR:${runtime_target}>/data/license/glad.txt"
      "$<TARGET_FILE_DIR:${runtime_target}>/data/license/Khronos.txt")
    foreach(shader IN LISTS legacy_obsolete_shaders)
      list(APPEND legacy_obsolete_runtime_shaders "$<TARGET_FILE_DIR:${runtime_target}>/data/shaders/${shader}")
    endforeach()
    add_custom_command(TARGET ${runtime_target} POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/data" "$<TARGET_FILE_DIR:${runtime_target}>/data"
      COMMAND ${CMAKE_COMMAND} -E remove -f ${legacy_obsolete_runtime_shaders})
  endif()
endforeach()

# Opt-in CPU benchmark; not part of runtime packaging or the regression suite.
option(LEGACY_BUILD_BENCHMARKS "Build CPU render-setup benchmarks" OFF)
if(LEGACY_BUILD_BENCHMARKS)
  add_executable(render_setup_benchmark benchmarks/render_setup.cpp)
  target_include_directories(render_setup_benchmark PRIVATE src)
  add_executable(model_point_benchmark benchmarks/model_point.cpp)
  target_include_directories(model_point_benchmark PRIVATE src)
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
