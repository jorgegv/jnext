# make-minimal-card (GH #458): builds the 40 MiB minimal NextZXOS card from the official
# distribution zip (or the cspect-next-1gb.img inside it) with core/minimal_card.cpp, the very
# builder the browser build runs. A DEVELOPER tool, not installed and not packaged. It links
# jnext_core, and is built in every native tree (build/, the SDL and Windows test trees);
# the browser build has no use for it.
if(NOT EMSCRIPTEN)
    add_executable(make-minimal-card make-minimal-card.cpp)
    target_link_libraries(make-minimal-card PRIVATE jnext_core)
    target_include_directories(make-minimal-card PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR})
endif()
