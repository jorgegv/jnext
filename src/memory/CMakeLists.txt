# CONFIGURE_DEPENDS: re-check this glob at BUILD time, so a newly added
# source is compiled without a manual reconfigure (GH #252 shipped as an
# undefined reference). Full rationale in the root CMakeLists.txt.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "*.cpp")
list(FILTER SOURCES EXCLUDE REGEX "${JNEXT_GENERATED_DIR_REGEX}")
add_library(jnext_memory STATIC ${SOURCES})
target_include_directories(jnext_memory PUBLIC ${CMAKE_SOURCE_DIR}/src)
# mmu.cpp dispatches DivMMC overlay (DivMmc::read/write); mmu.h's inline
# read/write paths reference BreakpointSet::has_watchpoint. PUBLIC-link
# jnext_peripheral and jnext_debug so consumers (jnext_cpu and any test
# executable that links jnext_memory transitively) resolve those symbols
# in Release builds where the optimizer leaves them as external calls.
# jnext_save carries the GH #27 `StateDesc` descriptor layer: ram.cpp's and
# mmu.cpp's save_state/load_state are a walk of its declaration (stage S3).
target_link_libraries(jnext_memory PUBLIC spdlog::spdlog jnext_peripheral jnext_debug jnext_save)
