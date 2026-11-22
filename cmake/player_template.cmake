# Mark only standalone player templates, never editor executables. Reapply this
# after stripping distribution copies because strip may remove the file overlay.
set(marker "LEGACY_PLAYER_TEMPLATE_000000002")
file(SIZE "${PLAYER}" size)
set(tail "")
if(size GREATER_EQUAL 32)
  math(EXPR offset "${size} - 32")
  file(READ "${PLAYER}" tail OFFSET ${offset} LIMIT 32)
endif()
if(NOT tail STREQUAL marker)
  file(APPEND "${PLAYER}" "${marker}")
endif()
