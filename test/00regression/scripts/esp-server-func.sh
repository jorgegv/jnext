#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #210 — the emulated ESP-01 as a TCP SERVER, end to end, against a real
# client on a real socket.
#
# WHY IT EXISTS WHEN esp-loopback-func ALREADY COVERS "the whole path". It
# covers the whole OUTBOUND path, and inbound is a different path with a
# different bug surface: a socket that jnext BINDS on the host, an `accept`
# that must not block the ESP worker, a connection the engine has to own (the
# outbound one is borrowed), and multiplexed framing that only a session which
# asked for it may see. The unit suites each fake one end — esp_at_test drives
# a fake listener and never binds anything, esp_socket_test binds for real and
# has no AT engine — so nothing but this row proves a real client reaching Z80
# code through a real listening socket.
#
# WHAT IT ASSERTS (four independent facts, all exact):
#   1. the client's connection was ACCEPTED and the guest's greeting reached it,
#      which is only observable from out here: those 5 bytes cross the accepted
#      socket only if the whole inbound chain worked;
#   2. the guest's magic-port trace is EXACTLY the expected AT stream, in
#      order — `1,CONNECT` for the accepted connection, the `> ` prompt with
#      its mandatory trailing space, `SEND OK`, and a correctly lengthed
#      MULTIPLEXED `+IPD,1,10:`, which is the framing v1.0 could not emit;
#   3. the connection was announced as id 1, never id 0 — slot 0 stays the
#      guest's own outbound connection (design doc §13.7);
#   4. the listener really bound the port jnext was told to bind, on the
#      loopback default, with no --esp-listen-address given.
#
# WHY IT CANNOT FLAKE, and the first draft of it DID. The guest BLOCKS on each
# expected byte, so it cannot run ahead of the network, and the peer retries its
# connect until the listener is up rather than assuming a timing. That left one
# genuinely racy ordering, which was found by writing the row and not by reading
# the code: when the client sent its request IMMEDIATELY on connecting, those
# bytes were waiting while the guest was still reacting to `1,CONNECT`, and
# whether the `+IPD` was framed before or after the guest's next `AT+CIPSEND`
# depended on which reached the wire first — the engine is correct either way
# (a `+IPD` is framed on a quiet wire, and a half-typed `AT` holds it back), so
# the trace had two legal orders and the row asserted one. The peer now sends
# only AFTER the guest has spoken (esp-server-peer.py), so nothing is ever
# pending while the guest is mid-command and exactly one order exists.
#
# The other ordering — `1,CONNECT` before the peer's `+IPD` — is fixed by
# construction rather than by luck: the engine queues CONNECT while accepting,
# which makes the wire non-quiet, and a `+IPD` is only ever framed on a quiet
# wire.
#
# WHY 48K, and why the peer holds the socket open, are the same reasons as
# esp-loopback-func; see it.
if want esp-server-func; then
    begin_func esp-server-func

    peer_py="$SCRIPT_DIR/esp-server-peer.py"
    guest_bin="$TMP_DIR/esp_server_guest.bin"
    ready="$TMP_DIR/esp_server_ready"
    received="$TMP_DIR/esp_server_received"
    peer_log="$TMP_DIR/esp_server_peer.log"
    run_log="$TMP_DIR/esp_server_run.log"
    rm -f "$guest_bin" "$ready" "$received" "$peer_log" "$run_log"

    # The AT reply stream the guest must observe, in order. LINE mode flushes on
    # CR/LF, so each reply is one line; `> ` keeps its mandatory trailing space
    # because mapfile does not strip it.
    expected=(
        "OK"                        # ATE0
        "OK"                        # AT+CIPMUX=1 — refused with ERROR before GH #210
        "OK"                        # AT+CIPSERVER=1,<port>
        "1,CONNECT"                 # the client arrived; id 1, never id 0
        "OK"                        # AT+CIPSEND=1,5 — the multiplexed send form
        "> "                        # the prompt, trailing space and all
        "SEND OK"
        "+IPD,1,10:DZRP-REQ"        # MULTIPLEXED framing, length counts the CRLF
    )

    peer_pid=""
    verdict=""
    if ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to host the TCP client)"
    else
        python3 "$peer_py" "$guest_bin" "$ready" "$received" >"$peer_log" 2>&1 &
        peer_pid=$!
        # The peer writes the ready file after the guest binary is complete, so
        # its appearance is the whole handshake.
        for _ in $(seq 1 100); do
            [[ -s "$ready" ]] && break
            kill -0 "$peer_pid" 2>/dev/null || break
            sleep 0.1
        done

        if [[ ! -s "$ready" ]]; then
            wait "$peer_pid" 2>/dev/null && rc=0 || rc=$?
            peer_pid=""
            fail_row " (peer never became ready, rc=$rc: $(tr -d '\n' <"$peer_log"))"
            verdict=done
        fi
    fi

    if [[ -z "$verdict" && -n "$peer_pid" ]]; then
        read -r listen_ip listen_port <"$ready"
        # NO --esp-listen-address: the row exercises the shipped loopback
        # DEFAULT, which is the configuration the security decision is about.
        # No --esp-allow either — an allowlist governs outbound names and has
        # nothing to say about a peer the guest did not choose.
        timeout --foreground --kill-after=5s 90s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" \
            --esp \
            --magic-port 0xCAFE --magic-port-mode line \
            --inject "$guest_bin" --inject-org 8000 --inject-pc 8000 \
            --inject-delay 100 --delayed-automatic-exit-frames 1500 \
            >"$run_log" 2>&1 || true

        # Every spdlog line starts with its `[timestamp]`; the magic port writes
        # with a bare fprintf. So the non-bracketed lines ARE the wire.
        mapfile -t observed < <(grep -av '^\[' "$run_log" || true)

        fails=()
        got_greeting=$(cat "$received" 2>/dev/null || true)
        [[ "$got_greeting" == "READY" ]] \
            || fails+=("client received '$got_greeting' from the guest, expected 'READY'")

        if [[ "${#observed[@]}" -ne "${#expected[@]}" ]]; then
            fails+=("wire has ${#observed[@]} lines, expected ${#expected[@]}: ${observed[*]}")
        else
            for i in "${!expected[@]}"; do
                [[ "${observed[$i]}" == "${expected[$i]}" ]] \
                    || fails+=("wire line $i is '${observed[$i]}', expected '${expected[$i]}'")
            done
        fi

        grep -aq "listening for inbound connections on $listen_ip:$listen_port" "$run_log" \
            || fails+=("no 'listening' log line for $listen_ip:$listen_port")
        grep -aq "connection ACCEPTED from $listen_ip" "$run_log" \
            || fails+=("no 'connection ACCEPTED' log line")

        if [[ ${#fails[@]} -eq 0 ]]; then
            pass_row " (AT+CIPMUX=1, listening on $listen_ip:$listen_port, client accepted as cid 1, +IPD,1,10 delivered, 5-byte greeting received)"
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
