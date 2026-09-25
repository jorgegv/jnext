#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #27 S6 — §10.2 P7: ALWAYS ADVANCE, NEVER REFUSE.
#
# A snapshot may only be taken at a frame boundary (Emulator::save_state's own
# contract: "snapshots are only ever taken at a frame boundary, so a restored
# machine has no frame in flight"). The debugger breaks MID-frame, and that is
# precisely when a developer reaches for a save. The owner's decision
# (2026-09-23) overruled the earlier "refuse while paused" recommendation: the
# writer advances to the next begin_new_frame() and saves there, so there is no
# refusal path, no unavailable menu item and no failure mode.
#
# This is the shipped save path (--delayed-snapshot, the same dispatch the GUI's
# File ▸ Save Snapshot uses) driven against a machine that IS paused mid-frame:
# magic_bp_demo.nex executes ED FF, which with --magic-breakpoint activates the
# debugger and pauses it, and run_frame() then returns immediately for the rest
# of the run — the same state screenshot-paused-func drives.
#
# WHAT THIS PROVES AND WHAT IT DOES NOT. It proves the end-to-end contract: a
# save requested while paused mid-frame WRITES, exits zero, reloads in a fresh
# process, and says it advanced. It does NOT prove the restored machine is
# pixel-identical to the one saved — a .sna carries no scheduler queue and no
# per-scanline history for that comparison to have meaning. The per-scanline
# half is pinned at the unit tier instead, where the oracle exists: rewind_test
# row S6-P7-HISTORY-01 breaks the advance and watches the frame's change log
# vanish (the Task 40 defect).
if want snapshot-paused-advance-func; then
    begin_func snapshot-paused-advance-func
    bp_nex="$PROJECT_DIR/test/00regression/nex/magic_bp_demo.nex"
    paused_sna="$TMP_DIR/paused-advance.sna"
    control_sna="$TMP_DIR/paused-control.sna"
    reload_png="$TMP_DIR/paused-advance-reload.png"
    rm -f "$paused_sna" "$control_sna" "$reload_png"

    # PAUSED: the demo traps into the debugger long before frame 200, so the
    # snapshot comes due on a machine with a frame half-executed.
    if out=$(timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine next \
                "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
                --magic-breakpoint --load "$bp_nex" \
                --delayed-snapshot "$paused_sna" --delayed-snapshot-frames 200 \
                --delayed-automatic-exit 20 2>&1)
    then paused_rc=0; else paused_rc=1; fi

    # CONTROL: identical minus --magic-breakpoint, so the machine is never
    # paused and the advance must NOT report itself. Without this the row
    # could pass on a build that advanced unconditionally.
    if ctrl=$(timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine next \
                "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
                --load "$bp_nex" \
                --delayed-snapshot "$control_sna" --delayed-snapshot-frames 200 \
                --delayed-automatic-exit 20 2>&1)
    then ctrl_rc=0; else ctrl_rc=1; fi

    # And the file a paused save produced must be a real snapshot: a fresh
    # process loads it and renders a frame.
    reload_rc=1
    if [[ -s "$paused_sna" ]] && timeout --foreground --kill-after=5s 60s "$JNEXT" \
            --headless --machine next "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
            --load "$paused_sna" \
            --delayed-screenshot "$reload_png" --delayed-screenshot-frames 2 \
            --delayed-automatic-exit 20 >/dev/null 2>&1
    then reload_rc=0; fi

    advanced=0
    echo "$out"  | grep -q "advanced to the next frame boundary" && advanced=1
    ctrl_quiet=1
    echo "$ctrl" | grep -q "advanced to the next frame boundary" && ctrl_quiet=0

    if [[ "$paused_rc" -eq 0 ]] && [[ -s "$paused_sna" ]] && [[ "$advanced" -eq 1 ]] \
       && [[ "$ctrl_rc" -eq 0 ]] && [[ -s "$control_sna" ]] && [[ "$ctrl_quiet" -eq 1 ]] \
       && [[ "$reload_rc" -eq 0 ]] && [[ -s "$reload_png" ]]; then
        pass_row " (paused save wrote + advanced + reloads; control never advances)"
    else
        fail_row " (paused_rc=$paused_rc advanced=$advanced reload_rc=$reload_rc control_rc=$ctrl_rc ctrl_quiet=$ctrl_quiet)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
