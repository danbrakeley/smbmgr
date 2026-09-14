cmake_minimum_required(VERSION 3.21)

project(smbmgr
        VERSION 0.4.0
        DESCRIPTION "GUI tool for manual SMB hard-link deduplication"
        LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Put all executables in one directory, so the Qt runtime windeployqt stages
# there serves every one of them.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Compile a single target's source files concurrently (MSVC defaults to one
# cl.exe per project). Matters most for libsmb2's ~30 .c files and
# smbmgr_core's own sources, both rebuilt from scratch on every CI run.
# Set before FetchContent_MakeAvailable(libsmb2) below so it's inherited.
if(MSVC)
  add_compile_options(/MP)
endif()

# ---------------------------------------------------------------------------
# libsmb2 — pinned to an exact commit.
# ---------------------------------------------------------------------------
include(FetchContent)

# Configure libsmb2 before it is added. Use CACHE ... FORCE rather than plain
# set(): libsmb2's CMakeLists calls cmake_minimum_required(VERSION 3.5), which
# resets CMP0077 to OLD inside its scope, so its option() would otherwise ignore
# a normal variable and default BUILD_SHARED_LIBS back to ON.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build libsmb2 statically" FORCE) # static: nothing to stage at runtime
set(ENABLE_EXAMPLES   OFF CACHE BOOL "" FORCE)   # we don't need its sample programs
set(ENABLE_LIBKRB5    OFF CACHE BOOL "" FORCE)   # app authenticates via NTLMSSP; no Kerberos
set(ENABLE_GSSAPI     OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  libsmb2
  GIT_REPOSITORY https://github.com/sahlberg/libsmb2.git
  GIT_TAG        6723c56fd6f5a597fc04cc091401349f50c00afc
  GIT_SHALLOW    FALSE   # required: a shallow clone can't check out an arbitrary SHA
)
FetchContent_MakeAvailable(libsmb2)

# ---------------------------------------------------------------------------
# Qt — found from the installed kit. CMAKE_PREFIX_PATH is supplied by the
# active CMake preset (see CMakePresets.json).
# ---------------------------------------------------------------------------
find_package(Qt6 REQUIRED COMPONENTS Widgets Network Svg)
qt_standard_project_setup()

# All app code lives in a static library so the test executables under tests/
# link the exact same objects; the app target is just main.cpp.
qt_add_library(smbmgr_core STATIC
  src/core/LinkRunner.cpp
  src/core/LinkRunner.h
  src/core/LogFormat.h
  src/core/Logger.cpp
  src/core/Logger.h
  src/core/MatchConflicts.h
  src/core/MatchPairing.h
  src/core/MatchSearcher.cpp
  src/core/MatchSearcher.h
  src/core/PathUtil.h
  src/core/VersionCompare.h
  src/models/FileFilterProxyModel.cpp
  src/models/FileFilterProxyModel.h
  src/models/FileListModel.cpp
  src/models/FileListModel.h
  src/models/MatchResultsModel.cpp
  src/models/MatchResultsModel.h
  src/smb/SmbSession.cpp
  src/smb/SmbSession.h
  src/smb/SmbTypes.h
  src/ui/AboutDialog.cpp
  src/ui/AboutDialog.h
  src/ui/CheckBoxHeader.cpp
  src/ui/CheckBoxHeader.h
  src/ui/FileBrowserView.cpp
  src/ui/FileBrowserView.h
  src/ui/IconUtil.cpp
  src/ui/IconUtil.h
  src/ui/MainWindow.cpp
  src/ui/MainWindow.h
  src/ui/MatchFinderPanel.cpp
  src/ui/MatchFinderPanel.h
  resources/icons/icons.qrc
)

target_include_directories(smbmgr_core PUBLIC src)

# Feeds the About dialog's version and build-date labels. A custom target
# with no declared OUTPUT, so it re-runs on every build and both values stay
# current across incremental builds (configure-time values would go stale).
# See cmake/GenerateBuildInfo.cmake for how the version is derived.
set(SMBMGR_BUILD_INFO_DIR ${CMAKE_BINARY_DIR}/generated)
set(SMBMGR_BUILD_INFO_HEADER ${SMBMGR_BUILD_INFO_DIR}/BuildInfo.h)
add_custom_target(smbmgr_build_info ALL
  COMMAND ${CMAKE_COMMAND}
          -D DST=${SMBMGR_BUILD_INFO_HEADER}
          -D SRC_DIR=${CMAKE_SOURCE_DIR}
          -D FALLBACK_VERSION=${PROJECT_VERSION}
          -P ${CMAKE_SOURCE_DIR}/cmake/GenerateBuildInfo.cmake
  BYPRODUCTS ${SMBMGR_BUILD_INFO_HEADER}
  COMMENT "Recording build info (version + build date)"
  VERBATIM)
target_include_directories(smbmgr_core PRIVATE ${SMBMGR_BUILD_INFO_DIR})
add_dependencies(smbmgr_core smbmgr_build_info)

# AUTORCC: qt_standard_project_setup() doesn't enable it, and without it
# resources/icons/icons.qrc would be an inert source file instead of going
# through rcc. Because the qrc sits in a static library, the linker may drop
# its self-registration object, so every executable calls
# Q_INIT_RESOURCE(icons) to force it in (src/main.cpp, tests/common/TestMain.h).
set_target_properties(smbmgr_core PROPERTIES AUTORCC ON)

target_link_libraries(smbmgr_core PUBLIC
  Qt6::Widgets
  Qt6::Network   # QHostInfo: async DNS ahead of libsmb2's blocking resolver
  Qt6::Svg       # renders the toolbar/action icons in resources/icons
  smb2
)

# libsmb2's public headers include <winsock2.h>/<windows.h>; without
# WIN32_LEAN_AND_MEAN that drags in the COM headers, which #define `interface`
# and collide with a struct member of that name in libsmb2's dcerpc header.
# NOMINMAX stops windows.h from defining min/max macros that break
# std::min/std::max and numeric_limits<>::max(). PUBLIC because the executables
# include <winsock2.h> themselves (src/main.cpp, tests/common/TestMain.h).
if(WIN32)
  target_compile_definitions(smbmgr_core PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)

  # Executables call WSAStartup themselves (libsmb2 leaves Winsock init to the
  # app); the import library is carried here so they all link it.
  target_link_libraries(smbmgr_core PUBLIC ws2_32)
endif()

if(WIN32)
  # app.rc embeds resources/icons/app/app.ico as the exe's icon (Explorer,
  # taskbar, Start Menu, Alt-Tab); scripts/gen-app-icons.sh regenerates it.
  qt_add_executable(smbmgr src/main.cpp resources/icons/app/app.rc)
else()
  qt_add_executable(smbmgr src/main.cpp)
endif()

# GUI app: no console window on Windows.
set_target_properties(smbmgr PROPERTIES WIN32_EXECUTABLE ON)

target_link_libraries(smbmgr PRIVATE smbmgr_core)

if(WIN32)
  # Stage Qt's runtime DLLs (and plugins) next to the exe after each build so it
  # runs / double-clicks without Qt on PATH. windeployqt auto-detects the build
  # config from the binary.
  add_custom_command(TARGET smbmgr POST_BUILD
    COMMAND Qt6::windeployqt
            --no-translations
            --no-compiler-runtime
            "$<TARGET_FILE:smbmgr>"
    COMMENT "Deploying Qt runtime dependencies next to smbmgr.exe"
    VERBATIM)
endif()

# ---------------------------------------------------------------------------
# Tests (ctest). BUILD_TESTING is ON by default via include(CTest); the suites
# labeled "docker" additionally need the Samba fixture (see tests/docker/).
# EXCLUDE_FROM_ALL keeps the tst_* executables off the default ALL target, so
# an ordinary `cmake --build` (app dev loop, release packaging) only builds
# smbmgr — build the "smbmgr_tests" target first to run the suite.
# ---------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests EXCLUDE_FROM_ALL)
endif()

# ---------------------------------------------------------------------------
# Packaging — Linux .deb via CPack (Windows releases are windeployqt's staged
# bin/ folder, zipped directly by CI; no install()/CPack involved there).
# libsmb2 is linked statically (see BUILD_SHARED_LIBS above), so the only
# runtime dependency to declare is system Qt; CPACK_DEBIAN_PACKAGE_SHLIBDEPS
# derives that Depends: line from the built binary instead of us hardcoding
# per-distro package names (which have shifted across Ubuntu releases, e.g.
# the "t64" suffix from the 24.04 time64 ABI transition).
# ---------------------------------------------------------------------------
if(UNIX AND NOT APPLE)
  if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Install prefix" FORCE)
  endif()

  install(TARGETS smbmgr RUNTIME DESTINATION bin)

  # Desktop entry + hicolor icon set (generated by scripts/gen-app-icons.sh)
  # so smbmgr shows up with its icon in application launchers/menus.
  # hicolor-icon-theme isn't declared as a dependency: it's effectively
  # universal on desktop Linux, and CPACK_DEBIAN_PACKAGE_SHLIBDEPS below only
  # covers shared-library deps.
  install(FILES resources/linux/smbmgr.desktop DESTINATION share/applications)
  install(DIRECTORY resources/icons/app/hicolor/ DESTINATION share/icons/hicolor)

  # AppStream MetaInfo: App Center's local-.deb install card (name + icon
  # above the Install button) is built from this, not from the .desktop file
  # or icon theme directly — without it, App Center shows a generic package
  # icon even though the launcher/menu icon above is already correct.
  install(FILES resources/linux/smbmgr.metainfo.xml DESTINATION share/metainfo)

  set(CPACK_GENERATOR "DEB")
  set(CPACK_PACKAGE_CONTACT "Dan Brakeley <deb@aliencamel.com>")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT") # <name>_<version>_<arch>.deb
  include(CPack)
endif()
