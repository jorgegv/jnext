# JNEXT_QT_MAJOR (5 or 6) is decided once in the root CMakeLists (GH #108
# Phase A); Qt6 is the default whenever present.
find_package(Qt${JNEXT_QT_MAJOR} REQUIRED COMPONENTS Widgets)

file(GLOB_RECURSE SOURCES "*.cpp")
file(GLOB_RECURSE HEADERS "*.h")

add_library(jnext_debugger STATIC ${SOURCES} ${HEADERS})
set_target_properties(jnext_debugger PROPERTIES AUTOMOC ON)
target_include_directories(jnext_debugger PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(jnext_debugger PUBLIC
    Qt${JNEXT_QT_MAJOR}::Widgets
    jnext_debug
    jnext_core
    jnext_cpu
    jnext_memory
    jnext_video
    jnext_audio
    jnext_port
    jnext_peripheral
    jnext_input
    spdlog::spdlog)
