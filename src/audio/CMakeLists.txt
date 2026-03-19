file(GLOB_RECURSE SOURCES "*.cpp")
add_library(zxnext_audio STATIC ${SOURCES})
target_include_directories(zxnext_audio PUBLIC ${CMAKE_SOURCE_DIR}/src)
