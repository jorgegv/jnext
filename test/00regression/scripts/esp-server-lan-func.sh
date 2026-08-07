#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #210 — `--esp-listen-address` really moves the SOCKET, not just the log.
#
# WHY IT IS A SEPARATE ROW FROM esp-server-func. That row runs on the shipped
# loopback DEFAULT and proves the inbound path end to end; this one changes
# exactly one thing — the bind address — and dials **this host's own RFC1918
# address**, which the default bind does not answer on. So the connection
# succeeds only if the flag travelled the whole way: CLI parse ->
# EmulatorConfig -> setup_esp -> esp::parse_ip -> make_socket_listener ->
# ::bind. Every one of those hops is inside the assertion.
#
# WHY IT EXISTS AT ALL. Review deleted the single line in main.cpp that copies
# the parsed value into EmulatorConfig and the ENTIRE triplet stayed green —
# unit 6742, FUSE 1356, regression 122 — while `--esp-listen-address 0.0.0.0`
# was silently ignored and jnext went on binding loopback. main.cpp is linked
# into no test binary, so no unit suite can reach it, and no row passed the flag
# on a real command line. esp-cli-func now asserts the value ARRIVES (the
# startup posture line); this asserts it is ACTED ON, which is the half a log
# line cannot prove.
#
# WHAT IT ASSERTS (three facts):
#   1. the listener bound the LAN address it was told to, not loopback;
#   2. a client dialling that address is ACCEPTED and reaches guest code — the
#      5-byte greeting arrives only if the whole chain worked;
#   3. the guest saw the connection as `1,CONNECT`, i.e. the same multiplexed
#      accounting as the loopback row, unaffected by where the socket lives.
#
# It asserts a SHORTER wire stream than esp-server-func on purpose: the full AT
# transcript is that row's contract, and restating it here would mean two rows
# failing for one cause.
#
# HERMETIC. The address is one this machine already answers on, and both ends
# of the connection are on this machine, so no packet leaves the host — the
# same technique esp-loopback-peer.py uses, and the one esp_socket_test's
# LSN-19/20 use to prove the boundary at unit level. A host with no RFC1918
# address SKIPs rather than pretending.
if want esp-server-lan-func; then
    begin_func esp-server-lan-func

    peer_py="$SCRIPT_DIR/esp-server-peer.py"
    guest_bin="$TMP_DIR/esp_lan_guest.bin"
    ready="$TMP_DIR/esp_lan_ready"
    received="$TMP_DIR/esp_lan_received"
    peer_log="$TMP_DIR/esp_lan_peer.log"
    run_log="$TMP_DIR/esp_lan_run.log"
    rm -f "$guest_bin" "$ready" "$received" "$peer_log" "$run_log"

    peer_pid=""
    verdict=""
    if ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to host the TCP client)"
    else
        python3 "$peer_py" "$guest_bin" "$ready" "$received" --lan >"$peer_log" 2>&1 &
        peer_pid=$!
        for _ in $(seq 1 100); do
            [[ -s "$ready" ]] && break
            kill -0 "$peer_pid" 2>/dev/null || break
            sleep 0.1
        done

        if [[ ! -s "$ready" ]]; then
            wait "$peer_pid" 2>/dev/null && rc=0 || rc=$?
            peer_pid=""
            if [[ "$rc" -eq 3 ]]; then
                skip_row " ($(tr -d '\n' <"$peer_log"))"
            else
                fail_row " (peer never became ready, rc=$rc: $(tr -d '\n' <"$peer_log"))"
            fi
            verdict=done
        fi
    fi

    if [[ -z "$verdict" && -n "$peer_pid" ]]; then
        read -r listen_ip listen_port <"$ready"
        timeout --foreground --kill-after=5s 90s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" \
            --esp --esp-listen-address "$listen_ip" \
            --magic-port 0xCAFE --magic-port-mode line \
            --inject "$guest_bin" --inject-org 8000 --inject-pc 8000 \
            --inject-delay 100 --delayed-automatic-exit-frames 1500 \
            >"$run_log" 2>&1 || true

        fails=()
        got_greeting=$(cat "$received" 2>/dev/null || true)
        [[ "$got_greeting" == "READY" ]] \
            || fails+=("client on $listen_ip received '$got_greeting', expected 'READY'")

        # The LISTENER's own line, which names the address it really bound —
        # the config string is printed by a different line, and it is that
        # difference the row exists to close.
        grep -aq "listening for inbound connections on $listen_ip:$listen_port" "$run_log" \
            || fails+=("the listener did not bind $listen_ip:$listen_port")
        grep -aq "connection ACCEPTED from $listen_ip" "$run_log" \
            || fails+=("no 'connection ACCEPTED' log line for $listen_ip")

        # The guest's own view, in the magic-port trace: non-bracketed lines are
        # the wire (every spdlog line starts with its `[timestamp]`).
        grep -av '^\[' "$run_log" | grep -q '^1,CONNECT$' \
            || fails+=("the guest never saw 1,CONNECT")

        if [[ ${#fails[@]} -eq 0 ]]; then
            pass_row " (bound the LAN address $listen_ip:$listen_port, client accepted there, greeting delivered)"
        else
            fail_row " (${fails[*]})"
        fi
    fi

    # The peer sleeps until killed, deliberately (see its docstring). No trap:
    # the suite library owns the single EXIT handler. `|| true` on both is
    # load-bearing under `set -e` — `wait` reports the signal that killed it
    # (143), which would otherwise abort the whole regression run at the very
    # moment this row had just passed.
    if [[ -n "$peer_pid" ]]; then
        kill "$peer_pid" 2>/dev/null || true
        wait "$peer_pid" 2>/dev/null || true
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
