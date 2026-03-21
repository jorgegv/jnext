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

find_package(SDL2 REQUIRED)

# spdlog — vendored as git submodule, header-only mode.
add_subdirectory(third_party/spdlog EXCLUDE_FROM_ALL)

# input must come before core: emulator.h includes input/keyboard.h and
# zxnext_core explicitly links zxnext_input.
add_subdirectory(src/input)
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

add_executable(jnext src/main.cpp)
target_link_libraries(jnext PRIVATE
    zxnext_core zxnext_cpu zxnext_memory
    zxnext_video zxnext_audio zxnext_port
    zxnext_peripheral zxnext_input
    zxnext_platform zxnext_save
    SDL2::SDL2)
target_include_directories(jnext PRIVATE src)

if(ENABLE_QT_UI)
    target_link_libraries(jnext PRIVATE zxnext_gui)
    target_compile_definitions(jnext PRIVATE ENABLE_QT_UI)
endif()

if(ENABLE_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
