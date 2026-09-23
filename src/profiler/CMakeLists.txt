# CONFIGURE_DEPENDS: re-check this glob at BUILD time, so a newly added
# source is compiled without a manual reconfigure (GH #252 shipped as an
# undefined reference). Full rationale in the root CMakeLists.txt.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "*.cpp")
list(FILTER SOURCES EXCLUDE REGEX "${JNEXT_GENERATED_DIR_REGEX}")
add_library(jnext_profiler STATIC ${SOURCES})
target_include_directories(jnext_profiler PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(jnext_profiler PUBLIC spdlog::spdlog jnext_memory)
