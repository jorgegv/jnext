#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #287 — --debug-listen-address, asserted against the REAL binary.
#
# The flag is the bind address of the debugger protocol servers (DZRP, ZRCP,
# GDB RSP). It landed with their shared transport, BEFORE any of them, so today
# it can do exactly two things, and the man page says so: check its value, and
# not listen. main.cpp's parse loop is linked into no unit suite (esp-cli-func
# explains why that matters), so this row is the only thing that proves either:
#
#   1. a value that is not a numeric address is refused, with the documented
#      reason, and a non-zero exit;
#   2. a host NAME is refused too, although it would resolve: a bind address
#      that could depend on DNS is one that could change under the user;
#   3. numeric addresses are accepted — IPv4 and IPv6 alike, and the
#      network-wide 0.0.0.0 — and the run exits cleanly;
#   4. nothing listens: no server prints the `<proto>: listening on` line the
#      servers' own rows will wait for.
#
# When the first server lands, ITS row asserts the address reaches its listener
# (the default binds 127.0.0.1, an override binds what was asked for), the half
# of esp-cli-func's fact 8 that cannot exist yet.
if want debug-listen-address-func; then
    begin_func debug-listen-address-func
    fails=()

    dla_run() {
        timeout --foreground --kill-after=5s 30s "$JNEXT" --headless \
            "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 2 \
            --debug-listen-address "$@" 2>&1
    }

    # Both the exit status AND the message: exiting 1 with some other
    # complaint would satisfy the status alone.
    bad_out=$(dla_run not-an-address) && bad_rc=0 || bad_rc=$?
    if [[ $bad_rc -eq 0 ]]; then
        fails+=("a value that is not an address was accepted")
    elif ! grep -q -- "--debug-listen-address: ADDR must be a numeric IP address" <<<"$bad_out"; then
        fails+=("a bad value was refused for the wrong reason")
    fi

    name_out=$(dla_run localhost) && name_rc=0 || name_rc=$?
    if [[ $name_rc -eq 0 ]]; then
        fails+=("a host NAME was accepted instead of refused")
    elif ! grep -q "must be a numeric IP address" <<<"$name_out"; then
        fails+=("a host NAME was refused for the wrong reason")
    fi

    for addr in 127.0.0.1 0.0.0.0 ::1; do
        out=$(dla_run "$addr") && rc=0 || rc=$?
        if [[ $rc -ne 0 ]]; then
            fails+=("the numeric address $addr was refused (exit $rc)")
        elif grep -q ": listening on " <<<"$out"; then
            fails+=("something listened with --debug-listen-address $addr")
        fi
    done

    if (( ${#fails[@]} == 0 )); then
        pass_row " (bad values and names refused; 127.0.0.1, 0.0.0.0, ::1 accepted; nothing listens)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
