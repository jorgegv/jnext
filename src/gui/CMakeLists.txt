find_package(Qt6 REQUIRED COMPONENTS Widgets)
find_package(SDL2 REQUIRED)

file(GLOB_RECURSE SOURCES "*.cpp")
add_library(jnext_gui STATIC ${SOURCES})

set_target_properties(jnext_gui PROPERTIES AUTOMOC ON)

target_link_libraries(jnext_gui PUBLIC
    Qt6::Widgets
    SDL2::SDL2
    spdlog::spdlog
    jnext_core jnext_cpu jnext_memory
    jnext_video jnext_audio jnext_port
    jnext_peripheral jnext_input
    jnext_platform jnext_save)

target_include_directories(jnext_gui PUBLIC ${CMAKE_SOURCE_DIR}/src)
