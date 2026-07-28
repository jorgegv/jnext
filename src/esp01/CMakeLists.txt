# Emulated ESP-01 WiFi module (GH #25) — a SELF-CONTAINED component.
#
# The whole point of this directory is that another project can drop it in.
# Consequences, all deliberate:
#
#   * The target links NOTHING from jnext. Not jnext_peripheral, not
#     jnext_core, not spdlog. Its only interface dependency is the C++17
#     standard library (plus ws2_32 on Windows, an OS component).
#   * `include/` is the ONLY public include root, so every include of a module
#     header — inside the module, in its tests, and in jnext — reads
#     `#include "esp01/<name>.h"`. A consumer adds this one directory.
#   * The tests ship WITH the code. A consumer gets the proof alongside the
#     implementation rather than having to take it on trust.
#
# What stays with jnext is the ADAPTER (src/peripheral/esp_uart_adapter.*),
# which implements jnext's `UartDevice` in terms of this module's
# `esp::EspDevice` and binds the logging seam to jnext's `esp01` logger. That
# is the whole of the coupling, and it lives on jnext's side of the line.

add_library(esp01 STATIC
    src/esp_log.cpp
    src/esp_socket.cpp
    src/esp_socket_posix.cpp
    src/esp_socket_win.cpp
    src/esp_address_policy.cpp
    src/esp_at.cpp
    src/esp_threaded.cpp
)
target_include_directories(esp01 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(esp01 PUBLIC cxx_std_17)

# The threaded wrapper is optional to USE but always built, so a consumer never
# has to reconfigure to get it. pthreads is what std::thread needs on POSIX.
find_package(Threads REQUIRED)
target_link_libraries(esp01 PUBLIC Threads::Threads)

# Raw BSD sockets need no library at all on POSIX; Winsock needs ws2_32. Both
# are OS components, not third-party dependencies — the same posture as the SD
# card provisioner's winhttp/bcrypt (src/core/CMakeLists.txt), so the project's
# no-new-dependency rule is untouched.
#
# This requirement moved here FROM src/peripheral/CMakeLists.txt together with
# the sources, which is exactly what that file's comment anticipated: the
# OS-library requirement belongs with the code that needs it, so a reader of
# this file can see why ws2_32 is linked. PUBLIC so every consumer — jnext and
# the suites below — inherits it.
if(WIN32)
    target_link_libraries(esp01 PUBLIC ws2_32)
endif()

# --- The module's own test suites ------------------------------------------
#
# SOURCES LIVE WITH THE MODULE; REGISTRATION STAYS WITH JNEXT'S CONTRACT.
# Both suites remain declared in test/unit-tests.conf with their exact pinned
# row counts, and are registered here via add_test(). test/run-unit-tests.sh
# reads EVERY CTestTestfile.cmake under the build tree, not just test/'s, so
# the declared-suite cross-check sees these exactly as it sees the rest — a
# missing suite stays a loud failure, which is not weakened for the sake of
# modularity. The binaries are emitted into build/test/ because that is where
# the harness looks for them.
#
# A consumer who does not want jnext's manifest simply runs the two binaries.
if(ENABLE_TESTS)
    enable_testing()

    # Drives the AT engine against an in-memory fake transport: no socket, no
    # DNS, no listener. Portable to any platform the module builds on.
    add_executable(esp_at_test test/esp_at_test.cpp)
    target_link_libraries(esp_at_test PRIVATE esp01)

    # The address policy is pure and portable; the transport half drives an
    # in-process loopback listener whose scaffolding is POSIX-only (see the
    # suite's own header comment).
    add_executable(esp_socket_test test/esp_socket_test.cpp)
    target_link_libraries(esp_socket_test PRIVATE esp01)

    set_target_properties(esp_at_test esp_socket_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/test)

    add_test(NAME esp_at_tests     COMMAND esp_at_test)
    add_test(NAME esp_socket_tests COMMAND esp_socket_test)
endif()
