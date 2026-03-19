file(GLOB_RECURSE SOURCES "*.cpp")
add_library(zxnext_cpu STATIC ${SOURCES})
target_include_directories(zxnext_cpu PUBLIC ${CMAKE_SOURCE_DIR}/src)
