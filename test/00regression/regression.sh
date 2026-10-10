#!/usr/bin/env bash
# Automated regression test suite for JNEXT emulator — the driver.
# Runs screenshot tests + a few functional/integration tests, all of whose
# logic lives in scripts/: one script per declared functional test, plus the
# three group scripts (00-preflight-lint.sh, 01-sdcard-provision.sh,
# screenshots.sh). This file only parses arguments, takes the host lock for a
# full run, validates the declared-test manifests, sources the group scripts,
# runs each functional row in a process of its own (parallel-rows.inc,
# row-runner.sh — GH #295), and enforces the end-of-run completeness
# accounting.
# (FUSE Z80 + Z80N opcode coverage lives in `make unit-test`.)
#
# Usage: bash test/00regression/regression.sh [--update] [--preflight-only] [--platform] [test_name...]
#   --update          Update reference screenshots instead of comparing
#   --platform        Run the suite against ANOTHER OS's binaries (GH #319): macOS
#                     natively, Windows as MinGW exes under wine. The target is read
#                     from the build trees (JNEXT_TARGET_BUILD / JNEXT_TARGET_SDL_BUILD,
#                     JNEXT_TARGET_OS in their CMakeCache.txt), never from the caller.
#                     Runs the screenshots and the functional rows whose `os=` tag in
#                     functional_tests.conf admits that OS; the 6 lints (tree checks)
#                     and the stamp stay with the Linux run. `make regression-win` /
#                     `make regression-macos`.
#   --preflight-only  Run only the harness preflight checks and exit (no tests).
#                     This is the seam test/harness-selftest.sh drives.
#   test_name         Run only specified tests (default: all)
#
# Env: JNEXT_REGRESSION_CONF / JNEXT_REGRESSION_FUNC_CONF override the
#      manifests; JNEXT_REGRESSION_SCRIPTS_DIR overrides the test-script
#      directory (the self-test uses these to inject a truncated or unpinned
#      manifest, a declared test with no script, or a stray undeclared script).
#      JNEXT_REGRESSION_SCRIPTS_DIR is usable with --preflight-only ONLY: a
#      full run sources the override's scripts, which resolve
#      ../test-functions.inc relative to their own directory and die loudly.
#      JNEXT_TEST_JOBS caps the screenshot and parallel functional lanes
#      (default: every CPU), JNEXT_TEST_QUIET_JOBS the quiet phase (default
#      1); JNEXT_REGRESSION_ROW_TIMEOUT bounds one row (600 s);
#      JNEXT_REGRESSION_STAMP=1 (set by `make regression`) stamps a green run;
#      =confirm (set by `make regression-confirm`) re-runs a full run's
#      loaded-host FAILs solo and, if they pass, stamps it;
#      JNEXT_REGRESSION_LOCK_FILE / _LOCK_WAIT / _LOAD_WAIT tune the host lock
#      and the quiet-host wait; JNEXT_REGRESSION_ROW_TIMES=<file> logs each
#      row's seconds.

set -euo pipefail

# --- One FULL run per host at a time (GH #295) ---
# A full run is one with no arguments and the real manifests: that is what
# `make regression` runs, and what loads the whole machine. Two of them at once
# make each other's timing-sensitive rows lie, so the second one waits for the
# first, saying who holds the lock. Targeted runs (row names, --update,
# --preflight-only) and runs on overridden manifests (the self-test's stub
# suites) never lock. JNEXT_REGRESSION_LOCK=force locks any run shape — the
# self-test's hook.
#
# This happens BEFORE the suite library is sourced, so a waiting run holds no
# scratch directory and no SD clone. flock -o: the lock is held by flock(1)
# itself and never inherited by the run's children, so a leftover child can
# never keep the host locked.
REGRESSION_SLOT=false
# --platform (GH #319) does not change what a "full run" is: it is full when it
# names no rows. On macOS a platform run never locks (flock + /proc/locks are
# Linux-only, and macOS runs happen on a CI runner of their own).
PLATFORM_MODE=false
reg_nargs=0
for reg_arg in "$@"; do
    if [[ "$reg_arg" == --platform ]]; then PLATFORM_MODE=true; else reg_nargs=$(( reg_nargs + 1 )); fi
done
if [[ "${JNEXT_REGRESSION_LOCK:-}" == force || "${JNEXT_REGRESSION_STAMP:-}" == confirm ]] \
   || [[ $reg_nargs -eq 0 && -z "${JNEXT_REGRESSION_CONF:-}${JNEXT_REGRESSION_FUNC_CONF:-}${JNEXT_REGRESSION_SCRIPTS_DIR:-}" ]]; then
    REGRESSION_SLOT=true
fi
if $PLATFORM_MODE && [[ "$(uname -s)" == Darwin ]]; then REGRESSION_SLOT=false; fi
reg_lock=${JNEXT_REGRESSION_LOCK_FILE:-${XDG_CACHE_HOME:-$HOME/.cache}/jnext/regression.lock}
# A run NESTED inside a run that already holds this very lock — a self-test
# child, a row that drives the harness — is part of that run: it must never
# wait for the lock its own ancestor holds (it would wait out the bound: the
# self-test's preflight children inside `make regression-confirm` did, at
# load 0.5), nor for a quiet host its ancestor is the load of. The locked run
# exports what it holds in JNEXT_REGRESSION_ANCESTOR_LOCKS (below) as
# "<lock>|<pid>|<start time>" entries, ';'-separated. The marker is EVIDENCE,
# not a password: an entry counts only if that pid is alive with that /proc
# start time (so a recycled pid is not it), is an ancestor of THIS process (a
# descendant that outlived its run, or a hand-exported marker, is not), and
# the lock is really held right now. Anything else is not nested and locks
# normally. A nested run on a DIFFERENT lock file (the self-test's lock rows)
# still locks normally too.
reg_proc_field() {   # reg_proc_field <pid> <n> — field n of /proc/<pid>/stat after "(comm)"; 1 = state
    local st n=$2 IFS=$' \t\n'
    local -a f
    st=$(cat "/proc/$1/stat" 2>/dev/null) || return 1
    st=${st##*) }             # past "(comm) ": a comm may itself hold spaces or ")"
    read -r -a f <<<"$st"
    [[ -n "${f[n-1]:-}" ]] || return 1
    echo "${f[n-1]}"
}
reg_lock_holder_is() {   # reg_lock_holder_is <lock> <pid>... — one of the pids holds a FLOCK on <lock>
    # Two facts, each checked the way the kernel sees the file, so a symlinked
    # lock path and a btrfs subvolume (whose stat st_dev is not the s_dev that
    # /proc/locks prints) both come out right: /proc/locks lists a FLOCK held
    # by that pid on the inode the path RESOLVES to (stat -L: flock(1) locks
    # the target), and that pid has the very file open (`-ef` compares device
    # AND inode of both resolved files, so an inode equal by chance on another
    # filesystem does not match).
    local ino line pid dev want fd IFS=$' \t\n'
    ino=$(stat -L -c %i "$1" 2>/dev/null) || return 1
    while read -r line; do
        # "<n>: FLOCK ADVISORY WRITE <pid> <maj>:<min>:<inode> <start> <end>"
        read -r _ _ _ _ pid dev _ <<<"$line"
        [[ "${dev##*:}" == "$ino" ]] || continue
        for want in "${@:2}"; do
            [[ -n "$want" && "$pid" == "$want" ]] || continue
            for fd in "/proc/$pid/fd/"*; do
                [[ "$fd" -ef "$1" ]] && return 0
            done
        done
    done < <(grep -E '^[0-9]+: +FLOCK' /proc/locks 2>/dev/null || true)
    return 1
}
reg_held_by_ancestor() {   # reg_held_by_ancestor <lock> — true iff a verified ancestor holds it
    local entry e_lock e_pid e_start p
    local IFS=';'
    for entry in ${JNEXT_REGRESSION_ANCESTOR_LOCKS:-}; do
        IFS='|' read -r e_lock e_pid e_start <<<"$entry"
        [[ "$e_lock" == "$1" && "$e_pid" =~ ^[0-9]+$ && -n "$e_start" ]] || continue
        # field 20 after "(comm)" is starttime (stat field 22); field 2 is ppid
        [[ "$(reg_proc_field "$e_pid" 20)" == "$e_start" ]] || continue
        p=$$
        while [[ -n "$p" && "$p" -gt 1 && "$p" != "$e_pid" ]]; do p=$(reg_proc_field "$p" 2) || p=""; done
        [[ "$p" == "$e_pid" ]] || continue
        # ... and THAT run holds the lock right now: /proc/locks names the
        # process owning each flock — the run's `flock -o` wrapper, i.e. its
        # parent — by the lock file's inode. Held by anyone else is not it.
        reg_lock_holder_is "$1" "$e_pid" "$(reg_proc_field "$e_pid" 2)" || continue
        return 0
    done
    return 1
}
if $REGRESSION_SLOT && reg_held_by_ancestor "$reg_lock"; then
    echo "  nested inside a run that holds $reg_lock: not locking again"
    REGRESSION_SLOT=false
fi
if $REGRESSION_SLOT && [[ -z "${JNEXT_REGRESSION_LOCK_HELD:-}" ]]; then
    # flock(1) is util-linux-core, in every Fedora image including the bare CI
    # container; still, a missing one is said, never an exit 127 mid-loop.
    if ! command -v flock >/dev/null 2>&1; then
        echo ""; echo "=== REGRESSION HARNESS FAULT ==="
        echo "  flock(1) (util-linux) is not installed: a full run cannot take the host lock"
        echo ""; exit 2
    fi
    reg_lock_bound=${JNEXT_REGRESSION_LOCK_WAIT:-7200}
    mkdir -p "$(dirname "$reg_lock")"
    reg_waited=0
    # The run itself is flock's child, started as a background JOB (set -m:
    # its own process group, so its INT/TERM stay trappable — a plain `&`
    # child of a script would start with SIGINT ignored). A TERM or INT sent
    # to THIS shell alone (`kill <pid>`, not the group) is forwarded to that
    # group, so the run cleans up and the lock is released; without it flock
    # and the run lived on, holding the lock to the end (GH #295 review).
    # Its stdin is /dev/null: a background process group that read the
    # terminal would be stopped (SIGTTIN), and nothing in the suite reads it.
    reg_fpid=""
    trap '[[ -n "$reg_fpid" ]] && kill -TERM -- "-$reg_fpid" 2>/dev/null' TERM
    trap '[[ -n "$reg_fpid" ]] && kill -INT -- "-$reg_fpid" 2>/dev/null' INT
    while :; do
        set -m
        JNEXT_REGRESSION_LOCK_HELD="$reg_lock" \
            flock -o -n -E 75 "$reg_lock" bash "$0" "$@" < /dev/null &
        reg_fpid=$!
        set +m
        reg_rc=0
        wait "$reg_fpid" || reg_rc=$?
        # A trapped signal interrupts `wait`; keep waiting for the run's own end.
        while kill -0 "$reg_fpid" 2>/dev/null; do
            reg_rc=0
            wait "$reg_fpid" || reg_rc=$?
        done
        reg_fpid=""
        [[ $reg_rc -eq 75 ]] || exit "$reg_rc"
        if (( reg_waited >= reg_lock_bound )); then
            echo ""
            echo "=== REGRESSION HARNESS FAULT ==="
            echo "  waited ${reg_waited} s for the full-run lock $reg_lock; still held by: $(cat "$reg_lock.holder" 2>/dev/null || echo unknown)"
            echo ""
            exit 2
        fi
        if (( reg_waited % 60 == 0 )); then
            echo "  another FULL regression run holds the host lock ($(cat "$reg_lock.holder" 2>/dev/null || echo "holder unknown")) — waiting, at most ${reg_lock_bound} s (waited ${reg_waited} s)"
        fi
        sleep 5
        reg_waited=$(( reg_waited + 5 ))
    done
fi
if [[ -n "${JNEXT_REGRESSION_LOCK_HELD:-}" ]]; then
    echo "pid $$ since $(date '+%Y-%m-%d %H:%M:%S') in $(cd "$(dirname "$0")/../.." && pwd)" \
        > "$JNEXT_REGRESSION_LOCK_HELD.holder" 2>/dev/null || true
    reg_entry="$JNEXT_REGRESSION_LOCK_HELD|$$|$(reg_proc_field $$ 20 || echo unknown)"
    export JNEXT_REGRESSION_ANCESTOR_LOCKS="${JNEXT_REGRESSION_ANCESTOR_LOCKS:+$JNEXT_REGRESSION_ANCESTOR_LOCKS;}$reg_entry"
    unset JNEXT_REGRESSION_LOCK_HELD
fi
# What THIS run was asked to do stays with this run: kept in shell variables
# and taken out of the environment its rows (and anything they start) inherit.
# Inherited, `JNEXT_REGRESSION_STAMP=confirm` made every nested regression.sh
# a lock-taking, stamp-writing run of its own; `JNEXT_REGRESSION_LOCK=force`
# would do the same.
REG_STAMP_MODE=${JNEXT_REGRESSION_STAMP:-}
export -n JNEXT_REGRESSION_STAMP JNEXT_REGRESSION_LOCK 2>/dev/null || true

# Shared helpers/constants and the one-time environment setup (locale, jnext
# binary resolution, SD args, manifest paths, TMP_DIR + EXIT trap, counters,
# row helpers) live in the suite library; sourcing it initializes them once.
# shellcheck source=test/00regression/test-functions.inc
source "$(dirname "$0")/test-functions.inc"
# The functional-row scheduler: one process per row, several at once.
# shellcheck source=test/00regression/parallel-rows.inc
source "$(dirname "$0")/parallel-rows.inc"
regression_lanes

# The tools the HARNESS itself executes — this driver, its .inc files, the
# row runner, the stamp script, the group scripts and the six lints — checked
# before anything runs, so a missing one is a named harness fault and never a
# lint or a row that FAILs "exit 127" (GH #295 CI: procps was missing in CI and
# a check read the failure as evidence). Rows check their own tools (python3,
# ffmpeg, xvfb-run, mtools, ...) and SKIP or FAIL by name, so they are not here;
# neither is ImageMagick, which the screenshot rows report as SKIP. DERIVED by
# `strace -f -qq -e trace=execve -e status=successful` over a full run,
# intersected with the program names that appear in the harness files above;
# re-derive the same way after changing any of them. `hostname` is absent from
# fedora:44 and only a fallback-guarded call (its fallback, uname, is listed).
# Present = an executable on PATH (`type -P`, never a builtin) that does not
# answer `--version` with 126/127 ("cannot execute" / "not found").
reg_missing=""
# A platform run executes no lint (the one user of rg), and on macOS takes no
# host lock (flock).
reg_tools=(awk basename bash cat comm cp date dirname env find git grep head \
           ln ls mkdir mktemp mv nproc rm rmdir sed sha256sum sleep sort stat \
           tail timeout tr uname wc "${CXX:-c++}")
$PLATFORM_MODE || reg_tools+=(rg)
if ! $PLATFORM_MODE || [[ "$(uname -s)" != Darwin ]]; then reg_tools+=(flock); fi
for reg_tool in "${reg_tools[@]}"; do
    reg_tool_path=$(type -P "$reg_tool") || { reg_missing+=" $reg_tool"; continue; }
    reg_rc=0; "$reg_tool_path" --version </dev/null >/dev/null 2>&1 || reg_rc=$?
    [[ $reg_rc -ne 126 && $reg_rc -ne 127 ]] || reg_missing+=" $reg_tool"
done
[[ -z "$reg_missing" ]] \
    || harness_fault "tool(s) the regression harness runs are not installed:${BOLD}${reg_missing}${RESET}" \
                     "Install them; a lint or a row would otherwise report their absence as a test failure."

# A full run waits (bounded) for a host that is not already overloaded.
if $REGRESSION_SLOT; then
    wait_for_quiet_host
fi

# Test scripts: scripts/<name>.sh for every functional test declared in
# functional_tests.conf, plus the three group scripts.
SCRIPTS_DIR="${JNEXT_REGRESSION_SCRIPTS_DIR:-$SCRIPT_DIR/scripts}"

# Parse arguments
UPDATE_MODE=false
PREFLIGHT_ONLY=false
FILTER_TESTS=()
for arg in "$@"; do
    if [[ "$arg" == "--update" ]]; then
        UPDATE_MODE=true
    elif [[ "$arg" == "--preflight-only" ]]; then
        PREFLIGHT_ONLY=true
    elif [[ "$arg" == "--platform" ]]; then
        :   # pre-scanned above (PLATFORM_MODE)
    else
        FILTER_TESTS+=("$arg")
    fi
done
# Membership is an in-shell hash lookup, NEVER `printf ... | grep -qx`: under
# `set -o pipefail` grep -q exits the instant it matches, the printf subshell
# can then die of SIGPIPE (141), and pipefail promotes 141 to the pipeline's
# status — a name that IS present reports as absent.
declare -A IS_FILTERED
for arg in "${FILTER_TESTS[@]+"${FILTER_TESTS[@]}"}"; do IS_FILTERED["$arg"]=1; done

# Check prerequisites
if ! $PLATFORM_MODE && [[ ! -x "$JNEXT" ]]; then
    echo -e "${RED}ERROR: jnext binary not found at $JNEXT — build first${RESET}"
    exit 1
fi

# No SD-card fixture existence check here: the suite self-provisions the
# fallback image (the "[sdcard-provision]" row, scripts/01-sdcard-provision.sh)
# after the lint row and before the screenshot launch, once the
# manifest/preflight checks below have passed. --preflight-only never needs an
# SD image at all.

# --- The platform under test (GH #319) ---
# TARGET_OS is the OS whose binaries the run executes. Without --platform it is
# linux, and the `os=` tags below admit every row (linux is mandatory in every
# tag). With it, it is read out of the build trees' own CMakeCache.txt
# (JNEXT_TARGET_OS, set by CMakeLists.txt from CMAKE_SYSTEM_NAME), never taken
# from the caller; both trees (the Qt one and the SDL-only one) must agree.
TARGET_OS=linux
if $PLATFORM_MODE; then
    $UPDATE_MODE && harness_fault "${BOLD}--platform${RESET} cannot be combined with --update" \
                                  "Reference screenshots are regenerated on Linux only."
    [[ -z "$REG_STAMP_MODE" ]] || harness_fault "${BOLD}--platform${RESET} runs are never stamped" \
                                  "The stamp is the Linux run's; JNEXT_REGRESSION_STAMP is set."
    plat_tos=""
    for plat_var in JNEXT_TARGET_BUILD JNEXT_TARGET_SDL_BUILD; do
        plat_dir=${!plat_var:-}
        [[ -n "$plat_dir" ]] || harness_fault "${BOLD}--platform${RESET} needs ${BOLD}$plat_var${RESET} (the build tree whose binary is under test)" \
                                              "Use ${BOLD}make regression-win${RESET} or ${BOLD}make regression-macos${RESET}."
        plat_cache="$plat_dir/CMakeCache.txt"
        [[ -f "$plat_cache" ]] || harness_fault "no CMakeCache.txt in ${BOLD}$plat_dir${RESET} ($plat_var): not a configured build tree"
        plat_os=$(grep -m1 -oP '^JNEXT_TARGET_OS:[A-Z]+=\K.*' "$plat_cache" || true)
        [[ -n "$plat_os" ]] || harness_fault "$plat_cache does not define ${BOLD}JNEXT_TARGET_OS${RESET}" \
                                             "Every build of this project sets it; the harness will not guess."
        case $plat_os in
            macos|windows) ;;
            linux) harness_fault "${BOLD}$plat_dir${RESET} targets linux: --platform is for another OS" \
                                 "A Linux run is plain ${BOLD}make regression${RESET}." ;;
            *) harness_fault "unknown target OS '${BOLD}$plat_os${RESET}' in $plat_cache" "Known: linux, macos, windows." ;;
        esac
        [[ -z "$plat_tos" || "$plat_tos" == "$plat_os" ]] \
            || harness_fault "the two build trees disagree about the target OS: ${BOLD}$plat_tos${RESET} and ${BOLD}$plat_os${RESET}" \
                             "JNEXT_TARGET_BUILD=$JNEXT_TARGET_BUILD, JNEXT_TARGET_SDL_BUILD=$JNEXT_TARGET_SDL_BUILD"
        plat_tos=$plat_os
    done
    TARGET_OS=$plat_tos
fi

# A manifest that is not there must say so, not be diagnosed as "missing its pin".
for conf in "$CONF" "$FUNC_CONF"; do
    [[ -f "$conf" ]] || harness_fault "Test manifest not found: ${BOLD}$conf${RESET}"
done
echo -e "  manifests: $(basename "$CONF") + $(basename "$FUNC_CONF")"

# rewind-func runs a unit-test binary that `make clean` deletes. Check it HERE,
# in the first second, not five minutes into the run: an incomplete build is a
# harness fault, not a code regression — and never a silently absent row.
if ! $PLATFORM_MODE && [[ ${#FILTER_TESTS[@]} -eq 0 || -n "${IS_FILTERED[rewind-func]:-}" ]]; then
    if [[ ! -x "$REWIND_TEST" ]]; then
        harness_fault "rewind_test is not built: ${BOLD}$REWIND_TEST${RESET}" \
                      "The suite runs it, so it cannot report a rewind result without it." \
                      "Build it with: ${BOLD}make unit-test-build${RESET}  (or use ${BOLD}make regression${RESET}, which does)"
    fi
fi

# sdl-keypress-func drives the SDL-ONLY frontend, which is a different binary
# from the one every other row uses: SdlApp is instantiated only when
# ENABLE_QT_UI=OFF (src/main.cpp:944-951). Same rule as rewind_test above — it
# is a build artifact the Makefile guarantees, so a missing one is a harness
# fault in the first second, never a row that quietly reports nothing.
if ! $PLATFORM_MODE && [[ ${#FILTER_TESTS[@]} -eq 0 || -n "${IS_FILTERED[sdl-keypress-func]:-}" ]]; then
    if [[ ! -x "$JNEXT_SDL" ]]; then
        harness_fault "SDL-only jnext is not built: ${BOLD}$JNEXT_SDL${RESET}" \
                      "It is the only build that runs SdlApp, so the suite cannot report an SDL keypress result without it." \
                      "Build it with: ${BOLD}make sdl-release${RESET}  (or use ${BOLD}make regression${RESET}, which does)"
    fi
fi

# --- The declared counts, pinned ---
# `# expect: N` in each manifest. Without it, deleting a test from a conf
# shrinks both sides of the completeness check below in lockstep and the suite
# reports a smaller number as a clean pass. The pin makes the denominator a
# claim the file has to make out loud, exactly like test/unit-tests.conf's
# per-suite row counts.
for conf in "$CONF" "$FUNC_CONF"; do
    pin=$(pinned_count "$conf")
    have=$(declared_count "$conf")
    [[ -n "$pin" ]] || harness_fault "No '# expect: N' pin in ${BOLD}$conf${RESET}" \
                                     "The manifest must state how many tests it declares."
    [[ "$pin" -eq "$have" ]] || harness_fault \
        "${BOLD}$conf${RESET} declares ${BOLD}$have${RESET} tests but pins ${BOLD}# expect: $pin${RESET}" \
        "A test was added or removed without updating the pin. If deliberate, update it."
done

# --- The screenshot manifest needs an INDEPENDENT witness ---
# The completeness check at the end compares the rows reported against the
# rows declared — but for screenshots both sides come from
# regression_tests.conf, so on its own that term is a tautology.
# img/<name>-reference.png is the independent witness: it is checked in, one
# per screenshot test, and it does not disappear when the conf is truncated.
# A reference with no conf entry means a test was dropped from the manifest.
declare -A IS_DECLARED_SCREENSHOT
while read -r name _; do
    [[ -z "$name" || "$name" == \#* ]] && continue
    IS_DECLARED_SCREENSHOT["$name"]=1
done < "$CONF"
for ref in "$IMG_DIR"/*-reference.png; do
    [[ -e "$ref" ]] || continue           # no refs at all (fresh tree) — nothing to witness
    ref_name=${ref##*/}
    ref_name=${ref_name%-reference.png}
    [[ -n "${IS_DECLARED_SCREENSHOT[$ref_name]:-}" ]] \
        || harness_fault "reference image ${BOLD}img/${ref_name}-reference.png${RESET} exists, but ${BOLD}$ref_name${RESET} is NOT declared in regression_tests.conf" \
                         "A screenshot test was dropped from the manifest. If that was deliberate, delete its reference image too."
done

# --- The declared functional tests (test/00regression/functional_tests.conf) ---
# Each test script calls `begin_func <name>`, which records that the row was
# actually reported. The completeness check at the end of the run proves that
# every declared test reported exactly one row and no undeclared row appeared.
# A row's line may carry TAGS after its name (GH #295), which decide where it
# runs: `quiet` — in the quiet phase after the parallel one, a lane at a time
# (rows that pace against real time and fail under contention); `serial` —
# alone, after everything else; `private-sd` — on a copy of the SD card of its
# own (rows that write to the card). Untagged rows run in the parallel phase on
# the run's shared clone. An unknown tag is refused, not ignored.
DECLARED_FUNC=()
declare -A IS_PRIVATE_SD IS_QUIET IS_SERIAL FUNC_OS
FUNC_HAS_OS_TAG=false
while read -r name tags; do
    [[ -z "$name" || "$name" == \#* ]] && continue
    DECLARED_FUNC+=("$name")
    for tag in $tags; do
        case $tag in
            quiet)      IS_QUIET["$name"]=1 ;;
            serial)     IS_SERIAL["$name"]=1 ;;
            private-sd) IS_PRIVATE_SD["$name"]=1 ;;
            os=*)
                # `os=<list>` (GH #319): the OSes whose run includes this row.
                # A closed set, comma-separated; linux is mandatory, so a Linux
                # run still runs every row. No tag = all three.
                [[ -z "${FUNC_OS[$name]:-}" ]] \
                    || harness_fault "functional test ${BOLD}$name${RESET} carries two ${BOLD}os=${RESET} tags"
                os_list=${tag#os=}
                [[ -n "$os_list" ]] || harness_fault "functional test ${BOLD}$name${RESET} carries an empty ${BOLD}os=${RESET} tag" \
                                                     "Known OSes: linux, macos, windows."
                os_has_linux=false
                for os_one in ${os_list//,/ }; do
                    case $os_one in
                        linux) os_has_linux=true ;;
                        macos|windows) ;;
                        *) harness_fault "functional test ${BOLD}$name${RESET} names an unknown OS ${BOLD}$os_one${RESET} in ${BOLD}$tag${RESET}" \
                                         "Known OSes: linux, macos, windows." ;;
                    esac
                done
                [[ -n "$os_list" && "$os_list" != ,* && "$os_list" != *, && "$os_list" != *,,* ]] \
                    || harness_fault "functional test ${BOLD}$name${RESET} carries a malformed ${BOLD}$tag${RESET}"
                $os_has_linux || harness_fault "functional test ${BOLD}$name${RESET}: ${BOLD}$tag${RESET} does not list linux" \
                                               "linux is mandatory: a Linux run runs every declared row."
                FUNC_OS["$name"]=",$os_list,"
                FUNC_HAS_OS_TAG=true
                ;;
            *) harness_fault "functional test ${BOLD}$name${RESET} carries an unknown tag ${BOLD}$tag${RESET}" \
                             "Known tags: quiet, serial, private-sd, os=<linux[,macos][,windows]>." ;;
        esac
    done
    [[ -z "${IS_QUIET[$name]:-}" || -z "${IS_SERIAL[$name]:-}" ]] \
        || harness_fault "functional test ${BOLD}$name${RESET} is tagged both quiet and serial — pick one"
done < "$FUNC_CONF"
[[ ${#DECLARED_FUNC[@]} -gt 0 ]] || harness_fault "No functional tests declared in $FUNC_CONF"
declare -A IS_DECLARED_FUNC
for name in "${DECLARED_FUNC[@]}"; do IS_DECLARED_FUNC["$name"]=1; done

REPORTED_FUNC=()

# --- The scripts directory and the conf must agree, in BOTH directions ---
# Exactly like the reference-image witness above: a declared test with no
# scripts/<name>.sh could never report its row, and a stray scripts/*.sh is a
# test that was dropped from the manifest.
for name in "${DECLARED_FUNC[@]}"; do
    [[ -f "$SCRIPTS_DIR/$name.sh" ]] \
        || harness_fault "functional test ${BOLD}$name${RESET} is declared in functional_tests.conf but has NO test script" \
                         "Expected ${BOLD}$SCRIPTS_DIR/$name.sh${RESET} — a declared test with no script can never report its row." \
                         "If the test was removed deliberately, remove its conf line and update the pin."
done
for script in "$SCRIPTS_DIR"/*.sh; do
    [[ -e "$script" ]] || continue        # no scripts at all — caught above
    script_name=${script##*/}
    case "$script_name" in
        00-preflight-lint.sh|01-sdcard-provision.sh|screenshots.sh) continue ;;
    esac
    test_name=${script_name%.sh}
    [[ -n "${IS_DECLARED_FUNC[$test_name]:-}" ]] \
        || harness_fault "test script ${BOLD}$SCRIPTS_DIR/$script_name${RESET} exists, but ${BOLD}$test_name${RESET} is NOT declared in functional_tests.conf" \
                         "A functional test was dropped from the manifest. If that was deliberate, delete its script too."
done

# Which rows each OS runs, and the per-OS pins. A row with no `os=` tag runs
# everywhere. `# expect-macos: N` / `# expect-windows: N` (beside `# expect:`)
# state how many rows that OS runs, so retagging a row is a deliberate edit of
# the pin, exactly like adding one is of `# expect:`. Once any row carries a tag,
# both pins are required; a pin present without tags must equal the total.
row_admits() {   # row_admits <row> <os> — does the <os> run include <row>?
    [[ -z "${FUNC_OS[$1]:-}" || "${FUNC_OS[$1]}" == *",$2,"* ]]
}
for os_one in macos windows; do
    have=0
    for name in "${DECLARED_FUNC[@]}"; do row_admits "$name" "$os_one" && have=$(( have + 1 )); done
    pin=$(grep -oP "^#\s*expect-$os_one:\s*\K[0-9]+" "$FUNC_CONF" 2>/dev/null | head -1 || true)
    if [[ -z "$pin" ]]; then
        ! $FUNC_HAS_OS_TAG || harness_fault "No '# expect-$os_one: N' pin in ${BOLD}$FUNC_CONF${RESET}" \
                                            "Rows carry os= tags, so the manifest must state how many rows $os_one runs."
    else
        [[ "$pin" -eq "$have" ]] || harness_fault \
            "${BOLD}$FUNC_CONF${RESET} admits ${BOLD}$have${RESET} rows on $os_one but pins ${BOLD}# expect-$os_one: $pin${RESET}" \
            "An os= tag or a row was added or removed without updating the pin. If deliberate, update it."
    fi
done
# The rows this run executes, and (platform runs) the ones it declares absent.
RUN_FUNC=(); ABSENT_FUNC=()
for name in "${DECLARED_FUNC[@]}"; do
    if row_admits "$name" "$TARGET_OS"; then RUN_FUNC+=("$name"); else ABSENT_FUNC+=("$name"); fi
done
declare -A IS_ABSENT_FUNC
for name in "${ABSENT_FUNC[@]+"${ABSENT_FUNC[@]}"}"; do IS_ABSENT_FUNC["$name"]=1; done
# A named row this OS does not run is refused, never silently dropped.
for name in "${FILTER_TESTS[@]+"${FILTER_TESTS[@]}"}"; do
    [[ -z "${IS_ABSENT_FUNC[$name]:-}" ]] \
        || harness_fault "row ${BOLD}$name${RESET} is declared absent on $TARGET_OS (its os= tag does not admit it)"
done

# Every preflight guard has now run. --preflight-only exists so the self-test
# can drive each of them in a second instead of a five-minute suite: these
# guards are what make the denominator trustworthy.
if $PREFLIGHT_ONLY; then
    echo -e "${GREEN}preflight OK${RESET}: $(declared_count "$CONF") screenshot + ${#DECLARED_FUNC[@]} functional tests declared, pins agree"
    ! $PLATFORM_MODE || echo "  platform $TARGET_OS: ${#RUN_FUNC[@]} functional rows run, ${#ABSENT_FUNC[@]} declared absent"
    exit 0
fi

# --- Platform run: the binaries under test (GH #319) ---
# $JNEXT / $JNEXT_SDL become what the rows execute. On macOS that is the build's
# own binary. On Windows it is a one-line shim that runs the MinGW exe under wine
# through test/wine-run.sh --jnext (which carries the translations wine needs: see
# its header), so a row keeps invoking "$JNEXT" as one word, with its own
# environment and its own stdout/stderr. The native helper tools (rewind_test,
# sdfile_tool, the probes) are Linux/macOS binaries of the SDL unit-test tree.
if $PLATFORM_MODE; then
    plat_exe=""; [[ "$TARGET_OS" == windows ]] && plat_exe=.exe
    plat_qt="$JNEXT_TARGET_BUILD/jnext$plat_exe"
    plat_sdl="$JNEXT_TARGET_SDL_BUILD/jnext$plat_exe"
    for plat_bin in "$plat_qt" "$plat_sdl"; do
        [[ -x "$plat_bin" ]] || harness_fault "the $TARGET_OS binary under test is not built: ${BOLD}$plat_bin${RESET}" \
                                              "Build it with ${BOLD}make regression-$([[ $TARGET_OS == windows ]] && echo win || echo macos)${RESET}, which does."
    done
    if [[ "$TARGET_OS" == windows ]]; then
        command -v wine >/dev/null 2>&1 || harness_fault "wine is not installed" "dnf install wine-core wine-common"
        plat_qt_abs=$(cd "$JNEXT_TARGET_BUILD" && pwd)
        plat_sdl_abs=$(cd "$JNEXT_TARGET_SDL_BUILD" && pwd)
        export WINEPREFIX="$plat_qt_abs/wine-prefix"
        bash "$PROJECT_DIR/test/wine-run.sh" --init "$plat_qt_abs" \
            || harness_fault "cannot initialise the wine prefix $WINEPREFIX"
        # Persistent wineserver, before any row (wine-run.sh header); stopped by regression_cleanup.
        REG_WINE_PREFIX=$WINEPREFIX
        bash "$PROJECT_DIR/test/wine-run.sh" --serve "$plat_qt_abs" \
            || harness_fault "cannot start a persistent wineserver"
        mkdir -p "$TMP_DIR/platform"
        for plat_pair in "jnext:$plat_qt_abs" "jnext-sdl:$plat_sdl_abs"; do
            printf '#!/bin/bash\nexport WINEPREFIX=%q\nexec /bin/bash %q --jnext %q "$@"\n' \
                "$WINEPREFIX" "$PROJECT_DIR/test/wine-run.sh" "${plat_pair#*:}/jnext.exe" \
                > "$TMP_DIR/platform/${plat_pair%%:*}"
            chmod +x "$TMP_DIR/platform/${plat_pair%%:*}"
        done
        JNEXT="$TMP_DIR/platform/jnext"
        JNEXT_SDL="$TMP_DIR/platform/jnext-sdl"
        # The host's own jnext, for the one job that must not go through wine:
        # deriving the SD master (scripts/01-sdcard-provision.sh).
        JNEXT_NATIVE="$(cd "$TEST_BIN_DIR/.." && pwd)/jnext"
        [[ -x "$JNEXT_NATIVE" ]] || harness_fault "the host's jnext is not built: ${BOLD}$JNEXT_NATIVE${RESET}" \
                                                  "The SDL unit-test tree provides it (make unit-test-sdl-build)."
        export JNEXT_NATIVE
    else
        JNEXT=$(cd "$JNEXT_TARGET_BUILD" && pwd)/jnext
        JNEXT_SDL=$(cd "$JNEXT_TARGET_SDL_BUILD" && pwd)/jnext
    fi
    export JNEXT JNEXT_SDL JNEXT_TARGET_OS="$TARGET_OS"
    echo -e "  platform run: ${BOLD}$TARGET_OS${RESET} (Qt: $plat_qt, SDL-only: $plat_sdl)"
    echo -e "  ${#RUN_FUNC[@]} functional rows run; ${BOLD}${#ABSENT_FUNC[@]} declared absent on $TARGET_OS${RESET} (os= in functional_tests.conf, reason beside each row):"
    for name in "${ABSENT_FUNC[@]+"${ABSENT_FUNC[@]}"}"; do echo "    absent: $name"; done
    for name in "${RUN_FUNC[@]}"; do
        [[ "$name" == rewind-func ]] && [[ ! -x "$REWIND_TEST" ]] && harness_fault "rewind_test is not built: $REWIND_TEST"
    done
fi

if ! $HAS_COMPARE; then
    echo -e "${YELLOW}WARNING: ImageMagick 'compare' not found — pixel comparison disabled${RESET}"
fi

echo -e "${BOLD}=== JNEXT Regression Test Suite ===${RESET}"
# Recorded, not acted on: a FAIL on a loaded host is flagged, never excused (GH #245).
load_report start
echo -e "  lanes: ${PAR_LANES} for screenshot and parallel functional rows, ${QUIET_LANES} for quiet rows (JNEXT_TEST_JOBS / JNEXT_TEST_QUIET_JOBS)"
REG_T0=$(date +%s)
# The stamp of a green full run is keyed on the tree as it was when the run
# STARTED (test/regression-stamp.sh); `make regression` asks for it.
# A stamp that cannot be computed is a loud note, not a fault: the run's
# verdict is the tests', and `make regression-stamp-check` then says "no stamp".
REG_STAMP_START=""
if [[ "$REG_STAMP_MODE" == 1 || "$REG_STAMP_MODE" == confirm ]]; then
    REG_STAMP_START=$(bash "$PROJECT_DIR/test/regression-stamp.sh" state) \
        || { REG_STAMP_START=""; echo -e "  ${YELLOW}WARNING: cannot compute the regression stamp key; this run will not be stamped${RESET}"; }
fi
echo ""

# Group rows: the six preflight lints and the SD-image provisioning, then the
# whole screenshot suite. Every test script is SOURCED (never exec'd) so all of
# them share this one shell's counters, REPORTED_FUNC and ORDERED_TESTS.
# shellcheck source=test/00regression/scripts/00-preflight-lint.sh
if $PLATFORM_MODE; then
    # The 6 lints check the source tree; they run in the Linux run.
    echo "  (lints: tree checks, run by the Linux run only)"
else
    source "$SCRIPTS_DIR/00-preflight-lint.sh"
fi
# shellcheck source=test/00regression/scripts/01-sdcard-provision.sh
source "$SCRIPTS_DIR/01-sdcard-provision.sh"
# The run's clone as provisioned. func_phases_end compares against it once
# every row has run: the screenshot rows and the untagged functional rows all
# boot this one file, so none of them may change it.
SHARED_SD_PRINT=$(sd_fingerprint "$RUN_DIR/sdcard/cspect-next-1gb-fixed.img")

# FUSE Z80 + Z80N opcode tests run in `make unit-test` (fuse_z80_test +
# z80n_test), not here.

reg_shots_t0=$SECONDS
# shellcheck source=test/00regression/scripts/screenshots.sh
source "$SCRIPTS_DIR/screenshots.sh"
echo -e "  (screenshots: $(( SECONDS - reg_shots_t0 )) s)"

# --- Functional tests ---
# Every row runs in a process of its own (row-runner.sh, which still SOURCES it
# inside the counter guard), in three phases: the untagged rows, PAR_LANES at a
# time; then the `quiet` rows, QUIET_LANES at a time, on a host no longer
# loaded by the first phase; then the `serial` rows, one at a time. Within a
# phase, rows are printed and merged in declared order; a phase's output is
# not interleaved with the next. A name filter selects rows exactly as before.
func_phases_begin
PAR_ROWS=(); QUIET_ROWS=(); SERIAL_ROWS=()
for func_name in "${RUN_FUNC[@]}"; do
    want "$func_name" || continue
    if [[ -n "${IS_SERIAL[$func_name]:-}" ]]; then SERIAL_ROWS+=("$func_name")
    elif [[ -n "${IS_QUIET[$func_name]:-}" ]]; then QUIET_ROWS+=("$func_name")
    else PAR_ROWS+=("$func_name")
    fi
done
run_func_phase "parallel" "$PAR_LANES" "${PAR_ROWS[@]+"${PAR_ROWS[@]}"}"
run_func_phase "quiet (timing-sensitive)" "$QUIET_LANES" "${QUIET_ROWS[@]+"${QUIET_ROWS[@]}"}"
run_func_phase "serial tail" 1 "${SERIAL_ROWS[@]+"${SERIAL_ROWS[@]}"}"
func_phases_end

echo ""
echo -e "${BOLD}=== Results ===${RESET}"
echo -e "  ${GREEN}Pass: $pass${RESET}  ${RED}Fail: $fail${RESET}  ${YELLOW}Skip: $skip${RESET}"
load_report end
load_summary

# --- Completeness: prove the suite ran everything it declares ---
# A green result is only as trustworthy as its denominator. On a full run, every
# declared functional test must have reported exactly one row, no undeclared row
# may appear, and the grand total must equal 5 (preflight lints) +
# 1 (sdcard-provision) + screenshots + functional. Anything else means a test
# went missing, which is a harness fault, not a pass.
if [[ ${#FILTER_TESTS[@]} -eq 0 ]] && ! $UPDATE_MODE; then
    faults=()
    # Per-name row counts and membership come from pure-bash hashes over the
    # arrays — never a pipe/subshell (`printf ... | grep` can misreport under
    # `set -o pipefail`; see the note above IS_FILTERED).
    declare -A REPORTED_COUNT
    for name in "${REPORTED_FUNC[@]+"${REPORTED_FUNC[@]}"}"; do
        REPORTED_COUNT["$name"]=$(( ${REPORTED_COUNT["$name"]:-0} + 1 ))
    done
    for name in "${RUN_FUNC[@]}"; do
        n=${REPORTED_COUNT["$name"]:-0}
        [[ "$n" -eq 1 ]] || faults+=("declared in functional_tests.conf but reported $n rows: ${BOLD}$name${RESET}")
    done
    for name in "${REPORTED_FUNC[@]+"${REPORTED_FUNC[@]}"}"; do
        [[ -n "${IS_DECLARED_FUNC[$name]:-}" ]] \
            || faults+=("reported a row but is NOT declared in functional_tests.conf: ${BOLD}$name${RESET}")
        [[ -z "${IS_ABSENT_FUNC[$name]:-}" ]] \
            || faults+=("reported a row although it is declared absent on $TARGET_OS: ${BOLD}$name${RESET}")
    done
    reg_lints=6; $PLATFORM_MODE && reg_lints=0
    expected=$(( reg_lints + 1 + ${#ORDERED_TESTS[@]} + ${#RUN_FUNC[@]} ))
    actual=$(( pass + fail + skip ))
    [[ "$actual" -eq "$expected" ]] \
        || faults+=("row count is ${BOLD}$actual${RESET}, but $reg_lints lint + 1 sdcard-provision + ${#ORDERED_TESTS[@]} screenshot + ${#RUN_FUNC[@]} functional = ${BOLD}$expected${RESET} were declared")
    if [[ ${#faults[@]} -gt 0 ]]; then
        harness_fault "${faults[@]}" "" \
            "The suite did not run what it says it ran. Treat this as RED, not as a pass."
    fi
    echo -e "  ${BOLD}$actual/$expected declared tests reported${RESET}"
fi
REG_WALL=$(( $(date +%s) - REG_T0 ))
echo -e "  wall time: ${REG_WALL} s"

# The stamp (GH #295). Only a full run in which every row PASSED is stamped: a
# SKIP is a row that was not tested. A full run whose only FAILs are
# functional rows that failed on a LOADED host — the owner's rule: a timing
# row that fails under load and passes SOLO counts as a pass — is recorded as
# PENDING instead, and `make regression-confirm` re-runs exactly those rows,
# one at a time on a quiet host, on the same tree; only if every one passes is
# the run stamped, and the stamp names them. Any other FAIL is real.
reg_stamp() { bash "$PROJECT_DIR/test/regression-stamp.sh" "$@" \
    || echo -e "  ${YELLOW}WARNING: the stamp record could not be written (test/regression-stamp.sh $1)${RESET}"; }
reg_facts=("pass=$pass" "fail=$fail" "skip=$skip" "rows=$(( pass + fail + skip ))"
           "load_start=${LOAD_START:-unknown}" "load_end=${LOAD_END:-unknown}" "cpus=$HOST_CPUS"
           "lanes=$PAR_LANES/$QUIET_LANES" "wall_s=$REG_WALL")
case "$REG_STAMP_MODE" in
    1)
        if [[ ${#FILTER_TESTS[@]} -gt 0 ]] || $UPDATE_MODE; then
            echo -e "  no regression stamp: only a full run is stamped"
        elif [[ -z "$REG_STAMP_START" ]]; then
            echo -e "  ${YELLOW}WARNING: NOT stamped: the stamp key could not be computed at the start${RESET}"
        elif [[ $skip -gt 0 ]]; then
            echo -e "  ${YELLOW}no regression stamp: $skip row(s) SKIPPED, i.e. not tested: ${SKIPPED_ROWS[*]}${RESET}"
        elif [[ $fail -eq 0 ]]; then
            reg_stamp write "$REG_STAMP_START" "${reg_facts[@]}"
        elif reg_solo=$(solo_confirmable_fails); then
            reg_stamp pending "$REG_STAMP_START" "fails=$reg_solo" "${reg_facts[@]}"
        else
            echo -e "  no regression stamp: $reg_solo"
        fi
        ;;
    confirm)
        if [[ -z "$REG_STAMP_START" ]]; then
            echo -e "  ${YELLOW}WARNING: NOT confirmed: the stamp key could not be computed at the start${RESET}"
        else
            reg_missing=""
            for reg_r in "${FILTER_TESTS[@]+"${FILTER_TESTS[@]}"}"; do
                reg_seen=""
                for reg_n in "${REPORTED_FUNC[@]+"${REPORTED_FUNC[@]}"}"; do [[ "$reg_n" == "$reg_r" ]] && reg_seen=1; done
                [[ -n "$reg_seen" ]] || reg_missing+=" $reg_r"
            done
            if [[ -n "$reg_missing" ]]; then
                echo -e "  no regression stamp: the solo re-run did not report:$reg_missing"
            else
                reg_stamp confirm "$REG_STAMP_START" "rows=${FILTER_TESTS[*]}" "${reg_facts[@]}"
            fi
        fi
        ;;
esac

if [[ $fail -gt 0 ]]; then
    exit 1
fi
# A SKIP is a row that was not tested, and it fails the run exactly as a FAIL
# does (owner, 2026-10-06) — in every mode: full, named rows, confirm, --update.
if [[ $skip -gt 0 ]]; then
    echo -e "  ${RED}${BOLD}REGRESSION NOT PASSED: $skip row(s) SKIPPED, i.e. not tested: ${SKIPPED_ROWS[*]} — a SKIP is not a pass${RESET}"
    exit 1
fi
exit 0
