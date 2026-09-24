# CONFIGURE_DEPENDS: re-check this glob at BUILD time, so a newly added
# source is compiled without a manual reconfigure (GH #252 shipped as an
# undefined reference). Full rationale in the root CMakeLists.txt.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "*.cpp")
list(FILTER SOURCES EXCLUDE REGEX "${JNEXT_GENERATED_DIR_REGEX}")
if(SOURCES)
    add_library(jnext_audio STATIC ${SOURCES})
    target_include_directories(jnext_audio PUBLIC ${CMAKE_SOURCE_DIR}/src)
    # jnext_save carries the GH #27 `StateDesc` descriptor layer; this
    # library's save_state/load_state pairs are a walk of its declarations
    # (stage S5), so the BinWriteDesc/BinReadDesc bodies must resolve here.
    # jnext_save does not depend on this library, so there is no cycle.
    target_link_libraries(jnext_audio PUBLIC jnext_save)
else()
    add_library(jnext_audio INTERFACE)
    target_include_directories(jnext_audio INTERFACE ${CMAKE_SOURCE_DIR}/src)
endif()
