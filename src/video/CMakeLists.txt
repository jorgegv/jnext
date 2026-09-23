# CONFIGURE_DEPENDS: re-check this glob at BUILD time, so a newly added
# source is compiled without a manual reconfigure (GH #252 shipped as an
# undefined reference). Full rationale in the root CMakeLists.txt.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "*.cpp")
if(SOURCES)
    add_library(jnext_video STATIC ${SOURCES})
    target_include_directories(jnext_video PUBLIC ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(jnext_video PUBLIC spdlog::spdlog)
else()
    add_library(jnext_video INTERFACE)
    target_include_directories(jnext_video INTERFACE ${CMAKE_SOURCE_DIR}/src)
endif()
