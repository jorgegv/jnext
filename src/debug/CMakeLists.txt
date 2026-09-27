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

# GH #276 — `-Werror=switch`, for the same reason issue #43 applies it to the
# `jnext` target: this library switches over published enums with no `default`
# arm, and for the ones that have NO trailing `Count` sentinel that switch is
# the ONLY thing in the tree that notices a new enumerator.
#
#   no sentinel, so the switch IS the guard:
#       `ResetKind`, `MemSpace::Kind`, `Result`, `LogLevel`, and the internal
#       `::StepMode`
#   sentinel AND switched over here, so doubly covered:
#       `RegId` (`RegId::Count`), `ClipLayer` (`ClipLayer::Count`)
#
# (An earlier version of this comment listed `RegId` and `ClipLayer` as having
# no sentinel — they do — and omitted `LogLevel`, which has none.)
#
# As a plain WARNING this would notice nothing, and not merely because a warning
# scrolls past: this project passes no `-Wall` at all, so `-Wswitch` is not
# otherwise enabled. Compiler-gated exactly as in the `jnext` case, so a
# non-GNU/Clang toolchain still builds. PRIVATE: consumers of this library do
# not inherit it.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(jnext_debug PRIVATE -Werror=switch)
endif()
