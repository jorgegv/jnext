#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/dzrp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../dzrp-functions.inc"

# GH #12 (dzrp-frontend.md §11 WP-5; transport.md §2 item 15) — a --headless
# run held paused by a DZRP client neither spins nor outlives its exit bound.
#
#   1. NO SPIN. While paused with a remote attached the headless loop runs no
#      frame and waits inside pump() for the next command. The client measures
#      jnext's own CPU time (/proc/<pid>/stat) over a second of wall time while
#      it holds the machine: under 25% of a core, where the old loop — which
#      called run_frame() on a paused machine — burned all of one.
#   2. THE EXIT BOUND STILL FIRES. `--delayed-automatic-exit-frames 100` is a
#      hard bound: while the client holds the machine it is charged in WALL time,
#      20 ms a frame, so jnext exits on its own ~2 s after the attach, the
#      client still connected, with exit status 0 and "automatic exit
#      triggered" in its log — not immediately (the spinning loop's way of
#      spending a frame bound), and not never (a bound a client could hold off).
#
# THE CLIENT IS STARTED BEFORE jnext, and the port is chosen before either:
# with a frame bound of 100 and a machine that runs ~1000 frames a second
# headless, a client that waited for the log line would attach too late to
# measure anything. So the client dials the port until the listener is up and
# attaches within jnext's first frames. The port is picked by binding and
# releasing, a small race (esp-server-peer.py explains it); a lost race is a
# bind failure, which is retried twice before it counts.
EXIT_FRAMES=100
if want dzrp-paused-headless-func; then
    begin_func dzrp-paused-headless-func
    log="$TMP_DIR/dzrp-paused-headless-func.log"
    verdict=""
    if ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to run the DZRP client)"
        verdict=done
    fi
    for attempt in 1 2 3; do
        [[ -z "$verdict" ]] || break
        port=$(LANG=C python3 -c 'import socket; s = socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
        LANG=C timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --dzrp-port "$port" \
            --delayed-automatic-exit-frames "$EXIT_FRAMES" >"$log" 2>&1 &
        jpid=$!
        peer_rc=0
        peer_out=$(LANG=C timeout --foreground --kill-after=5s 45s \
            python3 "$SCRIPT_DIR/dzrp-peer.py" paused-headless "$port" "$jpid" "$EXIT_FRAMES" 2>&1) \
            || peer_rc=$?
        j_rc=0
        wait "$jpid" 2>/dev/null || j_rc=$?
        if grep -aq "dzrp: cannot listen on 127.0.0.1:$port" "$log" && (( attempt < 3 )); then
            continue    # lost the port race: pick another
        fi
        fails=()
        [[ $peer_rc -eq 0 ]] || fails+=("$(tail -n 1 <<<"$peer_out")")
        [[ $j_rc -eq 0 ]] || fails+=("jnext exited $j_rc")
        grep -aq "automatic exit triggered" "$log" || fails+=("no 'automatic exit triggered' in the log")
        if (( ${#fails[@]} == 0 )); then
            pass_row " ($(tail -n 1 <<<"$peer_out" | sed 's/^PASS //'); exit status 0)"
        else
            fail_row " (${fails[*]})"
        fi
        verdict=done
    done
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
