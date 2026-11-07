#!/usr/bin/env bash
# Run a WebAssembly module under node: the runner behind `make unit-test-wasm`
# (handed to test/run-unit-tests.sh as JNEXT_TEST_RUNNER) and, in --jnext mode,
# behind `make regression-wasm` (the shims test/00regression/regression.sh writes).
# Its model is test/wine-run.sh, far simpler: node needs no prefix, no server and
# no CRLF folding.
#
#   node-run.sh <build_dir>/test/X.js [args...]
#   node-run.sh --jnext <build_dir>/jnext.js [args...]
#
# --jnext is the same run with the build tree at dirname(js) instead of one level
# above it. The regression suite's rows invoke "$JNEXT" as one word with their own
# environment, stdin and exit status, all passed straight through; there is no
# CRLF folding and no server to start, node needs neither.
#
# Run with `bash`, never sourced, so the trap below is its own.
#
#   - node is the one the build tree was configured with
#     (CMAKE_CROSSCOMPILING_EMULATOR in <build_dir>/CMakeCache.txt: the pinned
#     emsdk node), NEVER a node from PATH: the rows depend on the exact runtime,
#     as the emsdk pins do. A missing or non-executable one is exit 2.
#   - X.wasm must sit beside X.js (emcc writes both); a missing one is exit 2.
#   - The child's text is parsed by the rows, so its locale is fixed
#     (LC_ALL=C LANG=C) and it never sees a display (DISPLAY, WAYLAND_DISPLAY
#     unset). This script's own tools use PATH=/usr/bin:/bin. HOME and the XDG
#     directories stay as the harness set them: NODERAWFS hands process.env to
#     the module's getenv.
#   - A private TMPDIR per invocation, removed afterwards. Emscripten's getpid()
#     is the constant 42 (NODERAWFS included), and dozens of suites build their
#     scratch names from it, so two concurrent runs (agents in several
#     worktrees) would share one directory and one run's remove_all would
#     destroy the other's files.
#   - As root (the CI container) node would write through a read-only file: root's
#     CAP_DAC_OVERRIDE / CAP_DAC_READ_SEARCH ignore permission bits, where the
#     suites assume an ordinary user (sdcard_test SD-28). So root drops those two
#     capabilities from the bounding set for node, as wine-run.sh does, and
#     test/dac_caps_dropped.h verifies it from /proc/self/status. Without setpriv
#     the run cannot be faithful, so it refuses.
#   - node runs as a background child with stdin passed on; TERM and INT are
#     forwarded to it and it is waited for; its exit status is this script's
#     exit status; the TMPDIR is removed after it is gone. Not `exec`: that
#     would leak the TMPDIR.
#
# What node cannot stand in for (declared absent with `# os:` in
# test/unit-tests.conf, never skipped): processes, threads, listening sockets and
# ptys. getpid() is constant.
set -euo pipefail

export PATH=/usr/bin:/bin

UP=..
if [[ ${1:-} == --jnext ]]; then UP=.; shift; fi
EXE=${1:?usage: node-run.sh [--jnext] <build_dir>/[test/]X.js [args...]}
shift
case "$EXE" in
    *.js) ;;
    *) echo "node-run: ${EXE} is not a .js module" >&2; exit 2 ;;
esac
[[ $EXE == /* ]] || EXE=$PWD/$EXE
BUILD=$(cd "${EXE%/*}/$UP" && pwd)

[[ -f "${EXE%.js}.wasm" ]] \
    || { echo "node-run: ${EXE%.js}.wasm is missing beside ${EXE##*/}" >&2; exit 2; }

NODE=$(grep -m1 -oP '^CMAKE_CROSSCOMPILING_EMULATOR:[A-Z]+=\K.*' "$BUILD/CMakeCache.txt" 2>/dev/null || true)
[[ -n "$NODE" && -x "$NODE" ]] \
    || { echo "node-run: CMAKE_CROSSCOMPILING_EMULATOR in $BUILD/CMakeCache.txt is '$NODE': not an executable node" >&2; exit 2; }

PRIV=()
if [[ "$(id -u)" == 0 ]]; then
    command -v setpriv >/dev/null 2>&1 \
        || { echo "node-run: running as root needs setpriv (util-linux) to drop CAP_DAC_OVERRIDE" >&2; exit 2; }
    PRIV=(setpriv --bounding-set=-dac_override,-dac_read_search)
fi

export LC_ALL=C LANG=C
unset DISPLAY WAYLAND_DISPLAY

JNEXT_NODE_TMP=$(mktemp -d "${TMPDIR:-/tmp}/jnext-node.XXXXXX")
export TMPDIR=$JNEXT_NODE_TMP
npid=
cleanup() { rm -rf "$JNEXT_NODE_TMP"; }
trap cleanup EXIT
trap '[[ -z "$npid" ]] || kill -TERM "$npid" 2>/dev/null || true' TERM
trap '[[ -z "$npid" ]] || kill -INT "$npid" 2>/dev/null || true' INT

${PRIV[@]+"${PRIV[@]}"} "$NODE" "$EXE" "$@" <&0 &
npid=$!
rc=0
# A trapped signal interrupts `wait` (status > 128, child possibly still alive):
# keep waiting until the child is gone, then the last `wait` is its real status.
while :; do
    rc=0; wait "$npid" || rc=$?
    kill -0 "$npid" 2>/dev/null || break
done
exit "$rc"
