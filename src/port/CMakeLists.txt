# CONFIGURE_DEPENDS: re-check this glob at BUILD time, so a newly added
# source is compiled without a manual reconfigure (GH #252 shipped as an
# undefined reference). Full rationale in the root CMakeLists.txt.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "*.cpp")
add_library(jnext_port STATIC ${SOURCES})
target_include_directories(jnext_port PUBLIC ${CMAKE_SOURCE_DIR}/src)
# port_dispatch.cpp's read/write paths consult BreakpointSet::has_io_watchpoint
# and DebugState (I/O watchpoints, GH #222), exactly as mmu.h consults them for
# memory watchpoints — so jnext_port PUBLIC-links jnext_debug for the same
# reason jnext_memory does.
target_link_libraries(jnext_port PUBLIC spdlog::spdlog jnext_debug)
