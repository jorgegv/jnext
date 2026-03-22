file(GLOB_RECURSE SOURCES "*.cpp")
add_library(jnext_memory STATIC ${SOURCES})
target_include_directories(jnext_memory PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(jnext_memory PUBLIC spdlog::spdlog)
