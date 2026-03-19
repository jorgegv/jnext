file(GLOB_RECURSE SOURCES "*.cpp")
add_library(zxnext_save STATIC ${SOURCES})
target_include_directories(zxnext_save PUBLIC ${CMAKE_SOURCE_DIR}/src)
