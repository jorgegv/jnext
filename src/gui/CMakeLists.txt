find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(SDL2 REQUIRED)

file(GLOB_RECURSE SOURCES "*.cpp")
add_library(zxnext_gui STATIC ${SOURCES})

set_target_properties(zxnext_gui PROPERTIES AUTOMOC ON)

target_link_libraries(zxnext_gui PUBLIC
    Qt6::Widgets
    SDL2::SDL2
    spdlog::spdlog
    zxnext_core zxnext_cpu zxnext_memory
    zxnext_video zxnext_audio zxnext_port
    zxnext_peripheral zxnext_input
    zxnext_platform zxnext_save)

target_include_directories(zxnext_gui PUBLIC ${CMAKE_SOURCE_DIR}/src)
