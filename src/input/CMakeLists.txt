# CONFIGURE_DEPENDS: re-check this glob at BUILD time, so a newly added
# source is compiled without a manual reconfigure (GH #252 shipped as an
# undefined reference). Full rationale in the root CMakeLists.txt.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "*.cpp")
list(FILTER SOURCES EXCLUDE REGEX "${JNEXT_GENERATED_DIR_REGEX}")
if(SOURCES)
    add_library(jnext_input STATIC ${SOURCES})
    target_include_directories(jnext_input PUBLIC ${CMAKE_SOURCE_DIR}/src)
    find_package(SDL3 REQUIRED)
    target_link_libraries(jnext_input PUBLIC ${JNEXT_SDL3_TARGET} spdlog::spdlog)
else()
    add_library(jnext_input INTERFACE)
    target_include_directories(jnext_input INTERFACE ${CMAKE_SOURCE_DIR}/src)
endif()
