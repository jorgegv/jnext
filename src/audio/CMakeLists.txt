# CONFIGURE_DEPENDS: re-check this glob at BUILD time, so a newly added
# source is compiled without a manual reconfigure (GH #252 shipped as an
# undefined reference). Full rationale in the root CMakeLists.txt.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "*.cpp")
if(SOURCES)
    add_library(jnext_audio STATIC ${SOURCES})
    target_include_directories(jnext_audio PUBLIC ${CMAKE_SOURCE_DIR}/src)
else()
    add_library(jnext_audio INTERFACE)
    target_include_directories(jnext_audio INTERFACE ${CMAKE_SOURCE_DIR}/src)
endif()
