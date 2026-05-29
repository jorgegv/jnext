file(GLOB_RECURSE SOURCES "*.cpp")
add_library(jnext_profiler STATIC ${SOURCES})
target_include_directories(jnext_profiler PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(jnext_profiler PUBLIC spdlog::spdlog jnext_memory)
