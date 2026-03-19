file(GLOB_RECURSE SOURCES "*.cpp")
add_library(zxnext_port STATIC ${SOURCES})
target_include_directories(zxnext_port PUBLIC ${CMAKE_SOURCE_DIR}/src)
