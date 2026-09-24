# CONFIGURE_DEPENDS: re-check this glob at BUILD time, so a newly added
# source is compiled without a manual reconfigure (GH #252 shipped as an
# undefined reference). Full rationale in the root CMakeLists.txt.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "*.cpp")
list(FILTER SOURCES EXCLUDE REGEX "${JNEXT_GENERATED_DIR_REGEX}")
add_library(jnext_debug STATIC ${SOURCES})
target_include_directories(jnext_debug PUBLIC ${CMAKE_SOURCE_DIR}/src)
# rewind_buffer.cpp includes core/emulator.h → input/keyboard.h → <SDL3/SDL.h>,
# so this lib needs SDL3's include directories. On Linux the SDL3 headers live
# in a default system include path so this was masked; on macOS/Homebrew the
# include dir is non-default and the build failed with 'SDL3/SDL.h' not found.
target_link_libraries(jnext_debug PUBLIC spdlog::spdlog ${JNEXT_SDL3_TARGET})
