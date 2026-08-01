#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #198 — the emulated ESP-01 speaking UDP, end to end, against a REAL SNTP
# peer on a real datagram socket.
#
# WHY IT EXISTS WHEN esp-loopback-func ALREADY COVERS THE PATH. It does not: the
# TCP row proves a byte stream, and every property this feature had to get right
# is a property a byte stream does not have. The unit suites each fake one end —
# esp_at_test has no socket, esp_socket_test has no AT engine — so nothing else
# proves a Z80 guest reaching a real UDP socket and a real datagram coming back.
#
# WHAT IT ASSERTS (four independent facts, all exact):
#   1. the peer received EXACTLY 48 bytes, and they are the NTP client request
#      the guest built — so guest->network works AND the engine transmitted the
#      declared length, not the 49 bytes the guest actually wrote (see below);
#   2. the guest's magic-port trace is EXACTLY the expected AT reply stream,
#      byte for byte and in order: `CONNECT` before `OK` for a UDP CIPSTART, the
#      `> ` prompt with its mandatory trailing space, `SEND OK`, and a correctly
#      lengthed unmultiplexed `+IPD,48:`;
#   3. the 48 bytes after that header are BYTE-FOR-BYTE the datagram the peer
#      sent — the actual round trip, compared against the peer's own record of
#      what it put on the wire rather than against a constant restated here;
#   4. AT+CIPCLOSE with nothing open still answered ERROR.
#
# THE GUEST REPRODUCES newt's OFF-BY-ONE DELIBERATELY (see the peer's
# docstring): it declares `AT+CIPSEND=48` and then writes 49 bytes. Assertion 1
# is what proves the 49th was not transmitted, and assertion 2 is what proves it
# no longer blocks the `+IPD` — which is the whole reason the URC gate was
# narrowed. Delete either and the row stops testing the bug it was written for.
#
# HEX, NOT LINE, for the magic port: an SNTP reply is binary, so a line-buffered
# trace would flush at whatever CR/LF its timestamps happened to contain. Hex
# gives the exact byte stream, which is the only way to assert a `+IPD` whose
# payload is not text.
#
# WHY IT CANNOT FLAKE, on the same three grounds as esp-loopback-func: no
# screenshot, no pixel compare, no wall-clock threshold — the assertion is a
# byte stream; the shell waits for the peer's ready file before jnext starts;
# and the guest BLOCKS on each expected byte, so the emulated program cannot run
# ahead of the network. The only wall-clock dependency is that the frame budget
# outlasts a same-host UDP exchange, and CPU contention pushes that the SAFE way
# — a loaded box runs fewer frames per second, so the same frame budget buys
# MORE wall time. JNEXT_TEST_JOBS=4 cannot starve it.
#
# WHY 48K: identical to esp-loopback-func. The UART and the ESP are
# machine-independent in jnext, and 48K is a quiet machine that is ready to be
# injected into 100 frames in.
if want esp-udp-sntp-func; then
    begin_func esp-udp-sntp-func

    peer_py="$SCRIPT_DIR/esp-udp-sntp-peer.py"
    guest_bin="$TMP_DIR/esp_udp_guest.bin"
    ready="$TMP_DIR/esp_udp_ready"
    received="$TMP_DIR/esp_udp_received"
    sent="$TMP_DIR/esp_udp_sent"
    peer_log="$TMP_DIR/esp_udp_peer.log"
    run_log="$TMP_DIR/esp_udp_run.log"
    rm -f "$guest_bin" "$ready" "$received" "$sent" "$peer_log" "$run_log"

    # The AT reply stream the guest must observe, in order, as a hex string.
    # Spelled out as the ASCII it is, so a reader can check it against the
    # design doc's command table rather than decoding bytes.
    #   \r\nOK\r\n            ATE0
    #   \r\nERROR\r\n         AT+CIPCLOSE, nothing open
    #   CONNECT\r\n\r\nOK\r\n AT+CIPSTART="UDP". NOTE the missing leading CRLF:
    #                         CONNECT is a STATUS line, not a result code, so
    #                         the blank line belongs to the OK after it. newt
    #                         reads ONE line and compares it, so a leading CRLF
    #                         hands it an empty line and it gives up.
    #   \r\nOK\r\n>           AT+CIPSEND=48 ... plus the mandatory trailing space
    #   \r\nSEND OK\r\n       the 48 declared payload bytes completed
    #   \r\n+IPD,48:          the reply datagram, framed with its own length
    expected_ascii=$'\r\nOK\r\n\r\nERROR\r\nCONNECT\r\n\r\nOK\r\n\r\nOK\r\n> \r\nSEND OK\r\n\r\n+IPD,48:'

    peer_pid=""
    verdict=""
    if ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to host the SNTP peer)"
    else
        python3 "$peer_py" "$guest_bin" "$ready" "$received" "$sent" \
            >"$peer_log" 2>&1 &
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
            --magic-port 0xCAFE --magic-port-mode hex \
            --inject "$guest_bin" --inject-org 8000 --inject-pc 8000 \
            --inject-delay 100 --delayed-automatic-exit-frames 1500 \
            >"$run_log" 2>&1 || true

        # Every spdlog line starts with its `[timestamp]`; the magic port writes
        # one uppercase hex byte per line with a bare fprintf. So the two-hex-digit
        # lines ARE the wire, in order.
        observed_hex=$(grep -aE '^[0-9A-F][0-9A-F]$' "$run_log" | tr -d '\n' || true)

        expected_hex=$(printf '%s' "$expected_ascii" | od -An -tx1 -v | tr -d ' \n' \
                       | tr 'a-f' 'A-F')
        reply_hex=$(od -An -tx1 -v <"$sent" 2>/dev/null | tr -d ' \n' | tr 'a-f' 'A-F' || true)
        request_hex=$(od -An -tx1 -v <"$received" 2>/dev/null | tr -d ' \n' \
                      | tr 'a-f' 'A-F' || true)

        fails=()

        # 1. What the peer received: exactly 48 bytes (NOT the 49 the guest
        #    wrote), and the NTP client packet the guest built.
        if [[ "${#request_hex}" -ne $((48 * 2)) ]]; then
            fails+=("peer received $(( ${#request_hex} / 2 )) bytes, expected exactly 48"
                    "(a 49 here means the stray byte was transmitted as payload)")
        elif [[ "${request_hex:0:2}" != "23" ]]; then
            fails+=("peer received first byte 0x${request_hex:0:2}, expected 0x23 (VN 4, mode 3)")
        fi

        # 2 + 3. The guest-visible stream: the AT framing, then the peer's own
        #        datagram, byte for byte.
        want_hex="${expected_hex}${reply_hex}"
        if [[ -z "$reply_hex" ]]; then
            fails+=("the peer recorded no reply datagram")
        elif [[ "$observed_hex" != "$want_hex" ]]; then
            fails+=("wire mismatch: got ${#observed_hex} hex chars, expected ${#want_hex}")
            fails+=("got  $observed_hex")
            fails+=("want $want_hex")
        fi

        grep -aq "UDP connection OPENED to $peer_ip:$peer_port" "$run_log" \
            || fails+=("no 'UDP connection OPENED' log line for $peer_ip:$peer_port")

        if [[ ${#fails[@]} -eq 0 ]]; then
            pass_row " (UDP CIPSTART to $peer_ip, 48-byte SNTP request out, +IPD,48 back)"
        else
            fail_row " (${fails[*]})"
        fi
    fi

    # No trap: the suite library owns the single EXIT handler. `|| true` on both
    # is load-bearing under `set -e` — `wait` reports the signal that killed it
    # (143), which would otherwise abort the whole regression run at the very
    # moment this row had just passed.
    if [[ -n "$peer_pid" ]]; then
        kill "$peer_pid" 2>/dev/null || true
        wait "$peer_pid" 2>/dev/null || true
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
