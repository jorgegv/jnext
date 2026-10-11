#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #321 (R2, startup route): `--machine next --load x48.jns` must run the 48K
# the snapshot was taken on, and a reset afterwards must boot the 48K too.
# load_jns() reconfigures to the snapshot's machine; before the fix the startup
# booted the explicit Next first and the frontend kept naming it, so the first
# reset built a Next. The unit rows (QS-17/18) pin the helper; this pins that
# main.cpp actually calls it: reverting main.cpp to `if (!machine_type_set)`
# leaves every unit row green and turns this one red.
#
# Run 1 makes a 48K .jns; run 2 starts `--machine next --load` it, and a hard
# reset is scheduled after the load (JNEXT_DELAYED_RESET_FRAMES, HeadlessApp).
# Every "Initializing emulator: machine_type=" line of run 2 must be the 48K and
# the reset must have happened (a second init), so a Next anywhere is the bug.
if want snapshot-startup-machine-func; then
    begin_func snapshot-startup-machine-func
    snap="$TMP_DIR/startup48.jns"
    log1="$TMP_DIR/startup_make.log"; log2="$TMP_DIR/startup_load.log"
    rm -f "$snap" "$log1" "$log2"
    timeout --foreground --kill-after=5s 90s "$JNEXT" --headless --machine 48k \
        --delayed-snapshot "$snap" --delayed-snapshot-frames 100 \
        --delayed-automatic-exit-frames 150 >"$log1" 2>&1 || true
    JNEXT_DELAYED_RESET_FRAMES=250 JNEXT_DELAYED_RESET_TYPE=hard \
    timeout --foreground --kill-after=5s 90s "$JNEXT" --headless --machine next \
        --load "$snap" --delayed-automatic-exit-frames 400 >"$log2" 2>&1 || true
    if [[ ! -s "$snap" ]]; then
        fail_row " (the 48K .jns was not written: $(grep -m1 -E '\[(error|critical)\]' "$log1" || echo 'no error logged'))"
    else
        n_init=$(grep -c 'Initializing emulator: machine_type=' "$log2" || true)
        n_next=$(grep -c 'Initializing emulator: machine_type=0\[ZX Next\]' "$log2" || true)
        n_48=$(grep -c 'Initializing emulator: machine_type=1\[48K\]' "$log2" || true)
        loaded=$(grep -c 'Snapshot loaded' "$log2" || true)
        if [[ "$loaded" -lt 1 ]]; then
            fail_row " (the .jns was not loaded: $(grep -m1 -E '\[(error|critical)\]' "$log2" || echo 'no error logged'))"
        elif [[ "$n_init" -lt 2 ]]; then
            fail_row " (the scheduled reset did not happen: $n_init machine init(s) logged)"
        elif [[ "$n_next" -ne 0 || "$n_48" -ne "$n_init" ]]; then
            fail_row " (--machine next --load 48K.jns booted a Next: $n_next Next init(s), $n_48 48K of $n_init)"
        else
            pass_row " (--machine next --load a 48K .jns: all $n_init machine inits, the reset's included, are the 48K)"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
