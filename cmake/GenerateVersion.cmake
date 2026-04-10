# cmake/GenerateVersion.cmake
# Reads version.yaml and writes build/generated/version.h into the build tree.
# Called as a script via cmake -P, or included and invoked directly.

set(VERSION_YAML "${CMAKE_SOURCE_DIR}/version.yaml")
set(VERSION_H_OUT "${CMAKE_BINARY_DIR}/generated/version.h")

file(READ "${VERSION_YAML}" _ver_contents)

string(REGEX MATCH "version: *([0-9]+)\\.([0-9]+)\\.([0-9]+)" _ "${_ver_contents}")
set(VER_MAJOR "${CMAKE_MATCH_1}")
set(VER_MINOR "${CMAKE_MATCH_2}")
set(VER_PATCH "${CMAKE_MATCH_3}")

set(VER_STRING "${VER_MAJOR}.${VER_MINOR}.${VER_PATCH}")

file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/generated")
file(WRITE "${VERSION_H_OUT}"
"#pragma once
// Auto-generated from version.yaml — do not edit.
#define JNEXT_VERSION_MAJOR ${VER_MAJOR}
#define JNEXT_VERSION_MINOR ${VER_MINOR}
#define JNEXT_VERSION_PATCH ${VER_PATCH}
#define JNEXT_VERSION_STRING \"${VER_STRING}\"
")

message(STATUS "JNEXT version: ${VER_STRING}")
