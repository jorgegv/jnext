file(GLOB_RECURSE SOURCES "*.cpp")

# libz80 (z80emu) plain-C sources — compiled as part of this library so that
# our custom z80user.h (in CMAKE_CURRENT_SOURCE_DIR) shadows the one shipped
# with libz80.  The custom header is picked up because CMAKE_CURRENT_SOURCE_DIR
# appears before the libz80 directory in the include path.
file(GLOB LIBZ80_SOURCES "${CMAKE_SOURCE_DIR}/third_party/libz80/*.c")

# Exclude the libz80 test driver; it has its own main() and depends on the
# default z80user.h which our custom one replaces.
list(FILTER LIBZ80_SOURCES EXCLUDE REGEX "zextest\\.c$")
list(FILTER LIBZ80_SOURCES EXCLUDE REGEX "maketables\\.c$")

add_library(zxnext_cpu STATIC ${SOURCES} ${LIBZ80_SOURCES})

# IMPORTANT: CMAKE_CURRENT_SOURCE_DIR must come BEFORE the libz80 directory
# so that our z80user.h is found instead of the one bundled with libz80.
target_include_directories(zxnext_cpu PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party/libz80)
