# JNEXT_QT_MAJOR (5 or 6) is decided once in the root CMakeLists (GH #108
# Phase A); Qt6 is the default whenever present.
find_package(Qt${JNEXT_QT_MAJOR} REQUIRED COMPONENTS Widgets)
find_package(SDL3 REQUIRED)
# std::thread in sdcard_download_dialog.cpp (SD-image busy dialog) — link the
# platform threads library explicitly so the MinGW/macOS release legs link it
# too (glibc >= 2.34 folds pthread into libc, but the cross builds may not).
find_package(Threads REQUIRED)

# CONFIGURE_DEPENDS: re-check this glob at BUILD time, so a newly added
# source is compiled without a manual reconfigure (GH #252 shipped as an
# undefined reference). Full rationale in the root CMakeLists.txt.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "*.cpp")
list(FILTER SOURCES EXCLUDE REGEX "${JNEXT_GENERATED_DIR_REGEX}")
add_library(jnext_gui STATIC ${SOURCES})

set_target_properties(jnext_gui PROPERTIES AUTOMOC ON)

target_link_libraries(jnext_gui PUBLIC
    Qt${JNEXT_QT_MAJOR}::Widgets
    jnext_toolbar_icons
    ${JNEXT_SDL3_TARGET}
    spdlog::spdlog
    Threads::Threads
    jnext_core jnext_cpu jnext_memory
    jnext_video jnext_audio jnext_port
    jnext_peripheral jnext_input
    jnext_platform jnext_save jnext_debug)

target_include_directories(jnext_gui PUBLIC ${CMAKE_SOURCE_DIR}/src "${CMAKE_BINARY_DIR}/generated")

# GH #363 — the desktop's light/dark preference is read from the XDG desktop
# portal over D-Bus (gui/desktop_color_scheme.cpp). Linux + Qt 6 only: QtDBus is
# part of qtbase there, so this adds no package. Windows and macOS report the
# system scheme through Qt itself.
if(UNIX AND NOT APPLE AND JNEXT_QT_MAJOR EQUAL 6)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
    target_link_libraries(jnext_gui PUBLIC Qt6::DBus)
    target_compile_definitions(jnext_gui PUBLIC JNEXT_HAVE_QTDBUS)
endif()

if(ENABLE_DEBUGGER)
    target_link_libraries(jnext_gui PUBLIC jnext_debugger)
    target_compile_definitions(jnext_gui PUBLIC ENABLE_DEBUGGER)
endif()

# mac_cursor_warp.cpp (issue #303): CGWarpMouseCursorPosition and
# CGAssociateMouseAndMouseCursorPosition live in CoreGraphics.
if(APPLE)
    target_link_libraries(jnext_gui PUBLIC "-framework CoreGraphics")
endif()
