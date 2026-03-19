file(GLOB_RECURSE SOURCES "*.cpp")
add_library(zxnext_memory STATIC ${SOURCES})
target_include_directories(zxnext_memory PUBLIC ${CMAKE_SOURCE_DIR}/src)
