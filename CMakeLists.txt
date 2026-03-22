cmake_minimum_required(VERSION 3.16)
project(jnext VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Initialize git submodules (e.g. third_party/spdlog) if not already present
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    option(GIT_SUBMODULE "Update submodules during build" ON)
    if(GIT_SUBMODULE)
        message(STATUS "Submodule update")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
            WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
            RESULT_VARIABLE GIT_SUBMOD_RESULT)
        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
            message(WARNING "git submodule update failed: ${GIT_SUBMOD_RESULT}")
        endif()
    endif()
endif()

option(ENABLE_TESTS    "Build unit tests"       ON)
option(ENABLE_DEBUGGER "Include Qt debugger UI" OFF)
option(ENABLE_QT_UI    "Build Qt6 native UI"    OFF)
option(CYCLE_ACCURATE  "28 MHz cycle-accurate"  OFF)
option(STATIC_BUILD    "Link statically (requires static SDL2/Qt6 builds)" OFF)

# Static build: prefer static libraries and link C++ runtime statically.
if(STATIC_BUILD)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ".a" ${CMAKE_FIND_LIBRARY_SUFFIXES})
    set(BUILD_SHARED_LIBS OFF)
    if(NOT MSVC)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
    endif()
endif()

find_package(SDL2 REQUIRED)

# spdlog — vendored as git submodule, header-only mode.
add_subdirectory(third_party/spdlog EXCLUDE_FROM_ALL)

# input and debug must come before core: emulator.h includes input/keyboard.h
# and debug/debug_state.h; jnext_core explicitly links jnext_input and jnext_debug.
add_subdirectory(src/input)
add_subdirectory(src/debug)
add_subdirectory(src/core)
add_subdirectory(src/cpu)
add_subdirectory(src/memory)
add_subdirectory(src/video)
add_subdirectory(src/audio)
add_subdirectory(src/port)
add_subdirectory(src/peripheral)
add_subdirectory(src/platform)
add_subdirectory(src/save)

if(ENABLE_QT_UI)
    add_subdirectory(src/gui)
endif()

if(ENABLE_DEBUGGER)
    add_subdirectory(src/debugger)
endif()

add_executable(jnext src/main.cpp)

# SDL2 linking: prefer SDL2-static when building statically.
if(STATIC_BUILD AND TARGET SDL2::SDL2-static)
    set(SDL2_LINK_TARGET SDL2::SDL2-static)
else()
    set(SDL2_LINK_TARGET SDL2::SDL2)
endif()

target_link_libraries(jnext PRIVATE
    jnext_core jnext_cpu jnext_memory
    jnext_video jnext_audio jnext_port
    jnext_peripheral jnext_input
    jnext_platform jnext_save jnext_debug
    ${SDL2_LINK_TARGET})
target_include_directories(jnext PRIVATE src)

if(ENABLE_QT_UI)
    target_link_libraries(jnext PRIVATE jnext_gui)
    target_compile_definitions(jnext PRIVATE ENABLE_QT_UI)
endif()

if(ENABLE_DEBUGGER)
    target_link_libraries(jnext PRIVATE jnext_debugger)
    target_compile_definitions(jnext PRIVATE ENABLE_DEBUGGER)
endif()

if(ENABLE_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
