#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/gdb-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../gdb-functions.inc"

# GH #281 (gdb-rsp-frontend.md §6.4) — --gdb-port against the REAL binary,
# headless. main.cpp's parse loop and the loop owners' DebugServers are linked
# into no unit suite, so only a row proves them:
#
#   1. a PORT that is not a whole number from 0 to 65535 is a usage error with
#      the documented message and a non-zero exit;
#   2. a port that cannot be bound (another jnext holds it) is a STARTUP error,
#      named in the log, with a non-zero exit;
#   3. without the flag nothing listens on gdb, and --debug-listen-address is
#      accepted with --gdb-port as its only server port;
#   4. a real client over a real socket: the z88dk-gdb connect sequence
#      (qSupported, qXfer target.xml under 1023 bytes, ?, g, m, s, monitor, D);
#   5. --dzrp-port 0 --gdb-port 0 together: two listeners, a GDB and a DZRP
#      session live at once, each served, the GDB detach leaving DZRP up.
if want gdb-cli-func; then
    begin_func gdb-cli-func
    fails=()
    cli_run() {
        LANG=C timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 2 "$@" 2>&1
    }
    for bad in 65536 70000 3333x -1 ""; do
        out=$(cli_run --gdb-port "$bad") && rc=0 || rc=$?
        if [[ $rc -eq 0 ]]; then
            fails+=("--gdb-port '$bad' was accepted")
        elif ! grep -qF -- "--gdb-port: PORT must be a number from 0 to 65535, not \"$bad\"" <<<"$out"; then
            fails+=("--gdb-port '$bad' was refused for the wrong reason")
        fi
    done

    out=$(cli_run) && rc=0 || rc=$?
    if [[ $rc -ne 0 ]] || grep -q "gdb:" <<<"$out"; then
        fails+=("without --gdb-port: exit $rc, or a gdb line in the log")
    fi
    out=$(cli_run --gdb-port 0 --debug-listen-address 127.0.0.1) && rc=0 || rc=$?
    if [[ $rc -ne 0 ]] || ! grep -qE "gdb: listening on 127\.0\.0\.1:[1-9][0-9]*$" <<<"$out"; then
        fails+=("--debug-listen-address with --gdb-port as the only server port (exit $rc)")
    fi

    log="$TMP_DIR/gdb-cli-func.log"
    if ! command -v python3 &>/dev/null; then
        fails+=("python3 not available to run the GDB client")
    elif gdb_launch_cmd "$log" "$JNEXT" --headless --machine 48k "${SD_CARD_ARGS[@]}" \
            --gdb-port 0; then
        if ! gdb_peer handshake "$GDB_PORT"; then
            fails+=("handshake: ${GDB_OUT#FAIL }")
        fi
        out=$(cli_run --gdb-port "$GDB_PORT") && rc=0 || rc=$?
        if [[ $rc -eq 0 ]]; then
            fails+=("a second jnext on port $GDB_PORT started anyway")
        elif ! grep -q "gdb: cannot listen on 127.0.0.1:$GDB_PORT" <<<"$out" ||
             ! grep -q -- "--gdb-port $GDB_PORT: the GDB RSP server cannot listen" <<<"$out"; then
            fails+=("a port in use was refused without naming it")
        fi
    else
        fails+=("the headless jnext never logged 'gdb: listening on'")
    fi
    gdb_stop

    log="$TMP_DIR/gdb-cli-func-both.log"
    if gdb_launch_cmd "$log" "$JNEXT" --headless --machine 48k "${SD_CARD_ARGS[@]}" \
            --dzrp-port 0 --gdb-port 0 && [[ -n "$DZRP_PORT" ]]; then
        if ! gdb_peer both "$GDB_PORT" "$DZRP_PORT"; then
            fails+=("both: ${GDB_OUT#FAIL }")
        fi
    else
        fails+=("with --dzrp-port 0 --gdb-port 0 the two listeners were not both logged")
    fi
    gdb_stop

    if (( ${#fails[@]} == 0 )); then
        pass_row " (bad ports refused by name; a port in use is a named startup error; the z88dk-gdb connect sequence over a socket; DZRP and GDB at once)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
