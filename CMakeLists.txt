cmake_minimum_required(VERSION 3.16)

# Read version from version.yaml (single source of truth).
# Re-run CMake automatically when version.yaml changes so the generated
# build/generated/version.h (and JNEXT_VERSION_STRING baked into the binary)
# stay in sync with `make bump-*`.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/version.yaml")
include(cmake/GenerateVersion.cmake)
project(jnext VERSION "${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}" LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Build-type safety net (Task 27 T0). `cmake -B build` with no
# -DCMAKE_BUILD_TYPE used to yield an -O0 binary ~5x slower than Release —
# a trap for anyone measuring performance on build/jnext. Default to
# RelWithDebInfo: optimized (-O2) but still debuggable.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    message(STATUS "CMAKE_BUILD_TYPE not set — defaulting to RelWithDebInfo")
endif()

# Frame pointers on RelWithDebInfo ONLY (the dev/profiling build): needed
# for usable `perf record -g` call graphs (Task 27 P1). NOT on Release —
# measured at 5.8% cost there (independent review, 2026-07-15), an
# unacceptable permanent tax on the user-facing binary. Profiling the
# Release binary uses `perf record --call-graph dwarf` instead.
if(NOT MSVC)
    string(APPEND CMAKE_C_FLAGS_RELWITHDEBINFO   " -fno-omit-frame-pointer")
    string(APPEND CMAKE_CXX_FLAGS_RELWITHDEBINFO " -fno-omit-frame-pointer")
endif()

# LTO/IPO for the Release (gui-release) build ONLY (Task 27 B1). The 14
# per-subsystem static libs mean every hot cross-TU call — CPU→Mmu::read,
# CPU→PortDispatch — is un-inlinable and un-devirtualisable at compile time.
# LTO lets the linker inline/devirtualise across those library boundaries.
# Release-only (CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE): RelWithDebInfo
# (the dev/profiling binary) and Debug stay LTO-free so dev iteration and
# `perf` call-graphs are unaffected. Guarded by check_ipo_supported() so a
# toolchain without working LTO configures and builds exactly as before.
include(CheckIPOSupported)
check_ipo_supported(RESULT JNEXT_IPO_SUPPORTED OUTPUT JNEXT_IPO_ERROR LANGUAGES C CXX)
if(JNEXT_IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    message(STATUS "IPO/LTO supported — enabled for Release builds only")
else()
    message(STATUS "IPO/LTO not supported by toolchain — skipping (${JNEXT_IPO_ERROR})")
endif()

# ccache — makes the mandatory `make clean` + full-rebuild discipline cheap.
# Reviewers rebuild the whole tree several times per review (revert a fix,
# rebuild, run the suite, restore, rebuild, run); with ccache the revert and
# restore builds are near-total cache hits. Guarded: a machine without ccache
# configures and builds exactly as before. Set USE_CCACHE=OFF to opt out.
option(USE_CCACHE "Use ccache as compiler launcher when available" ON)
if(USE_CCACHE)
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        message(STATUS "ccache found (${CCACHE_PROGRAM}) — using it as compiler launcher")
        set(CMAKE_C_COMPILER_LAUNCHER   "${CCACHE_PROGRAM}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
    else()
        message(STATUS "ccache not found — compiling without a launcher")
    endif()
endif()

# Initialize git submodules (e.g. third_party/spdlog) if not already present
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    option(GIT_SUBMODULE "Update submodules during build" ON)
    if(GIT_SUBMODULE)
        message(STATUS "Submodule update")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE GIT_SUBMOD_RESULT)
        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
            message(WARNING "git submodule update failed: ${GIT_SUBMOD_RESULT}")
        endif()
    endif()
endif()

option(ENABLE_TESTS    "Build unit tests"       ON)
option(ENABLE_DEBUGGER "Include Qt debugger UI" ON)
option(ENABLE_QT_UI    "Build Qt6 native UI"    OFF)
option(CYCLE_ACCURATE  "28 MHz cycle-accurate"  OFF)
option(STATIC_BUILD    "Link statically (requires static SDL2/Qt6 builds)" OFF)

# Static build: prefer static libraries and link C++ runtime statically.
if(STATIC_BUILD)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ${CMAKE_FIND_LIBRARY_SUFFIXES})
    set(BUILD_SHARED_LIBS OFF)
    if(NOT MSVC)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
    endif()
endif()

find_package(SDL2 REQUIRED)
find_package(ZLIB REQUIRED)
find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

# spdlog — vendored as git submodule, header-only mode.
add_subdirectory(third_party/spdlog EXCLUDE_FROM_ALL)

# ZOT TZX/TAP player by antirez — MIT License.
add_subdirectory(third_party/zot EXCLUDE_FROM_ALL)

# input and debug must come before core: emulator.h includes input/keyboard.h
# and debug/debug_state.h; jnext_core explicitly links jnext_input and jnext_debug.
# Task 21: jnext_profiler is linked by jnext_core (Emulator owns a Profiler),
# so the profiler target must exist before core's add_subdirectory. The
# profiler in turn includes memory/mmu.h, so jnext_memory must come first.
add_subdirectory(src/input)
add_subdirectory(src/debug)
add_subdirectory(src/memory)
add_subdirectory(src/profiler)
add_subdirectory(src/core)
add_subdirectory(src/cpu)
add_subdirectory(src/video)
add_subdirectory(src/audio)
add_subdirectory(src/port)
add_subdirectory(src/peripheral)
add_subdirectory(src/platform)
add_subdirectory(src/save)

if(ENABLE_QT_UI)
    add_subdirectory(src/gui)
endif()

if(ENABLE_DEBUGGER)
    add_subdirectory(src/debugger)
endif()

add_executable(jnext src/main.cpp)

# GCC emits a spurious -Wstringop-overflow inside the vendored fmt bundled in
# spdlog (fmt/bundled/format.h write2digits(): "writing 2 bytes into a region
# of size 0"). It is a known GCC/fmt false positive in third-party code we
# don't own. Because Release builds enable LTO/IPO (Task 27 B1), it surfaces at
# the LTO *link* step — not a per-TU compile — so it must be silenced on both
# the compile and link of the jnext target (GCC only). Our own code keeps the
# warning; only this third-party false positive is suppressed.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(jnext PRIVATE -Wno-stringop-overflow)
    target_link_options(jnext PRIVATE -Wno-stringop-overflow)
endif()

# Windows: embed the application icon into jnext.exe via a resource script
# compiled by windres. jnext.rc references "jnext.ico" relative to the assets
# dir (added to the RC include path). No-op on other platforms.
if(WIN32)
    enable_language(RC)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/packaging/assets/jnext.rc
        PROPERTIES INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}/packaging/assets)
    target_sources(jnext PRIVATE ${CMAKE_SOURCE_DIR}/packaging/assets/jnext.rc)
endif()

# SDL2 linking: prefer SDL2-static when building statically.
if(STATIC_BUILD AND TARGET SDL2::SDL2-static)
    set(SDL2_LINK_TARGET SDL2::SDL2-static)
else()
    set(SDL2_LINK_TARGET SDL2::SDL2)
endif()

target_link_libraries(jnext PRIVATE
    jnext_core jnext_cpu jnext_memory
    jnext_video jnext_audio jnext_port
    jnext_peripheral jnext_input
    jnext_platform jnext_save jnext_debug
    jnext_profiler
    ${SDL2_LINK_TARGET})
target_include_directories(jnext PRIVATE src "${CMAKE_BINARY_DIR}/generated")

# nextboot_rom.o is an objcopy'd binary blob (roms/nextboot.rom) with no
# .note.GNU-stack marker, so GNU ld conservatively marks the whole binary's
# stack executable and warns. The blob is inert data, so force a
# non-executable stack — silences the warning and hardens the binary.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(jnext PRIVATE -Wl,-z,noexecstack)
endif()

if(ENABLE_QT_UI)
    target_link_libraries(jnext PRIVATE jnext_gui)
    target_compile_definitions(jnext PRIVATE ENABLE_QT_UI)
    # On Windows (MinGW cross-build) the Qt6 CRT startup calls WinMain() rather
    # than main(); Qt6::EntryPointPrivate supplies the WinMain -> main() bridge.
    # Without it the link fails with "undefined reference to WinMain". The
    # resulting jnext.exe is a console-subsystem binary, so --headless/--version
    # stdout/stderr work. Windows-only (a no-op on the Linux build).
    if(WIN32)
        find_package(Qt6 REQUIRED COMPONENTS Core)   # provides the entry point
        target_link_libraries(jnext PRIVATE Qt6::EntryPointPrivate)
    endif()
endif()

if(ENABLE_DEBUGGER)
    target_link_libraries(jnext PRIVATE jnext_debugger)
    target_compile_definitions(jnext PRIVATE ENABLE_DEBUGGER)
endif()

if(ENABLE_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

# --- Packaging (Task 67) ------------------------------------------------
# install() rules + CPack config. Additive only: does not affect
# `make gui-release` / `make unit-test` / plain `cmake --build`.
include(GNUInstallDirs)

install(TARGETS jnext RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(FILES packaging/assets/io.github.zxjogv.jnext.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/assets/io.github.zxjogv.jnext.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES packaging/assets/io.github.zxjogv.jnext.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps)
if(EXISTS "${CMAKE_SOURCE_DIR}/packaging/assets/io.github.zxjogv.jnext.png")
    # 512x512 raster icon (the scalable SVG above covers every other size).
    install(FILES packaging/assets/io.github.zxjogv.jnext.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/512x512/apps)
endif()

install(FILES LICENSE USAGE.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR})

# CPack: source-agnostic TGZ/DEB/RPM package generation (`cpack -G <gen>`
# from the build dir). Runtime deps below are the direct link-time
# libraries (see packaging/README.md for how they were derived); the
# rest of each binary's shared-lib closure is picked up by the
# native dpkg-shlibdeps / rpm auto-requires generators.
set(CPACK_PACKAGE_NAME "jnext")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "ZXjogv")
set(CPACK_PACKAGE_CONTACT "zx@jogv.es")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Real-time ZX Spectrum Next emulator with an integrated debugger")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/jorgegv/jnext")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_GENERATOR "TGZ")

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "ZXjogv <zx@jogv.es>")
set(CPACK_DEBIAN_PACKAGE_SECTION "games")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
# Name the .deb the Debian way: <name>_<version>-<release>_<arch>.deb
# (DEB-DEFAULT), instead of CPack's default "jnext-<version>-Linux.deb".
set(CPACK_DEBIAN_FILE_NAME "DEB-DEFAULT")

set(CPACK_RPM_PACKAGE_LICENSE "GPLv3")
set(CPACK_RPM_PACKAGE_GROUP "Amusements/Emulators")
set(CPACK_RPM_PACKAGE_AUTOREQ ON)
# Name the .rpm the RPM way: <name>-<version>-<release>.<arch>.rpm
# (RPM-DEFAULT), instead of CPack's default "jnext-<version>-Linux.rpm".
set(CPACK_RPM_FILE_NAME "RPM-DEFAULT")

# macOS: icon for the .app/.dmg produced by the Bundle/DragNDrop generators.
# Unverified on this Linux dev host (macOS packaging is CI/Mac-only).
set(CPACK_BUNDLE_NAME "jnext")
set(CPACK_BUNDLE_ICON "${CMAKE_SOURCE_DIR}/packaging/assets/jnext.icns")
set(CPACK_DMG_VOLUME_NAME "jnext")

include(CPack)
