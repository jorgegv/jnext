file(GLOB_RECURSE SOURCES "*.cpp")
add_library(jnext_debug STATIC ${SOURCES})
target_include_directories(jnext_debug PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(jnext_debug PUBLIC spdlog::spdlog)
