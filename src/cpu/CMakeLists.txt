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

# Force-include our custom z80user.h before z80emu.c processes its own
# #include "z80user.h". The include guard (__Z80USER_INCLUDED__) in our file
# then prevents the libz80 default z80user.h from being re-included.
set_source_files_properties(${LIBZ80_SOURCES} PROPERTIES
    COMPILE_FLAGS "-include ${CMAKE_CURRENT_SOURCE_DIR}/z80user.h")

target_include_directories(zxnext_cpu PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/third_party/libz80)
