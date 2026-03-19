file(GLOB_RECURSE SOURCES "*.cpp")
add_library(zxnext_core STATIC ${SOURCES})
target_include_directories(zxnext_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
