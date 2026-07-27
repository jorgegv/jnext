# JNEXT_QT_MAJOR (5 or 6) is decided once in the root CMakeLists (GH #108
# Phase A); Qt6 is the default whenever present.
find_package(Qt${JNEXT_QT_MAJOR} REQUIRED COMPONENTS Widgets)
find_package(SDL2 REQUIRED)
# std::thread in sdcard_download_dialog.cpp (SD-image busy dialog) — link the
# platform threads library explicitly so the MinGW/macOS release legs link it
# too (glibc >= 2.34 folds pthread into libc, but the cross builds may not).
find_package(Threads REQUIRED)

file(GLOB_RECURSE SOURCES "*.cpp")
add_library(jnext_gui STATIC ${SOURCES})

set_target_properties(jnext_gui PROPERTIES AUTOMOC ON)

target_link_libraries(jnext_gui PUBLIC
    Qt${JNEXT_QT_MAJOR}::Widgets
    SDL2::SDL2
    spdlog::spdlog
    Threads::Threads
    jnext_core jnext_cpu jnext_memory
    jnext_video jnext_audio jnext_port
    jnext_peripheral jnext_input
    jnext_platform jnext_save)

target_include_directories(jnext_gui PUBLIC ${CMAKE_SOURCE_DIR}/src "${CMAKE_BINARY_DIR}/generated")

if(ENABLE_DEBUGGER)
    target_link_libraries(jnext_gui PUBLIC jnext_debugger)
    target_compile_definitions(jnext_gui PUBLIC ENABLE_DEBUGGER)
endif()
