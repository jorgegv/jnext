# docshot — regenerates the user guide's debugger screenshots (see
# tools/docshot/docshot.cpp and `make docs-screenshots`).
#
# EXCLUDE_FROM_ALL is the point of this file: capturing images needs a Qt/GUI
# build and an offscreen QPA platform, and it is not something an ordinary
# `make` or a test run should do. Nothing depends on this target; `make
# docs-screenshots` builds it by name.
add_executable(docshot EXCLUDE_FROM_ALL docshot.cpp)
target_link_libraries(docshot PRIVATE
    jnext_debugger
    jnext_core jnext_cpu jnext_memory jnext_video
    jnext_audio jnext_port jnext_peripheral jnext_input
    jnext_save jnext_debug spdlog::spdlog zot_tzx ZLIB::ZLIB)
target_include_directories(docshot PRIVATE ${CMAKE_SOURCE_DIR}/src)
# Land beside build/jnext rather than in build/tools/docshot/, so the Makefile
# target and a hand run both spell it ./build/docshot.
set_target_properties(docshot PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
