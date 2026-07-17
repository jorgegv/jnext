file(GLOB_RECURSE SOURCES "*.cpp")
add_library(jnext_debug STATIC ${SOURCES})
target_include_directories(jnext_debug PUBLIC ${CMAKE_SOURCE_DIR}/src)
# rewind_buffer.cpp includes core/emulator.h → input/keyboard.h → <SDL2/SDL.h>,
# so this lib needs SDL2's include directories. On Linux the SDL2 headers live
# in a default system include path so this was masked; on macOS/Homebrew the
# include dir is non-default and the build failed with 'SDL2/SDL.h' not found.
target_link_libraries(jnext_debug PUBLIC spdlog::spdlog SDL2::SDL2)
