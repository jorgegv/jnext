# The debugger scripting language, `.jds` (GH #26, epic #276 package S; arch
# §3.1 — target `jnext_script`). WP1: the lexer, the parser and syntax tree, the
# load-time checks, and the expression compiler exported as a LIBRARY
# (`expr_compiler.h`: `compile_expr` / `eval_expr`), which the ZRCP adapter
# (package Z) consumes instead of owning a second parser.
#
# NO TOOLKIT DEPENDENCY, so it is built in EVERY configuration (arch §3.2): no
# Qt, no SDL, nothing from src/platform/, no `Emulator*`. It reads the machine
# only through the published `jnext::dbg::Debugger` facade.
#
# CONFIGURE_DEPENDS: re-check this glob at BUILD time, so a newly added source
# is compiled without a manual reconfigure. Full rationale in the root
# CMakeLists.txt.
file(GLOB_RECURSE SOURCES CONFIGURE_DEPENDS "*.cpp")
list(FILTER SOURCES EXCLUDE REGEX "${JNEXT_GENERATED_DIR_REGEX}")
add_library(jnext_script STATIC ${SOURCES})
target_include_directories(jnext_script PUBLIC ${CMAKE_SOURCE_DIR}/src)
# jnext_debug: the `Condition` / `Event` types a compiled predicate has, and the
# `Debugger` inspection surface the evaluator reads.
target_link_libraries(jnext_script PUBLIC jnext_debug)
# Switches over the published enums (`EventKind`, `EventSource`, `MachineType`)
# carry no `default` arm where a new enumerator must be handled; make that a
# build failure, as src/debug does (see its CMakeLists.txt).
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(jnext_script PRIVATE -Werror=switch)
endif()
