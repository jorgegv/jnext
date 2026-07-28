file(GLOB_RECURSE SOURCES "*.cpp")
if(SOURCES)
    add_library(jnext_peripheral STATIC ${SOURCES})
    target_include_directories(jnext_peripheral PUBLIC ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(jnext_peripheral PUBLIC spdlog::spdlog)
    # Emulated ESP-01 socket transport (GH #25 branch 2): raw BSD sockets need
    # no library at all on POSIX; Winsock needs ws2_32. Both are OS components,
    # not third-party dependencies — the same posture as the SD-card
    # provisioner's winhttp/bcrypt (src/core/CMakeLists.txt), so the project's
    # no-new-dependency rule is untouched.
    #
    # Declared HERE rather than in src/core for two reasons, both about
    # layering. First, the OS-library requirement belongs with the code that
    # needs it: esp_socket* lives in this directory, so a reader of this file
    # can see why ws2_32 is linked, and a future move of the sources moves the
    # requirement with them. Second, jnext_peripheral is a LEAF library and
    # jnext_core is the top-level orchestrator that links it; putting the
    # transport in jnext_core would make branch 3's ESP device — which belongs
    # here, next to uart_device.h — depend back on the orchestrator, inverting
    # the dependency direction every other peripheral respects.
    # PUBLIC so both jnext_core and the unit suite inherit it.
    if(WIN32)
        target_link_libraries(jnext_peripheral PUBLIC ws2_32)
    endif()
else()
    add_library(jnext_peripheral INTERFACE)
    target_include_directories(jnext_peripheral INTERFACE ${CMAKE_SOURCE_DIR}/src)
endif()
