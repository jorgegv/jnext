file(GLOB_RECURSE SOURCES "*.cpp")
add_library(jnext_port STATIC ${SOURCES})
target_include_directories(jnext_port PUBLIC ${CMAKE_SOURCE_DIR}/src)
# port_dispatch.cpp's read/write paths consult BreakpointSet::has_io_watchpoint
# and DebugState (I/O watchpoints, GH #222), exactly as mmu.h consults them for
# memory watchpoints — so jnext_port PUBLIC-links jnext_debug for the same
# reason jnext_memory does.
target_link_libraries(jnext_port PUBLIC spdlog::spdlog jnext_debug)
