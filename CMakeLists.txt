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
#
# JNEXT_ENABLE_LTO lets a specific build opt out of LTO. It exists for the
# Flatpak build ONLY: the org.kde.Sdk GCC miscompiles the QApplication init
# path under whole-program LTO (SIGSEGV at GUI launch, inside the QApplication
# constructor — a null-sender screenAdded emission). This was bisected to the
# LTO *process* in that toolchain, not any single unit or transform (per-target
# LTO, -fno-devirtualize/-fno-ipa-icf, and -O2 all still crashed); only fully
# disabling LTO fixes it. The native host GCC does not miscompile, so every
# other build (native, DEB, RPM, gui-release) keeps LTO and its full perf. The
# option defaults ON and is flipped OFF solely in packaging/flatpak/*.yml.
# Full analysis + bisection record: doc/FLATPAK-LTO-PROBLEMS.md.
option(JNEXT_ENABLE_LTO "Enable LTO/IPO for Release builds (Flatpak sets OFF; see comment)" ON)
include(CheckIPOSupported)
check_ipo_supported(RESULT JNEXT_IPO_SUPPORTED OUTPUT JNEXT_IPO_ERROR LANGUAGES C CXX)
if(JNEXT_IPO_SUPPORTED AND JNEXT_ENABLE_LTO)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    message(STATUS "IPO/LTO supported — enabled for Release builds only")
elseif(NOT JNEXT_ENABLE_LTO)
    message(STATUS "IPO/LTO disabled via JNEXT_ENABLE_LTO=OFF (Flatpak toolchain workaround)")
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
# macOS packaging only (`make package-macos`); see the APPLE block further down.
option(MACOS_APP_BUNDLE "Build a relocatable jnext.app bundle (macOS packaging)" OFF)

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

# Issue #43 — the CLI parse loop switches over cli::OptId with no `default:`
# arm, so an option added to cli::OPTIONS but never given a parser arm is a
# COMPILE ERROR here rather than a flag that silently parses to nothing. This
# is what couples the option table to the code that consumes it; the man page
# alignment (cli_options_test) is the other half.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(jnext PRIVATE -Werror=switch)
endif()

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

# macOS: build a real jnext.app bundle rather than a bare Mach-O executable
# (GH #46). A bare executable has nowhere to carry its dependencies, so the
# shipped binary kept absolute /opt/homebrew/... install names and aborted on
# any Mac without the exact same Homebrew formulae. The bundle gives a
# Contents/Frameworks to deploy into and an @executable_path to point at; the
# deployment itself happens at install time, further down.
#
# OFF by default, and set by `make package-macos` alone, because a bundle target
# moves the binary to build/<dir>/jnext.app/Contents/MacOS/jnext — which would
# hide it from regression.sh and every other harness that looks for
# build/gui-release/jnext. Only the shipped package needs to be relocatable; a
# developer's own Mac build keeps the plain executable and its Homebrew libs.
if(APPLE AND MACOS_APP_BUNDLE)
    set_target_properties(jnext PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "jnext"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.zxjogv.jnext"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "jnext.icns"
        MACOSX_BUNDLE_COPYRIGHT "GPLv3")
    # The icon must be INSIDE the bundle for MACOSX_BUNDLE_ICON_FILE to resolve.
    target_sources(jnext PRIVATE ${CMAKE_SOURCE_DIR}/packaging/assets/jnext.icns)
    set_source_files_properties(${CMAKE_SOURCE_DIR}/packaging/assets/jnext.icns
        PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
    # Let the bundled dylibs in Contents/Frameworks be found at run time.
    set_target_properties(jnext PROPERTIES
        INSTALL_RPATH "@executable_path/../Frameworks"
        BUILD_WITH_INSTALL_RPATH FALSE)
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

# Force a non-executable stack — general hardening; jnext never needs an
# executable stack. (Historically this also silenced a GNU ld warning about
# the objcopy'd nextboot.rom blob lacking a .note.GNU-stack marker; the ROM is
# now a normal compiled C object that carries the marker, so the flag is pure
# hardening today.)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(jnext PRIVATE -Wl,-z,noexecstack)
endif()

# Windows main-thread stack reserve. MinGW defaults to 2 MB; jnext is built
# assuming the generous stack Linux gives by default (8 MB), and an early
# large-frame code path overflows the 2 MB reserve on startup (observed as an
# immediate stack-overflow under wine/Windows, in __chkstk before main runs).
# Reserve 16 MB to match Linux's headroom. Harmless on real Windows — the
# reserve is address space only; pages commit lazily.
if(WIN32)
    target_link_options(jnext PRIVATE -Wl,--stack,16777216)
    # Link as a GUI-subsystem binary so a double-click launch from Explorer
    # opens no console window. CLI/headless stdout/stderr still work when the
    # exe is run from an existing console: main() calls
    # AttachConsole(ATTACH_PARENT_PROCESS) and reopens the stdio streams onto it
    # (win_attach_parent_console(), src/platform/win_console.cpp). Qt6::EntryPointPrivate
    # (linked below) supplies the WinMain -> main() bridge the GUI subsystem needs.
    target_link_options(jnext PRIVATE -Wl,--subsystem,windows)
endif()

if(ENABLE_QT_UI)
    target_link_libraries(jnext PRIVATE jnext_gui)
    target_compile_definitions(jnext PRIVATE ENABLE_QT_UI)
    # On Windows (MinGW cross-build) the Qt6 CRT startup calls WinMain() rather
    # than main(); Qt6::EntryPointPrivate supplies the WinMain -> main() bridge.
    # Without it the link fails with "undefined reference to WinMain". jnext.exe
    # is linked as a GUI-subsystem binary (see the --subsystem,windows option
    # above); --headless/--version stdout/stderr still reach a parent console via
    # win_attach_parent_console(). Windows-only (a no-op on the Linux build).
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

# macdeployqt lives next to qmake in the Qt installation, which is not on PATH
# for a Homebrew qt@6. Resolve it from the imported Qt6::qmake target so the
# lookup follows CMAKE_PREFIX_PATH rather than a hard-coded brew path.
# Left empty (not fatal) when Qt is off — the install-time check below is what
# turns that into a loud failure, and only for a build that is actually packaged.
# NOTE the JNEXT_ prefix: Qt6Core's own macros run
# `find_program(MACDEPLOYQT_EXECUTABLE ...)` for their deploy-script support, and
# that runs first (via find_package(Qt6 ... Widgets) in src/gui, src/debugger).
# A cache variable already populated is never re-searched, so an unprefixed name
# would silently be Qt's result and this lookup would be dead code.
if(APPLE AND MACOS_APP_BUNDLE AND ENABLE_QT_UI)
    find_package(Qt6 REQUIRED COMPONENTS Core)
    get_target_property(_qt_qmake_location Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qt_qmake_location}" DIRECTORY)
    find_program(JNEXT_MACDEPLOYQT macdeployqt HINTS "${_qt_bin_dir}")
endif()
# find_program leaves JNEXT_MACDEPLOYQT-NOTFOUND behind when it fails, and
# that string is truthy once interpolated into the install script below — it
# would be run as a command name. Normalise a failed lookup to empty here, where
# CMake's if() still understands -NOTFOUND.
if(NOT JNEXT_MACDEPLOYQT)
    set(JNEXT_MACDEPLOYQT "")
endif()

if(APPLE AND MACOS_APP_BUNDLE)

# macOS ships a single relocatable jnext.app at the root of the .dmg (GH #46),
# not an FHS tree — hence the separate branch. The freedesktop .desktop /
# metainfo / hicolor icon files below are meaningless on macOS (the bundle's
# Info.plist and Contents/Resources/jnext.icns do that job) and are not shipped.
install(TARGETS jnext BUNDLE DESTINATION .)
install(FILES LICENSE README.md ChangeLog USAGE.md DESTINATION .)
install(DIRECTORY doc/user-guide DESTINATION .)

# Deploy the dependency closure INTO the bundle and prove it worked.
#
# Without this the installed binary keeps the absolute /opt/homebrew/... install
# names it linked against, and aborts on any Mac lacking those exact formulae —
# the GH #46 report ("Library not loaded: .../libpng16.16.dylib"). macdeployqt
# copies the Qt frameworks, the Cocoa platform plugin and the non-Qt dylibs into
# Contents/Frameworks and rewrites the install names to @executable_path.
#
# verify-bundle.sh is not belt-and-braces: it is the actual gate. macdeployqt's
# coverage of non-Qt dependencies cannot be checked on the Linux dev host, so
# the install FAILS LOUDLY if any dependency still points outside the bundle,
# rather than producing another .dmg that only runs on the machine that built it.
#
# macdeployqt's EXIT STATUS IS USELESS: its main() ends in an unconditional
# `return 0`, and its LogError() is a qDebug() wrapper that sets no error state.
# Observed live (run 29852867578): it printed ERROR: Cannot resolve rpath
# "@rpath/QtPdf..." and exited 0 — and an unresolvable framework is silently
# dropped from its copy worklist, leaving the plugin that needed it in the
# bundle, unusable.
#
# Deliberately NOT handled by grepping its output for "ERROR:". That would gate
# the build on the wording of a tool's log lines, and would fail on exactly the
# cases prune-broken-plugins.sh is designed to resolve. The state of the BUNDLE
# is checked instead: every reference must resolve to a file that is present
# (verify-bundle.sh), and anything unusable is removed first. That holds no
# matter what macdeployqt prints or returns.
install(CODE "
    set(_app \"\${CMAKE_INSTALL_PREFIX}/jnext.app\")
    if(NOT \"${JNEXT_MACDEPLOYQT}\" STREQUAL \"\")
        message(STATUS \"Deploying dependencies into \${_app}\")
        execute_process(
            COMMAND \"${JNEXT_MACDEPLOYQT}\" \"\${_app}\" -always-overwrite
            TIMEOUT 900
            RESULT_VARIABLE _rc
            OUTPUT_VARIABLE _out
            ERROR_VARIABLE _err)
        message(STATUS \"\${_out}\${_err}\")
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR \"macdeployqt failed or timed out (result: \${_rc})\")
        endif()
        # Re-sign. Every install_name_tool rewrite macdeployqt just performed
        # INVALIDATES the code signature of the file it touched, and on Apple
        # Silicon the kernel refuses to map an invalidly-signed page — the app is
        # killed at launch. This is Qt's own QTBUG-138019; their CMake deploy API
        # was fixed by always passing -codesign=, which raw macdeployqt does not.
        # Ad-hoc identity (-) is enough to make the signature VALID, which is what
        # loading requires; it is not notarisation and does not pretend to be.
        # Remove plugins macdeployqt copied but could not complete. On a
        # split Homebrew Qt it cannot resolve some plugins' own frameworks
        # (QtSvg, QtPdf, QtVirtualKeyboard) and ships them unusable; jnext
        # needs none of them. Runs BEFORE signing so the signature covers the
        # final contents.
        execute_process(
            COMMAND bash \"${CMAKE_SOURCE_DIR}/packaging/macos/prune-broken-plugins.sh\" \"\${_app}\"
            TIMEOUT 300
            RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR \"prune-broken-plugins failed or timed out (result: \${_rc}) — see above\")
        endif()

        # Copy in anything the bundle references but does not contain.
        # macdeployqt missed libsharpyuv on run 29856062806 while copying the
        # three libwebp libraries that load it. Runs after pruning (fewer files
        # to complete) and before signing (the signature must cover the result).
        execute_process(
            COMMAND bash \"${CMAKE_SOURCE_DIR}/packaging/macos/complete-closure.sh\" \"\${_app}\"
            TIMEOUT 300
            RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR \"complete-closure failed or timed out (result: \${_rc})\")
        endif()

        message(STATUS \"Ad-hoc signing \${_app}\")
        execute_process(
            COMMAND codesign --force --deep --sign - \"\${_app}\"
            TIMEOUT 600
            RESULT_VARIABLE _rc)
        if(NOT _rc EQUAL 0)
            message(FATAL_ERROR \"codesign failed or timed out (result: \${_rc})\")
        endif()
    else()
        message(WARNING \"macdeployqt not found — the bundle will not be self-contained\")
    endif()
    execute_process(
        COMMAND bash \"${CMAKE_SOURCE_DIR}/packaging/macos/verify-bundle.sh\" \"\${_app}\"
        TIMEOUT 300
        RESULT_VARIABLE _rc)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR
            \"jnext.app is not self-contained, or the check timed out (result: \${_rc}) — see above. \"
            \"A macOS package build needs -DENABLE_QT_UI=ON and macdeployqt on PATH.\")
    endif()
")

else()

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

install(FILES LICENSE README.md ChangeLog USAGE.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR})

# User guide. Committed pre-rendered (from src/doc/user-guide via
# `make docs-userguide`), so installing needs no doc toolchain — same rationale
# as the man page below. Ships the whole rendered tree so the guide can be
# opened straight off disk: <docdir>/user-guide/index.html. mkdocs is
# configured with use_directory_urls: false precisely so those links resolve
# under file:// without a web server.
install(DIRECTORY doc/user-guide
    DESTINATION ${CMAKE_INSTALL_DOCDIR})

# Man page. Committed pre-generated (from doc/man/jnext.1.md via `make docs`),
# so installing needs no doc toolchain. The packaging formats compress it
# themselves (rpm brp-compress, dh_compress), hence the jnext.1* globs in
# their file lists.
install(FILES doc/man/jnext.1
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)

endif()  # APPLE AND MACOS_APP_BUNDLE

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
if(APPLE AND MACOS_APP_BUNDLE)
    # A .dmg is a drag-and-drop volume, not a filesystem root: the bundle and
    # the docs sit at the top level. The /usr prefix used by the Linux packages
    # would bury jnext.app under usr/ inside the disk image (GH #46 — what the
    # reporter dragged to /Applications was that buried bare binary).
    set(CPACK_PACKAGING_INSTALL_PREFIX "/")
else()
    set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
endif()
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
# Do NOT embed the licence as a click-through Software License Agreement.
# CPack turns CPACK_RESOURCE_FILE_LICENSE into one for the DragNDrop generator,
# and an SLA makes the image un-mountable without a human: `hdiutil attach`
# answers "attach canceled" and mounts nothing (run 29854836676), which broke
# our own verification. It is also a poor first impression for a GPL app — the
# full LICENSE ships inside the .dmg where anyone can read it.
set(CPACK_DMG_SLA_USE_RESOURCE_FILE_LICENSE OFF)

include(CPack)
