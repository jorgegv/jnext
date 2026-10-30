# Driver for pgo_map_parse_test (GH #416): runs the REAL
# _jnext_pgo_linked_members() of cmake/JnextPgo.cmake on a link map in script
# mode and prints the members it returns.
#   cmake -DSRC=<repo> -DDIR=<dir holding jnext.map> -DCLANG=ON|OFF -P pgo_map_parse.cmake
set(CMAKE_SOURCE_DIR "${SRC}")
include("${SRC}/cmake/JnextPgo.cmake")
set(JNEXT_PGO_DIR "${DIR}")
# Script mode has no directory scope; the function only registers the map as a
# configure dependency with it.
function(set_property)
endfunction()
_jnext_pgo_linked_members(out ${CLANG})
message("LINKED=[${out}]")
