file(GLOB_RECURSE SOURCES "*.cpp")
add_library(zxnext_core STATIC ${SOURCES})
target_include_directories(zxnext_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
# emulator.h includes input/keyboard.h; Keyboard symbols live in zxnext_input.
# src/input is added before src/core in the root CMakeLists so this target exists.
target_link_libraries(zxnext_core PUBLIC zxnext_input zxnext_peripheral spdlog::spdlog)
