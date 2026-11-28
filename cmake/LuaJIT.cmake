# LuaJIT's supported build uses GNU make (including native MSYS2/MinGW-w64).
# Build in a private copy so separate CMake build trees never share .o files.
if(MSVC OR CMAKE_CROSSCOMPILING)
  message(FATAL_ERROR "LuaJIT requires a native GCC/Clang build; use MinGW-w64 on Windows")
endif()
find_program(LEGACY_LUAJIT_MAKE NAMES gmake make REQUIRED)
FetchContent_Declare(luajit
  GIT_REPOSITORY https://github.com/LuaJIT/LuaJIT.git
  GIT_TAG 3bfb8fa4ab970b36d42d108ead04e92341c49998)
FetchContent_MakeAvailable(luajit)
set(luajit_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/luajit-build")
file(MAKE_DIRECTORY "${luajit_BUILD_DIR}")
file(COPY "${luajit_SOURCE_DIR}/src" "${luajit_SOURCE_DIR}/dynasm"
  DESTINATION "${luajit_BUILD_DIR}")
file(WRITE "${luajit_BUILD_DIR}/.relver" "1795687819\n")
set(luajit_LIBRARY "${luajit_BUILD_DIR}/src/libluajit.a")
set(luajit_SYSTEM_ARGS)
if(WIN32)
  list(APPEND luajit_SYSTEM_ARGS TARGET_SYS=Windows)
endif()
add_custom_command(OUTPUT "${luajit_LIBRARY}"
  COMMAND "${LEGACY_LUAJIT_MAKE}" -C "${luajit_BUILD_DIR}/src" clean
  COMMAND "${LEGACY_LUAJIT_MAKE}" -C "${luajit_BUILD_DIR}/src" -j1
    "CC=${CMAKE_C_COMPILER}" "AR=${CMAKE_AR}"
    "TARGET_AR=${CMAKE_AR} rcus" BUILDMODE=static
    "XCFLAGS=-DLUAJIT_ENABLE_LUA52COMPAT -DLUAJIT_ENABLE_CHECKHOOK -DLUAJIT_DISABLE_FFI"
    ${luajit_SYSTEM_ARGS} libluajit.a
  DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/LuaJIT.cmake"
  COMMENT "Building embedded LuaJIT 2.1 (static, sandboxed)" VERBATIM)
add_custom_target(legacy_luajit_build DEPENDS "${luajit_LIBRARY}")
add_library(legacy_lua STATIC IMPORTED GLOBAL)
set_target_properties(legacy_lua PROPERTIES
  IMPORTED_LOCATION "${luajit_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${luajit_BUILD_DIR}/src")
add_dependencies(legacy_lua legacy_luajit_build)
if(UNIX)
  target_link_libraries(legacy_lua INTERFACE m ${CMAKE_DL_LIBS})
endif()
