file(GLOB_RECURSE SOURCES "*.cpp")
add_library(jnext_core STATIC ${SOURCES})
target_include_directories(jnext_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
# emulator.h includes input/keyboard.h; Keyboard symbols live in jnext_input.
# src/input is added before src/core in the root CMakeLists so this target exists.
target_link_libraries(jnext_core PUBLIC jnext_input jnext_peripheral jnext_debug spdlog::spdlog)
