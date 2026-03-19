file(GLOB_RECURSE SOURCES "*.cpp")
add_library(zxnext_peripheral STATIC ${SOURCES})
target_include_directories(zxnext_peripheral PUBLIC ${CMAKE_SOURCE_DIR}/src)
