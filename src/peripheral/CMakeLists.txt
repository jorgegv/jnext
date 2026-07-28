file(GLOB_RECURSE SOURCES "*.cpp")
if(SOURCES)
    add_library(jnext_peripheral STATIC ${SOURCES})
    target_include_directories(jnext_peripheral PUBLIC ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(jnext_peripheral PUBLIC spdlog::spdlog)
    # Emulated ESP-01 socket transport (GH #25 branch 2): raw BSD sockets need
    # no library at all on POSIX; Winsock needs ws2_32. Both are OS components,
    # not third-party dependencies — the same posture as the SD-card
    # provisioner's winhttp/bcrypt (src/core/CMakeLists.txt), so the project's
    # no-new-dependency rule is untouched. Deliberately declared HERE and not in
    # src/core: esp_socket* lives beside uart_device.h, its only future consumer,
    # and putting it in jnext_core would force branch 3's ESP device (also in
    # src/peripheral) to depend BACK on jnext_core — the undeclared
    # static-library cycle src/core/CMakeLists.txt goes out of its way to avoid.
    # PUBLIC so both jnext_core and the unit suite inherit it.
    if(WIN32)
        target_link_libraries(jnext_peripheral PUBLIC ws2_32)
    endif()
else()
    add_library(jnext_peripheral INTERFACE)
    target_include_directories(jnext_peripheral INTERFACE ${CMAKE_SOURCE_DIR}/src)
endif()
