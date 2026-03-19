file(GLOB_RECURSE SOURCES "*.cpp")
add_library(zxnext_input STATIC ${SOURCES})
target_include_directories(zxnext_input PUBLIC ${CMAKE_SOURCE_DIR}/src)
