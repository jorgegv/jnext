#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #154 — the ZX Spectrum Next's OWN documented WiFi setup session, executed.
#
# `tbblue/docs/extra-hw/wifi/WIFIand UARTReadME1st.txt` ships with the Next
# distribution and walks a user, at a terminal, through bringing the ESP-01
# online: AT+CWMODE? / AT+CWMODE=1 / AT+CWLAP / AT+CWJAP=... / AT+CIFSR /
# AT+GMR, and later AT+CWQAP. Before GH #154, jnext answered ERROR to the FIRST
# of those lines and to five of the first six: everything that moved bytes
# worked, and the entire radio-configuration half did not exist.
#
# THE HEADLINE ASSERTION IS THEREFORE A NEGATIVE: the whole session must
# contain no `ERROR` at all. That is the defect stated as a test — a user
# following the Next's own instructions inside jnext must not hit a wall.
# The positive assertions below stop that negative from passing vacuously (a
# guest that hung before sending anything also produces no ERROR), by pinning
# the row count and the replies that carry real state.
#
# WHY IT CANNOT FLAKE. No socket, no peer, no port, no screenshot, no audio and
# no wall-clock threshold — this session never leaves the emulator, which is
# what makes it the cheapest row in the ESP group as well as the most direct.
# The guest BLOCKS on a sync byte after every line, so it cannot run ahead of
# the module, and CPU contention only buys the frame budget more wall time.
#
# WHY 48K: the same reason as esp-loopback-func — the UART and the ESP are
# machine-independent in jnext, and 48K gives a quiet machine that is ready to
# be injected into 100 frames in.
if want esp-wifi-setup-func; then
    begin_func esp-wifi-setup-func

    guest_py="$SCRIPT_DIR/esp-wifi-setup-guest.py"
    guest_bin="$TMP_DIR/esp_wifi_guest.bin"
    run_log="$TMP_DIR/esp_wifi_run.log"
    rm -f "$guest_bin" "$run_log"

    if ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available to build the guest binary)"
    else
        python3 "$guest_py" "$guest_bin"

        timeout --foreground --kill-after=5s 90s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" \
            --esp \
            --magic-port 0xCAFE --magic-port-mode line \
            --inject "$guest_bin" --inject-org 8000 --inject-pc 8000 \
            --inject-delay 100 --delayed-automatic-exit-frames 1200 \
            >"$run_log" 2>&1 || true

        # Every spdlog line starts with its `[timestamp]`; the magic port writes
        # with a bare fprintf. So the non-bracketed lines ARE the wire.
        mapfile -t wire < <(grep -av '^\[' "$run_log" || true)
        joined=$(printf '%s\n' "${wire[@]}")

        fails=()

        # A BOUNDED dump for the failure messages. The unbounded one was a real
        # defect found by mutation-testing this row: with a command removed the
        # guest blocks on a sync byte that never arrives, the wire fills with
        # whatever the module said next, and the row's own FAIL text became too
        # long to read — a failure nobody can diagnose is barely better than no
        # failure at all. 30 lines is past the 21 a healthy run produces.
        dump=$(printf '%s|' "${wire[@]:0:30}")
        [[ "${#wire[@]}" -gt 30 ]] && dump+="...(${#wire[@]} lines total)"

        # 1. THE GATE. Not one ERROR in the Next's own documented session.
        #
        # A HERESTRING, NOT `printf ... | grep -q`. The pipe form is unsound
        # under the `set -o pipefail` this script runs with: `grep -q` exits the
        # instant it matches, `printf` then dies of SIGPIPE (141), and pipefail
        # promotes that to the PIPELINE's status — so a match reports as a
        # miss. This row's headline assertion is a NEGATIVE, which makes it
        # exactly the wrong place for a check that can silently answer "not
        # found" when it did find one. `harness-selftest`'s HS-30 refuses the
        # idiom across every suite source, and it caught this one.
        if grep -qx 'ERROR' <<<"$joined"; then
            fails+=("the walk-through produced ERROR: $dump")
        fi

        # 2. A denominator, so a guest that died early cannot pass rule 1 by
        #    saying nothing at all.
        if [[ "${#wire[@]}" -ne 21 ]]; then
            fails+=("wire has ${#wire[@]} lines, expected 21: $dump")
        fi

        # 3. The replies that carry real state, each one a command that
        #    answered ERROR before this change.
        grep -qx '+CWMODE:1' <<<"$joined" \
            || fails+=("no '+CWMODE:1' — the session's very first line")
        grep -qxF '+CWLAP:(3,"JNextWifiHost",-55,"02:00:00:00:00:01",1)' <<<"$joined" \
            || fails+=("no synthetic AP from AT+CWLAP")
        grep -qx 'WIFI CONNECTED' <<<"$joined" || fails+=("no 'WIFI CONNECTED' from AT+CWJAP")
        grep -qx 'WIFI GOT IP'    <<<"$joined" || fails+=("no 'WIFI GOT IP' from AT+CWJAP")
        grep -qx 'WIFI DISCONNECT' <<<"$joined" || fails+=("no 'WIFI DISCONNECT' from AT+CWQAP")

        # 4. THE ORDERING, which is the only part that proves the commands did
        #    something rather than merely answered. The station address must be
        #    present BEFORE the leave and gone AFTER it — the same observable
        #    GH #246 gives a host-scheduled outage, reached here by the guest's
        #    own command.
        # `|| true` ON ALL THREE IS LOAD-BEARING, and mutation-testing this row
        # is what proved it. The script runs under `set -euo pipefail`, so a
        # `grep` that finds nothing returns 1, the pipeline returns 1, and the
        # command substitution takes the whole ROW down with it — the row then
        # prints its name and dies without a verdict, which is worse than a
        # plain FAIL because the suite learns nothing. Removing one command from
        # the dispatch table reproduced exactly that. Now a missing line is an
        # empty string, which the check below reports by name.
        before=$(grep -n '+CIFSR:STAIP,"192.168.1.50"' <<<"$joined" | head -1 | cut -d: -f1 || true)
        leave=$(grep -n 'WIFI DISCONNECT' <<<"$joined" | head -1 | cut -d: -f1 || true)
        after=$(grep -n '+CIFSR:STAIP,"0.0.0.0"' <<<"$joined" | head -1 | cut -d: -f1 || true)
        if [[ -z "$before" || -z "$leave" || -z "$after" ]]; then
            fails+=("missing one of: address before ($before), leave ($leave), no-address after ($after)")
        elif ! [[ "$before" -lt "$leave" && "$leave" -lt "$after" ]]; then
            fails+=("out of order: address=$before leave=$leave no-address=$after")
        fi

        if [[ ${#fails[@]} -eq 0 ]]; then
            pass_row " (the Next's own WiFi walk-through: 8 commands, 21 reply lines, no ERROR)"
        else
            fail_row " (${fails[*]})"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
