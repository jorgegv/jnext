file(GLOB_RECURSE SOURCES "*.cpp")
if(SOURCES)
    add_library(jnext_peripheral STATIC ${SOURCES})
    target_include_directories(jnext_peripheral PUBLIC ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(jnext_peripheral PUBLIC spdlog::spdlog)
    # The emulated ESP-01 (GH #25) lives in src/esp01 as a self-contained
    # module with no jnext dependencies at all; what lives HERE is the thin
    # adapter (esp_uart_adapter.*) that implements `UartDevice` in terms of
    # `esp::EspDevice` and binds the module's logging seam to `Log::esp01()`.
    #
    # PUBLIC because the adapter's header includes `esp01/esp_at.h`, so every
    # consumer of jnext_peripheral needs the module's include root. The ws2_32
    # link that used to be declared here moved into src/esp01/CMakeLists.txt
    # with the sockets — exactly as this file's previous comment anticipated:
    # the OS-library requirement belongs with the code that needs it, and it
    # still arrives here transitively.
    target_link_libraries(jnext_peripheral PUBLIC esp01)
else()
    add_library(jnext_peripheral INTERFACE)
    target_include_directories(jnext_peripheral INTERFACE ${CMAKE_SOURCE_DIR}/src)
endif()
