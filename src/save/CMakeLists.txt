# CONFIGURE_DEPENDS: re-check this glob at BUILD time, so a newly added
# source is compiled without a manual reconfigure (GH #252 shipped as an
# undefined reference). Full rationale in the root CMakeLists.txt.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "*.cpp")
list(FILTER SOURCES EXCLUDE REGEX "${JNEXT_GENERATED_DIR_REGEX}")
if(SOURCES)
    add_library(jnext_save STATIC ${SOURCES})
    # ${CMAKE_SOURCE_DIR} is on the path so `#include
    # "third_party/nlohmann-json/nlohmann/json.hpp"` resolves the same way
    # jnext_core reaches third_party/fatfs. The vendored JSON header is
    # included by exactly one translation unit here (jns_container.cpp) and
    # nowhere else in the tree — see src/save/jns_container.h.
    target_include_directories(jnext_save PUBLIC
        ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR})
    # zlib supplies raw DEFLATE and CRC-32 for the .jns ZIP container
    # (GH #27 S1). It is already a hard dependency of the build
    # (find_package(ZLIB REQUIRED), root CMakeLists.txt), which is exactly why
    # the container is first-party rather than minizip: no new packaging
    # surface in the rpm spec, debian, Flatpak, Homebrew or the MinGW
    # cross-build. See doc/design/NEXT-SNAPSHOT-FORMAT.md §14.1.
    target_link_libraries(jnext_save PUBLIC ZLIB::ZLIB)
else()
    add_library(jnext_save INTERFACE)
    target_include_directories(jnext_save INTERFACE ${CMAKE_SOURCE_DIR}/src)
endif()
