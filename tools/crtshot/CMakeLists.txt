# crtshot — pictures of the CRT display effect (see tools/crtshot/crtshot.cpp).
#
# EXCLUDE_FROM_ALL, like tools/docshot: capturing needs a real display and a GPU,
# and nothing depends on it. Build it by name: `cmake --build build --target crtshot`.
add_executable(crtshot EXCLUDE_FROM_ALL crtshot.cpp)
target_link_libraries(crtshot PRIVATE
    jnext_gui
    jnext_core jnext_cpu jnext_memory jnext_video
    jnext_audio jnext_port jnext_peripheral jnext_input
    jnext_save jnext_debug spdlog::spdlog zot_tzx ZLIB::ZLIB)
target_include_directories(crtshot PRIVATE ${CMAKE_SOURCE_DIR}/src)
set_target_properties(crtshot PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
