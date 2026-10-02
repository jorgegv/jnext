# Profile-guided optimisation of the jnext executable (GH #297), gcc only.
#
#   JNEXT_PGO=OFF       (default) no PGO flags anywhere.
#   JNEXT_PGO=GENERATE  instrument: the build writes .gcda files into
#                       JNEXT_PGO_DIR when the binary runs.
#   JNEXT_PGO=USE       optimise with the .gcda files in JNEXT_PGO_DIR.
#
# The flags go ONLY on the sources of the jnext executable and of the in-tree
# static libraries it links (its transitive closure), never on anything else
# the tree builds (tools/gen-snapshot-schema, test binaries): those never run
# in training, so under USE they would have no profile and
# -Werror=missing-profile below would fail them for nothing.
#
# They go on SOURCES, not targets, to leave out the code the build GENERATES
# into its own tree — Qt's moc output (AUTOMOC's mocs_compilation.cpp) and
# nextboot_rom.c. gcc's per-function profile checksum includes the source
# file's path, so a moc file compiled in the instrumented tree can never match
# the same file compiled in the optimised one (-Wcoverage-mismatch, an error).
# Meta-object glue is not hot code; it is built exactly as without PGO.
#
# -fprofile-prefix-path=<build dir> makes the mangled .gcda names relative to
# the build directory, so the instrumented tree and the optimised tree can be
# two different directories (each keeps its own ccache-warm objects) and still
# agree on the file names.
#
# A profile problem FAILS the build: gcc already treats a mismatched profile
# (-Wcoverage-mismatch) as an error by default, and a TU with no profile at
# all is made an error here (-Werror=missing-profile). A binary labelled PGO
# is therefore PGO in every unit.
#
# -fprofile-partial-training: code the training run never reaches (debugger,
# rewind, most of the GUI) is optimised normally instead of for size.
# -fprofile-update=single: jnext's emulation is single-threaded; atomic
# counters would only slow the training run down.
set(JNEXT_PGO "OFF" CACHE STRING "Profile-guided optimisation stage: OFF, GENERATE or USE (gcc only)")
set_property(CACHE JNEXT_PGO PROPERTY STRINGS OFF GENERATE USE)
set(JNEXT_PGO_DIR "" CACHE PATH "Directory holding the .gcda profile (JNEXT_PGO=GENERATE/USE)")

# Collect `tgt` and every non-imported library target it links, transitively.
function(_jnext_pgo_collect tgt out_var)
    set(seen ${${out_var}})
    get_target_property(aliased ${tgt} ALIASED_TARGET)
    if(aliased)
        set(tgt ${aliased})
    endif()
    if(tgt IN_LIST seen)
        return()
    endif()
    get_target_property(imported ${tgt} IMPORTED)
    if(imported)
        return()
    endif()
    list(APPEND seen ${tgt})
    set(deps "")
    get_target_property(type ${tgt} TYPE)
    if(NOT type STREQUAL "INTERFACE_LIBRARY")
        get_target_property(l ${tgt} LINK_LIBRARIES)
        if(l)
            list(APPEND deps ${l})
        endif()
    endif()
    get_target_property(il ${tgt} INTERFACE_LINK_LIBRARIES)
    if(il)
        list(APPEND deps ${il})
    endif()
    foreach(d IN LISTS deps)
        string(REGEX REPLACE "^\\$<LINK_ONLY:(.*)>$" "\\1" d "${d}")
        if(TARGET "${d}")
            _jnext_pgo_collect("${d}" seen)
        endif()
    endforeach()
    set(${out_var} ${seen} PARENT_SCOPE)
endfunction()

function(_jnext_pgo_all_executables dir out_var)
    set(found "")
    get_property(tgts DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(t IN LISTS tgts)
        get_target_property(type ${t} TYPE)
        if(type STREQUAL "EXECUTABLE")
            list(APPEND found ${t})
        endif()
    endforeach()
    get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
    foreach(s IN LISTS subdirs)
        _jnext_pgo_all_executables("${s}" sub)
        list(APPEND found ${sub})
    endforeach()
    set(${out_var} ${found} PARENT_SCOPE)
endfunction()

# A library member the linker never pulls into jnext (the SDL frontend in a Qt
# build, spdlog's unused sinks, the schema writer only gen-snapshot-schema
# links...) never runs, so it has no .gcda, and -Werror=missing-profile would
# fail it for nothing. The INSTRUMENTED link's map (same sources, same
# libraries, same objects) says which members those are; they alone are
# exempted. The map, not the presence of a .gcda, decides: a naming mismatch
# between the two trees must still fail every TU jnext does link.
function(_jnext_pgo_linked_members out_var)
    set(map "${JNEXT_PGO_DIR}/jnext.map")
    if(NOT EXISTS "${map}")
        message(FATAL_ERROR "JNEXT_PGO=USE: ${map} missing — the profile is incomplete (retrain)")
    endif()
    # A retrain rewrites the map; re-read it then (the exempt set may change).
    set_property(DIRECTORY "${CMAKE_SOURCE_DIR}" APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${map}")
    file(STRINGS "${map}" lines REGEX "^[^ \t(]+\\.a\\([^)]+\\)")
    set(linked "")
    foreach(l IN LISTS lines)
        string(REGEX MATCH "^([^ \t(]+)\\(([^)]+)\\)" _ "${l}")
        get_filename_component(archive "${CMAKE_MATCH_1}" NAME)
        list(APPEND linked "${archive}(${CMAKE_MATCH_2})")
    endforeach()
    if(NOT linked)
        message(FATAL_ERROR "JNEXT_PGO=USE: no archive members in ${map}")
    endif()
    set(${out_var} ${linked} PARENT_SCOPE)
endfunction()

function(jnext_apply_pgo exe)
    if(JNEXT_PGO STREQUAL "OFF")
        return()
    endif()
    if(NOT JNEXT_PGO MATCHES "^(GENERATE|USE)$")
        message(FATAL_ERROR "JNEXT_PGO must be OFF, GENERATE or USE (got '${JNEXT_PGO}')")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR NOT CMAKE_C_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "JNEXT_PGO=${JNEXT_PGO} needs gcc (the C++ compiler is ${CMAKE_CXX_COMPILER_ID})")
    endif()
    if(NOT JNEXT_PGO_DIR OR NOT IS_ABSOLUTE "${JNEXT_PGO_DIR}")
        message(FATAL_ERROR "JNEXT_PGO=${JNEXT_PGO} needs an absolute JNEXT_PGO_DIR")
    endif()
    if(CMAKE_VERSION VERSION_LESS 3.18)
        message(FATAL_ERROR "JNEXT_PGO=${JNEXT_PGO} needs CMake >= 3.18")
    endif()
    if(JNEXT_PGO STREQUAL "USE" AND NOT IS_DIRECTORY "${JNEXT_PGO_DIR}")
        message(FATAL_ERROR "JNEXT_PGO=USE: no profile directory ${JNEXT_PGO_DIR} (train first)")
    endif()

    set(prefix "-fprofile-prefix-path=${CMAKE_BINARY_DIR}")
    if(JNEXT_PGO STREQUAL "GENERATE")
        set(cflags -fprofile-generate=${JNEXT_PGO_DIR} -fprofile-update=single ${prefix})
        set(lflags -fprofile-generate=${JNEXT_PGO_DIR})
    else()
        set(cflags -fprofile-use=${JNEXT_PGO_DIR} -fprofile-partial-training
                   -Werror=missing-profile ${prefix})
        set(lflags -fprofile-use=${JNEXT_PGO_DIR} -fprofile-partial-training)
    endif()

    set(targets "")
    _jnext_pgo_collect(${exe} targets)
    set(linked "")
    if(JNEXT_PGO STREQUAL "USE")
        _jnext_pgo_linked_members(linked)
    endif()
    set(exempt "")
    set(nsrc 0)
    foreach(t IN LISTS targets)
        get_target_property(type ${t} TYPE)
        if(NOT type MATCHES "^(STATIC_LIBRARY|OBJECT_LIBRARY|EXECUTABLE)$")
            continue()
        endif()
        set(archive "${CMAKE_STATIC_LIBRARY_PREFIX}${t}${CMAKE_STATIC_LIBRARY_SUFFIX}")
        get_target_property(dir ${t} SOURCE_DIR)
        get_target_property(srcs ${t} SOURCES)
        foreach(src IN LISTS srcs)
            if(NOT src MATCHES "\\.(c|cc|cpp|cxx)$")
                continue()
            endif()
            if(NOT IS_ABSOLUTE "${src}")
                set(src "${dir}/${src}")
            endif()
            get_source_file_property(generated "${src}" DIRECTORY "${dir}" GENERATED)
            if(generated OR src MATCHES "^${CMAKE_BINARY_DIR}/")
                continue()
            endif()
            set(opts ${cflags})
            get_filename_component(name "${src}" NAME)
            if(JNEXT_PGO STREQUAL "USE" AND type STREQUAL "STATIC_LIBRARY"
               AND NOT "${archive}(${name}${CMAKE_C_OUTPUT_EXTENSION})" IN_LIST linked)
                list(APPEND opts -Wno-missing-profile)
                list(APPEND exempt "${name}")
            endif()
            set_property(SOURCE "${src}" DIRECTORY "${dir}" APPEND PROPERTY COMPILE_OPTIONS ${opts})
            math(EXPR nsrc "${nsrc} + 1")
        endforeach()
    endforeach()
    target_link_options(${exe} PRIVATE ${lflags})
    if(JNEXT_PGO STREQUAL "GENERATE")
        # The link map records which library members the linker pulled into
        # jnext; tools/pgo-train.sh files it with the profile for USE.
        target_link_options(${exe} PRIVATE "LINKER:-Map=${CMAKE_BINARY_DIR}/jnext-pgo.map")
    else()
        message(STATUS "PGO USE: not linked into jnext, no profile required: ${exempt}")
    endif()
    # Any OTHER executable linking an instrumented library (gen-snapshot-schema
    # links jnext_save) needs the gcov runtime too, or it fails to link.
    if(JNEXT_PGO STREQUAL "GENERATE")
        _jnext_pgo_all_executables("${CMAKE_SOURCE_DIR}" exes)
        foreach(e IN LISTS exes)
            if(NOT e STREQUAL exe)
                target_link_options(${e} PRIVATE ${lflags})
            endif()
        endforeach()
    endif()
    message(STATUS "PGO ${JNEXT_PGO} (${JNEXT_PGO_DIR}): ${nsrc} sources of ${targets}")
endfunction()
