#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #25 — the emulated ESP-01, end to end, against a REAL TCP peer.
#
# Everything else in the suite tests a piece: the module suites drive the AT
# engine against a fake transport, the adapter suite drives the UART seam
# against a fake ESP, and esp-cli-func proves the flags. NOTHING proved the
# whole path — Z80 -> port 0x133B -> UartChannel -> EspUartAdapter -> AtEngine
# -> EspGatedTransport -> a real socket, and every one of those back — until
# this row. That path is the product.
#
# WHAT IT ASSERTS (three independent facts, all exact):
#   1. the peer received exactly the guest's payload, so guest->network works;
#   2. the guest's magic-port trace is EXACTLY the expected AT reply stream,
#      byte for byte and in order, so network->guest works and the framing is
#      right (`> ` prompt with its mandatory trailing space, `SEND OK`, and a
#      correctly-lengthed unmultiplexed `+IPD,<len>:`);
#   3. AT+CIPCLOSE with nothing open answered ERROR — load-bearing, because
#      nextsync loops it WHILE `ERROR` is not seen (design doc §5.2).
#
# WHY IT CANNOT FLAKE. Three reasons, and the third is the interesting one:
#   * No screenshot, no pixel compare, no audio, no wall-clock threshold. The
#     assertion is a byte stream.
#   * No race with the peer: the shell waits for the listener's ready file
#     before jnext starts, and the peer holds the connection open until killed,
#     so no `CLOSED` URC can appear mid-assertion.
#   * The guest BLOCKS on each expected byte, so the emulated program cannot
#     run ahead of the network. The only wall-clock dependency is that the
#     frame budget outlasts a same-host TCP connect (measured ~6 ms against a
#     ~2 s budget), and CPU contention pushes that the SAFE way: a loaded box
#     runs fewer frames per second, so the same frame budget buys MORE wall
#     time for the socket, not less. JNEXT_TEST_JOBS=4 cannot starve it.
#
# WHY 48K. The UART and the ESP are machine-independent in jnext (ports
# 0x133B-0x163B are registered unconditionally, gated only by NR 0x83 bit 4,
# which resets to 1), and 48K gives a quiet machine that is ready to be
# injected into 100 frames in — the same reason audio-gain-func uses it.
# Injecting into a mid-boot Next would land on whatever tbblue.fw had mapped.
#
# The peer binds an RFC1918 address, not loopback, because the ESP's address
# policy denies loopback by default and a test is not a reason to weaken a
# security decision. See esp-loopback-peer.py for the full reasoning.
if want esp-loopback-func; then
    begin_func esp-loopback-func

    peer_py="$SCRIPT_DIR/esp-loopback-peer.py"
    guest_bin="$TMP_DIR/esp_guest.bin"
    ready="$TMP_DIR/esp_peer_ready"
    received="$TMP_DIR/esp_peer_received"
    peer_log="$TMP_DIR/esp_peer.log"
    run_log="$TMP_DIR/esp_run.log"
    rm -f "$guest_bin" "$ready" "$received" "$peer_log" "$run_log"

    # The AT reply stream the guest must observe, in order. LINE mode flushes
    # on CR/LF, so each AT reply is one line; `> ` keeps its mandatory
    # trailing space (design doc §5.2) because mapfile does not strip it.
    expected=(
        "OK"                        # ATE0
        "ERROR"                     # AT+CIPCLOSE, nothing open
        "OK"                        # AT+CIPMUX=0
        "OK"                        # AT+CIPSTART, deferred until the connect lands
        "OK"                        # AT+CIPSEND=5
        "> "                        # the prompt, trailing space and all
        "SEND OK"
        "+IPD,14:JNEXT-ESP-OK"      # unmultiplexed +IPD, length counts the CRLF
    )

    peer_pid=""
    verdict=""
    if ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to host the TCP peer)"
    else
        python3 "$peer_py" "$guest_bin" "$ready" "$received" >"$peer_log" 2>&1 &
        peer_pid=$!
        # The peer writes the ready file last, after listen() and after the
        # guest binary exists, so its appearance is the whole handshake.
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
                fail_row " (peer never became ready: $(tr -d '\n' <"$peer_log"))"
            fi
            verdict=done
        fi
    fi

    if [[ -z "$verdict" && -n "$peer_pid" ]]; then
        read -r peer_ip peer_port <"$ready"
        timeout --foreground --kill-after=5s 90s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" \
            --esp --esp-allow "$peer_ip" \
            --magic-port 0xCAFE --magic-port-mode line \
            --inject "$guest_bin" --inject-org 8000 --inject-pc 8000 \
            --inject-delay 100 --delayed-automatic-exit-frames 1500 \
            >"$run_log" 2>&1 || true

        # Every spdlog line starts with its `[timestamp]`; the magic port
        # writes with a bare fprintf. So the non-bracketed lines ARE the wire.
        mapfile -t observed < <(grep -av '^\[' "$run_log" || true)

        fails=()
        got_payload=$(cat "$received" 2>/dev/null || true)
        [[ "$got_payload" == "HELLO" ]] \
            || fails+=("peer received '$got_payload', expected 'HELLO'")

        if [[ "${#observed[@]}" -ne "${#expected[@]}" ]]; then
            fails+=("wire has ${#observed[@]} lines, expected ${#expected[@]}: ${observed[*]}")
        else
            for i in "${!expected[@]}"; do
                [[ "${observed[$i]}" == "${expected[$i]}" ]] \
                    || fails+=("wire line $i is '${observed[$i]}', expected '${expected[$i]}'")
            done
        fi

        grep -aq "connection OPENED to $peer_ip:$peer_port" "$run_log" \
            || fails+=("no 'connection OPENED' log line for $peer_ip:$peer_port")

        if [[ ${#fails[@]} -eq 0 ]]; then
            pass_row " (AT init, TCP connect to $peer_ip, 5 bytes sent, +IPD,14 received)"
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
