file(GLOB_RECURSE SOURCES "*.cpp")
add_library(jnext_memory STATIC ${SOURCES})
target_include_directories(jnext_memory PUBLIC ${CMAKE_SOURCE_DIR}/src)
# mmu.cpp dispatches DivMMC overlay (DivMmc::read/write); mmu.h's inline
# read/write paths reference BreakpointSet::has_watchpoint. PUBLIC-link
# jnext_peripheral and jnext_debug so consumers (jnext_cpu and any test
# executable that links jnext_memory transitively) resolve those symbols
# in Release builds where the optimizer leaves them as external calls.
target_link_libraries(jnext_memory PUBLIC spdlog::spdlog jnext_peripheral jnext_debug)
