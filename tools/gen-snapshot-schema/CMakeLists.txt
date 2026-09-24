# The .jns schema generator (GH #27 S2). Links jnext_save alone: the schema is
# produced by walking field DECLARATIONS, so it needs no emulator, no SD image
# and no display — which is what lets `make schema-check` be as cheap as
# `docs-check`.
add_executable(gen-snapshot-schema main.cpp)
target_link_libraries(gen-snapshot-schema PRIVATE jnext_save)
target_include_directories(gen-snapshot-schema PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR})
