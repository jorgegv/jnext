file(GLOB_RECURSE SOURCES "*.cpp")
add_library(jnext_port STATIC ${SOURCES})
target_include_directories(jnext_port PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(jnext_port PUBLIC spdlog::spdlog)
