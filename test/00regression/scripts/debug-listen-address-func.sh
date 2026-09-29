#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #287 — --debug-listen-address, asserted against the REAL binary.
#
# The flag is the bind address of the debugger protocol servers (DZRP today,
# ZRCP and GDB RSP later). main.cpp's parse loop is linked into no unit suite
# (esp-cli-func explains why that matters), so this row is the only thing that
# proves any of it:
#
#   1. a value that is not a numeric address is refused, with the documented
#      reason, and a non-zero exit;
#   2. a host NAME is refused too, although it would resolve: a bind address
#      that could depend on DNS is one that could change under the user;
#   3. a VALID address with no server port is refused (GH #12, owner decision,
#      the same rule as --esp-listen-address without --esp): an address for
#      servers that are all off would configure nothing — and nothing listens;
#   4. with a server (--dzrp-port 0) the address REACHES the listener: no
#      --debug-listen-address binds the 127.0.0.1 default, and 127.0.0.1,
#      0.0.0.0 and ::1 bind what was asked for, each logged with the bound
#      port. ::1 on a host with no IPv6 loopback is the one tolerated outcome
#      other than a bind: the documented startup error for that address,
#      naming it, with a non-zero exit — never a silent fallback to IPv4;
#   5. (GH #280) --zrcp-port alone is a server port too: the address is
#      accepted with it and reaches the ZRCP listener.
if want debug-listen-address-func; then
    begin_func debug-listen-address-func
    fails=()

    dla_run() {
        timeout --foreground --kill-after=5s 30s "$JNEXT" --headless \
            "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 2 "$@" 2>&1
    }

    # Both the exit status AND the message: exiting 1 with some other
    # complaint would satisfy the status alone.
    bad_out=$(dla_run --dzrp-port 0 --debug-listen-address not-an-address) && bad_rc=0 || bad_rc=$?
    if [[ $bad_rc -eq 0 ]]; then
        fails+=("a value that is not an address was accepted")
    elif ! grep -q -- "--debug-listen-address: ADDR must be a numeric IP address" <<<"$bad_out"; then
        fails+=("a bad value was refused for the wrong reason")
    fi

    name_out=$(dla_run --dzrp-port 0 --debug-listen-address localhost) && name_rc=0 || name_rc=$?
    if [[ $name_rc -eq 0 ]]; then
        fails+=("a host NAME was accepted instead of refused")
    elif ! grep -q "must be a numeric IP address" <<<"$name_out"; then
        fails+=("a host NAME was refused for the wrong reason")
    fi

    noport_out=$(dla_run --debug-listen-address 127.0.0.1) && noport_rc=0 || noport_rc=$?
    if [[ $noport_rc -eq 0 ]]; then
        fails+=("a valid address with no server port was accepted")
    elif ! grep -q -- "--debug-listen-address requires a debugger server port (--dzrp-port or --zrcp-port)" <<<"$noport_out"; then
        fails+=("a valid address with no server port was refused for the wrong reason")
    elif grep -q ": listening on " <<<"$noport_out"; then
        fails+=("something listened with no server port")
    fi

    # `<label>:<port>` with a NON-ZERO port: 0 asks the system to choose, and
    # the line must say what it chose.
    def_out=$(dla_run --dzrp-port 0) && def_rc=0 || def_rc=$?
    if [[ $def_rc -ne 0 ]] || ! grep -qE "dzrp: listening on 127\.0\.0\.1:[1-9][0-9]*$" <<<"$def_out"; then
        fails+=("no --debug-listen-address did not bind the 127.0.0.1 default (exit $def_rc)")
    fi

    # The log names an IPv6 address in brackets, in the long form the socket
    # layer prints (`[0:0:0:0:0:0:0:1]`); the short `[::1]` is accepted too.
    for addr in 127.0.0.1 0.0.0.0 ::1; do
        label=${addr//./\\.}
        [[ $addr == ::1 ]] && label='\[(::1|0:0:0:0:0:0:0:1)\]'
        out=$(dla_run --dzrp-port 0 --debug-listen-address "$addr") && rc=0 || rc=$?
        if [[ $rc -eq 0 ]] && grep -qE "dzrp: listening on $label:[1-9][0-9]*$" <<<"$out"; then
            :
        elif [[ $addr == ::1 && $rc -ne 0 ]] && grep -qE "dzrp: cannot listen on $label:0 " <<<"$out"; then
            :   # no IPv6 loopback on this host: the documented error, loudly
        else
            fails+=("--debug-listen-address $addr did not reach the listener (exit $rc)")
        fi
    done

    zrcp_out=$(dla_run --zrcp-port 0 --debug-listen-address 0.0.0.0) && zrcp_rc=0 || zrcp_rc=$?
    if [[ $zrcp_rc -ne 0 ]] || ! grep -qE "zrcp: listening on 0\.0\.0\.0:[1-9][0-9]*$" <<<"$zrcp_out"; then
        fails+=("--debug-listen-address 0.0.0.0 with only --zrcp-port did not reach the ZRCP listener (exit $zrcp_rc)")
    fi

    if (( ${#fails[@]} == 0 )); then
        pass_row " (bad values and names refused; an address without a server port refused; 127.0.0.1 default, 0.0.0.0 and ::1 reach the DZRP listener; 0.0.0.0 reaches the ZRCP one)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
