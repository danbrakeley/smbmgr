# Writes BuildInfo.h with APP_VERSION and APP_BUILD_DATE. Invoked as a
# build-time (not configure-time) step -- see the "smbmgr_build_info" custom
# target in the top-level CMakeLists.txt -- so both values reflect the
# actual build, not just the last `cmake --preset` configure: the build date
# would otherwise go stale across incremental builds, and the version
# wouldn't pick up new commits/tags or a newly-dirtied working tree.
#
# The header is only replaced when its content changes, so a no-op build
# doesn't recompile AboutDialog.cpp and relink every executable. That is also
# why the build date has day (not minute) resolution.
#
# Expects on the command line (via -D):
#   DST              - path to the header to write
#   SRC_DIR          - repo root, to run git commands against
#   FALLBACK_VERSION - CMakeLists.txt's project() VERSION, used verbatim
#                       when no "vMAJOR.MINOR.PATCH" tag is reachable from
#                       HEAD (e.g. a shallow clone with no tags fetched)
#
# APP_VERSION is derived from the most recent "vMAJOR.MINOR.PATCH"-style git
# tag reachable from HEAD (README's release process is the source of these
# tags): the "v" is stripped, then "-{short_hash}" is appended if HEAD is
# not exactly that tag, then "-dev" is appended if the working tree has
# uncommitted changes (staged or not).

string(TIMESTAMP SMBMGR_BUILD_DATE "%Y-%m-%d" UTC)

set(SMBMGR_VERSION "${FALLBACK_VERSION}")

find_program(SMBMGR_GIT_EXECUTABLE git)
if(SMBMGR_GIT_EXECUTABLE)
  execute_process(
    COMMAND "${SMBMGR_GIT_EXECUTABLE}" describe --tags --match "v[0-9]*.[0-9]*.[0-9]*" --abbrev=0
    WORKING_DIRECTORY "${SRC_DIR}"
    OUTPUT_VARIABLE SMBMGR_LAST_TAG
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE SMBMGR_DESCRIBE_RESULT)

  if(SMBMGR_DESCRIBE_RESULT EQUAL 0 AND SMBMGR_LAST_TAG MATCHES "^v(.+)$")
    set(SMBMGR_VERSION "${CMAKE_MATCH_1}")

    execute_process(
      COMMAND "${SMBMGR_GIT_EXECUTABLE}" rev-list "${SMBMGR_LAST_TAG}..HEAD" --count
      WORKING_DIRECTORY "${SRC_DIR}"
      OUTPUT_VARIABLE SMBMGR_COMMITS_SINCE_TAG
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)

    if(NOT SMBMGR_COMMITS_SINCE_TAG STREQUAL "0")
      execute_process(
        COMMAND "${SMBMGR_GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${SRC_DIR}"
        OUTPUT_VARIABLE SMBMGR_SHORT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
      if(SMBMGR_SHORT_HASH)
        set(SMBMGR_VERSION "${SMBMGR_VERSION}-${SMBMGR_SHORT_HASH}")
      endif()
    endif()
  endif()

  execute_process(
    COMMAND "${SMBMGR_GIT_EXECUTABLE}" status --porcelain
    WORKING_DIRECTORY "${SRC_DIR}"
    OUTPUT_VARIABLE SMBMGR_GIT_STATUS
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if(NOT SMBMGR_GIT_STATUS STREQUAL "")
    set(SMBMGR_VERSION "${SMBMGR_VERSION}-dev")
  endif()
endif()

get_filename_component(SMBMGR_BUILD_INFO_DIR "${DST}" DIRECTORY)
file(MAKE_DIRECTORY "${SMBMGR_BUILD_INFO_DIR}")
file(WRITE "${DST}.tmp"
  "#pragma once\n"
  "#define APP_VERSION \"${SMBMGR_VERSION}\"\n"
  "#define APP_BUILD_DATE \"${SMBMGR_BUILD_DATE}\"\n")
file(COPY_FILE "${DST}.tmp" "${DST}" ONLY_IF_DIFFERENT)
file(REMOVE "${DST}.tmp")
