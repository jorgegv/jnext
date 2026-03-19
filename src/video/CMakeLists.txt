file(GLOB_RECURSE SOURCES "*.cpp")
add_library(zxnext_video STATIC ${SOURCES})
target_include_directories(zxnext_video PUBLIC ${CMAKE_SOURCE_DIR}/src)
