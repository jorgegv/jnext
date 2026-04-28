file(GLOB_RECURSE SOURCES "*.cpp")
add_library(jnext_platform STATIC ${SOURCES})
find_package(SDL2 REQUIRED)
find_package(PNG REQUIRED)
# jnext_input provides MouseDispatcher (sdl_app uses it as host adapter for
# Kempston mouse — closes G43, plan rows MOUSE-13/14/15).
target_link_libraries(jnext_platform PUBLIC SDL2::SDL2 spdlog::spdlog PNG::PNG jnext_input)
target_include_directories(jnext_platform PUBLIC ${CMAKE_SOURCE_DIR}/src)
