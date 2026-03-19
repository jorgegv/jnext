cmake_minimum_required(VERSION 3.16)
project(zxnext VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(ENABLE_TESTS    "Build unit tests"       ON)
option(ENABLE_DEBUGGER "Include Qt debugger UI" OFF)
option(CYCLE_ACCURATE  "28 MHz cycle-accurate"  OFF)

find_package(SDL2 REQUIRED)

add_subdirectory(src/core)
add_subdirectory(src/cpu)
add_subdirectory(src/memory)
add_subdirectory(src/video)
add_subdirectory(src/audio)
add_subdirectory(src/port)
add_subdirectory(src/peripheral)
add_subdirectory(src/input)
add_subdirectory(src/platform)
add_subdirectory(src/save)

add_executable(zxnext src/main.cpp)
target_link_libraries(zxnext PRIVATE
    zxnext_core zxnext_cpu zxnext_memory
    zxnext_video zxnext_audio zxnext_port
    zxnext_peripheral zxnext_input
    zxnext_platform zxnext_save
    SDL2::SDL2)
target_include_directories(zxnext PRIVATE src)

if(ENABLE_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
