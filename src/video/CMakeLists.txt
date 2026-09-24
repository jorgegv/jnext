# CONFIGURE_DEPENDS: re-check this glob at BUILD time, so a newly added
# source is compiled without a manual reconfigure (GH #252 shipped as an
# undefined reference). Full rationale in the root CMakeLists.txt.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "*.cpp")
list(FILTER SOURCES EXCLUDE REGEX "${JNEXT_GENERATED_DIR_REGEX}")
if(SOURCES)
    add_library(jnext_video STATIC ${SOURCES})
    target_include_directories(jnext_video PUBLIC ${CMAKE_SOURCE_DIR}/src)
    # jnext_save carries the GH #27 `StateDesc` descriptor layer; the video
    # subsystems' save_state/load_state are a walk of its declaration (stage
    # S4), so the BinWriteDesc/BinReadDesc bodies in jnext_save must resolve
    # here. jnext_save depends only on zlib, so there is no cycle.
    target_link_libraries(jnext_video PUBLIC spdlog::spdlog jnext_save)
else()
    add_library(jnext_video INTERFACE)
    target_include_directories(jnext_video INTERFACE ${CMAKE_SOURCE_DIR}/src)
endif()
