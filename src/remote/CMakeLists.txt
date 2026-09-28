# The debugger protocol servers and the one transport they share (GH #276,
# arch §3.1). Package T (GH #287) is the transport alone: `transport.*` — the
# listener/`Service` every server runs over — and `fake_transport.*`, the
# in-memory `esp::EspListener`/`EspTransport` their unit suites push packets
# through. The servers (`dzrp/`, `zrcp/`, `gdb/`) land here as their packages do.
#
# NO TOOLKIT DEPENDENCY, so it is built in EVERY configuration (arch §3.2): no
# Qt, nothing from src/platform/. Sockets come only from esp01's public seam,
# which is already Windows-twinned — this directory makes no socket call of its
# own.
#
# CONFIGURE_DEPENDS: re-check this glob at BUILD time, so a newly added source
# is compiled without a manual reconfigure. Full rationale in the root
# CMakeLists.txt.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "*.cpp")
list(FILTER SOURCES EXCLUDE REGEX "${JNEXT_GENERATED_DIR_REGEX}")
add_library(jnext_remote STATIC ${SOURCES})
target_include_directories(jnext_remote PUBLIC ${CMAKE_SOURCE_DIR}/src)
# jnext_debug: `Server` IS a `jnext::dbg::Service`, and logs on the backend's
# `debugger` channel (core/log.h, spdlog — which jnext_debug links PUBLIC).
# esp01: the listener and transport interfaces, and the real socket twins.
target_link_libraries(jnext_remote PUBLIC jnext_debug esp01 spdlog::spdlog)
