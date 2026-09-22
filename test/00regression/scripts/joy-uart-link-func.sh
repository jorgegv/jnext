#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #252 — the LIVE, bidirectional joystick-port serial cable, end to end,
# against real FIFOs and a real Z80 program.
#
# WHY A REAL RIG AND NOT A UNIT ROW. The unit suite (uart_integration_test
# JOY-13..21) drives the same descriptors and proves the mux routing in both
# directions, and that is the right tier for the routing. What it cannot reach
# is main.cpp's parse loop and the shape of the thing a user actually builds:
# two named pipes in a directory, a NEX that a real Z80 executes, and a shell on
# the other end. The feature this replaces (--joy-uart-rx, GH #251) failed for a
# reason of exactly that kind — it read the file whole at startup, so pointing
# it at a FIFO blocked until the writer closed — and no in-process row would
# have noticed, because in-process there is no writer to wait for.
#
# THE HANDSHAKE IS DETERMINISTIC, not a sleep. Bytes sent to the joystick UART
# while NR 0x0B is not routing this connector are LOST on the wire, exactly as
# they are on hardware, so "wait a bit and hope the guest is up" is a race
# dressed as a delay. The demo therefore transmits one 'R' the instant it has
# written NR 0x0B, and this row waits for that byte before sending anything.
# That 'R' also proves the TX direction on its own: it is queued by the cable
# before any reader exists and handed over when one attaches.
#
# FIVE FACTS:
#   1. --joy-uart-fifo reaches the emulator and CREATES both FIFOs.
#   2. Next -> host works: the guest's 'R' arrives on <base>.tx, from a cable
#      that had no reader at the time it was transmitted.
#   3. host -> Next works: 'ABC' written to <base>.rx reaches the Z80.
#   4. ...and it went THROUGH the Z80. The demo answers BYTE+1, so the reply is
#      'BCD'. A plain echo would be 'ABC' and is exactly what a loopback bug in
#      UartChannel::deliver_tx_byte would produce, so this is the assertion that
#      distinguishes the feature from the defect it is built next to.
#   5. The refusals: two cables at once, a live cable plus a recorded stream, a
#      delay that only a recorded stream can honour, and a path that exists and
#      is not a FIFO. Every one of them is a run that would otherwise proceed
#      looking alive while half of it did nothing.
if want joy-uart-link-func; then
    begin_func joy-uart-link-func

    nex="$PROJECT_DIR/test/00regression/nex/joy_uart_demo.nex"
    base="$TMP_DIR/joy-uart-cable"
    outfile="$TMP_DIR/joy-uart-from-guest.bin"
    runlog="$TMP_DIR/joy-uart-link-run.log"
    rm -f "$base.rx" "$base.tx" "$outfile" "$runlog"

    fails=()
    jnext_pid=""
    cat_pid=""

    # 60 emulated seconds is ~40 s of wall clock for this demo's poll loop on
    # the development host and less on a faster one; the handshake below needs
    # about one. `timeout` is the backstop for a jnext that never exits.
    timeout --foreground --kill-after=5s 120s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" \
        --joy-uart-fifo "$base" \
        --load "$nex" \
        --delayed-automatic-exit 60 >"$runlog" 2>&1 &
    jnext_pid=$!

    # Both FIFOs exist once the cable has been opened; jnext creates them.
    for _ in $(seq 1 300); do
        [[ -p "$base.rx" && -p "$base.tx" ]] && break
        kill -0 "$jnext_pid" 2>/dev/null || break
        sleep 0.1
    done

    if [[ ! -p "$base.rx" || ! -p "$base.tx" ]]; then
        fails+=("--joy-uart-fifo did not create both FIFOs at '$base'")
    else
        # Read the Next->host direction continuously. Opening a FIFO for reading
        # blocks until a writer appears, which is why this is a background job
        # and not an `exec` redirect: jnext opens its write end only when it has
        # something to send.
        timeout 110 cat "$base.tx" >"$outfile" &
        cat_pid=$!

        # Fact 2 — the guest's readiness byte, and with it the TX direction.
        for _ in $(seq 1 600); do
            [[ -s "$outfile" ]] && break
            kill -0 "$jnext_pid" 2>/dev/null || break
            sleep 0.1
        done
        ready=$(head -c 1 "$outfile" 2>/dev/null || true)
        if [[ "$ready" != "R" ]]; then
            fails+=("guest never announced itself on the cable (got '$ready', want 'R')")
        else
            # Facts 3 and 4 — the round trip. `printf > fifo` returns at once
            # because jnext is holding the read end open.
            printf 'ABC' >"$base.rx"
            for _ in $(seq 1 600); do
                [[ "$(stat -c %s "$outfile" 2>/dev/null || echo 0)" -ge 4 ]] && break
                kill -0 "$jnext_pid" 2>/dev/null || break
                sleep 0.1
            done
            got=$(head -c 4 "$outfile" 2>/dev/null || true)
            # RBCD: the readiness byte, then the guest's answer to A, B and C.
            # 'RABC' would mean the emulator echoed rather than the Z80 replying.
            [[ "$got" == "RBCD" ]] \
                || fails+=("round trip returned '$got', expected 'RBCD'")
        fi
    fi

    # `|| true` on both is load-bearing under `set -e`: `wait` reports the
    # signal that killed the job (143), which would otherwise abort the whole
    # regression run at the moment this row had just passed. No trap — the
    # suite library owns the single EXIT handler, and $TMP_DIR goes with it.
    if [[ -n "$jnext_pid" ]]; then
        kill "$jnext_pid" 2>/dev/null || true
        wait "$jnext_pid" 2>/dev/null || true
    fi
    if [[ -n "$cat_pid" ]]; then
        kill "$cat_pid" 2>/dev/null || true
        wait "$cat_pid" 2>/dev/null || true
    fi

    # Fact 5 — the refusals. Both the exit status AND the message are asserted:
    # exiting 1 with some other complaint would satisfy the status alone.
    link_refuse() {
        local why=$1 want=$2; shift 2
        local out rc=0
        out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless \
            "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 2 "$@" 2>&1) || rc=$?
        if [[ $rc -eq 0 ]]; then
            fails+=("$why was accepted instead of refused")
        elif ! grep -q "$want" <<<"$out"; then
            fails+=("$why was refused for the wrong reason")
        fi
    }
    rx_src="$TMP_DIR/joy-uart-link-src.bin"
    printf 'XYZ' >"$rx_src"
    plain="$TMP_DIR/joy-uart-not-a-fifo"
    printf 'not a fifo' >"$plain.rx"

    link_refuse "two live cables at once" 'both attach a live cable' \
        --joy-uart-fifo "$TMP_DIR/c1" --joy-uart-pty
    link_refuse "a live cable alongside a recorded stream" 'a recorded stream' \
        --joy-uart-fifo "$TMP_DIR/c2" --joy-uart-rx "$rx_src"
    link_refuse "a start delay given to a live cable" 'requires' \
        --joy-uart-fifo "$TMP_DIR/c3" --joy-uart-rx-delay-frames 5
    link_refuse "a --joy-uart-fifo path that is not a FIFO" 'not a FIFO' \
        --joy-uart-fifo "$plain"

    if [[ ${#fails[@]} -eq 0 ]]; then
        pass_row " (FIFO pair created, guest readiness byte received with no reader attached, ABC -> BCD round trip through the Z80, 4 refusals verified)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
