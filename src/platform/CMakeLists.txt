file(GLOB_RECURSE SOURCES "*.cpp")
add_library(jnext_platform STATIC ${SOURCES})
find_package(SDL2 REQUIRED)
find_package(PNG REQUIRED)
# jnext_input provides MouseDispatcher (sdl_app uses it as host adapter for
# Kempston mouse — closes G43, plan rows MOUSE-13/14/15).
# jnext_core is a REAL dependency, not an incidental one: headless_app.cpp
# calls NexSaver::save / SzxSaver::save, both defined in src/core. It was
# missing here and the build only linked because the debugger library happened
# to pull jnext_core in at the right position — so turning the debugger OFF
# (ENABLE_QT_UI=OFF + ENABLE_DEBUGGER=OFF, the lean/ARM64 combo) broke the link
# with undefined references to exactly those two symbols. jnext_core does not
# depend on jnext_platform, so this introduces no cycle.
target_link_libraries(jnext_platform PUBLIC SDL2::SDL2 spdlog::spdlog PNG::PNG jnext_input jnext_core)
target_include_directories(jnext_platform PUBLIC ${CMAKE_SOURCE_DIR}/src)
# Bake the build type into the --benchmark BENCH line (Task 27 T1), so a
# benchmark result always states which optimisation level produced it.
target_compile_definitions(jnext_platform PRIVATE JNEXT_BUILD_TYPE="${CMAKE_BUILD_TYPE}")
