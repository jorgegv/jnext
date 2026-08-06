#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #211 — `AT+CIPCLOSE=<id>`: the guest hangs up on ONE multiplexed
# connection, and the peer on the other end of a REAL socket finds out.
#
# WHY IT EXISTS WHEN esp_at_test ALREADY HAS 37 ROWS FOR THIS COMMAND. Those
# rows drive a fake transport, so the strongest thing they can assert about the
# close is that `close()` was called on the fake and that the slot was reused.
# Neither is the property the issue is about: a wedged peer occupies a real
# host socket, and freeing the slot is worth nothing if the module will not then
# take a new client on it. Only a real client can report either.
#
# WHAT IT ASSERTS (three independent facts):
#   1. the guest's trace is EXACTLY the expected AT stream: `1,CLOSED` then
#      `OK` for the close, and then `1,CONNECT` for a SECOND client dialling in
#      — the same id, which is the slot having gone back to the pool;
#   2. the client received the guest's greeting, so the connection it is about
#      to lose was genuinely established and carrying data;
#   3. the client saw EOF and could then reconnect.
#
# THE RECONNECT IS WHAT MAKES (3) WORTH ANYTHING, and that was measured rather
# than assumed. The first version of this row asserted EOF alone; jnext exits at
# the end of the row, process exit closes every socket it owns, and the row
# therefore PASSED against a build with the socket close removed AND against one
# with the slot release removed as well. A second connect cannot be satisfied
# that way: it succeeds only while jnext is still running and still listening,
# and the id it is given says whether the slot was returned to the pool
# (`1,CONNECT`) or merely marked not-open (`2,CONNECT`).
#
# WHAT IT DOES NOT DISTINGUISH, stated because it was measured too: removing
# the explicit `transport->close()` while leaving the slot release in place
# still PASSES this row, because destroying the accepted transport closes its
# descriptor anyway. That is honest — the row asserts that the connection goes
# away and that the slot comes back, not which line of the engine did it.
#
# WHY IT CANNOT FLAKE. Same construction as esp-server-func: the guest BLOCKS on
# a sync byte in every step, so it never runs ahead of the network, and the peer
# retries its first connect until the listener is up. The one new ordering —
# greeting before FIN — is TCP's own guarantee, not a timing assumption: both
# travel the same connection in the same direction, so the bytes cannot overtake
# the close that follows them.
if want esp-close-func; then
    begin_func esp-close-func

    # The same peer as esp-server-func, in its `--close` mode — one client, one
    # conversation, one place the Z80 table walker lives.
    peer_py="$SCRIPT_DIR/esp-server-peer.py"
    guest_bin="$TMP_DIR/esp_close_guest.bin"
    ready="$TMP_DIR/esp_close_ready"
    received="$TMP_DIR/esp_close_received"
    peer_log="$TMP_DIR/esp_close_peer.log"
    run_log="$TMP_DIR/esp_close_run.log"
    rm -f "$guest_bin" "$ready" "$received" "$peer_log" "$run_log"

    # The AT reply stream the guest must observe, in order. LINE mode flushes on
    # CR/LF, so each reply is one line; `> ` keeps its mandatory trailing space
    # because mapfile does not strip it.
    expected=(
        "OK"                        # ATE0
        "OK"                        # AT+CIPMUX=1
        "OK"                        # AT+CIPSERVER=1,<port>
        "1,CONNECT"                 # the client arrived; id 1, never id 0
        "OK"                        # AT+CIPSEND=1,5
        "> "                        # the prompt, trailing space and all
        "SEND OK"
        "1,CLOSED"                  # AT+CIPCLOSE=1 — the id the guest named
        "OK"                        # ...and only then the result code
        "1,CONNECT"                 # the peer dials in again and lands in the FREED slot
    )

    peer_pid=""
    verdict=""
    if ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to host the TCP client)"
    else
        python3 "$peer_py" "$guest_bin" "$ready" "$received" --close \
            >"$peer_log" 2>&1 &
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
        # No --esp-listen-address and no --esp-allow, for the same reasons as
        # esp-server-func: the shipped loopback default is the configuration
        # under test, and an allowlist governs outbound names only.
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
        # The peer writes its verdict after the reconnect attempt, which in the
        # FAILING case is up to a few seconds after jnext has gone. Waiting for
        # the file is what turns that failure into a readable one — without it
        # the row still fails, but reports an empty verdict that reads like "the
        # client never got the greeting".
        for _ in $(seq 1 100); do
            [[ $(wc -l <"$received" 2>/dev/null || echo 0) -ge 2 ]] && break
            sleep 0.1
        done
        mapfile -t peer_saw < <(cat "$received" 2>/dev/null || true)
        [[ "${peer_saw[0]:-}" == "READY" ]] \
            || fails+=("client received '${peer_saw[0]:-}' from the guest, expected 'READY'")
        [[ "${peer_saw[1]:-}" == "EOF+RECONNECT" ]] \
            || fails+=("client verdict after the greeting is '${peer_saw[1]:-}', expected 'EOF+RECONNECT' — either the accepted socket outlived the close, or the module would not take a new client afterwards")

        if [[ "${#observed[@]}" -ne "${#expected[@]}" ]]; then
            fails+=("wire has ${#observed[@]} lines, expected ${#expected[@]}: ${observed[*]}")
        else
            for i in "${!expected[@]}"; do
                [[ "${observed[$i]}" == "${expected[$i]}" ]] \
                    || fails+=("wire line $i is '${observed[$i]}', expected '${expected[$i]}'")
            done
        fi

        grep -aq "closed by the guest (AT+CIPCLOSE=1)" "$run_log" \
            || fails+=("no 'closed by the guest (AT+CIPCLOSE=1)' log line")

        if [[ ${#fails[@]} -eq 0 ]]; then
            pass_row " (client accepted as cid 1, greeted, AT+CIPCLOSE=1 -> 1,CLOSED + OK, socket really gone, and the next client landed in the freed slot as 1,CONNECT on $listen_ip:$listen_port)"
        else
            fail_row " (${fails[*]})"
        fi
    fi

    # The peer sleeps until killed once it has recorded its verdict, exactly as
    # esp-server-func's does. `|| true` on both is load-bearing under `set -e`:
    # `wait` reports the signal that killed it (143), which would otherwise
    # abort the whole regression run at the moment this row had just passed.
    if [[ -n "$peer_pid" ]]; then
        kill "$peer_pid" 2>/dev/null || true
        wait "$peer_pid" 2>/dev/null || true
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
