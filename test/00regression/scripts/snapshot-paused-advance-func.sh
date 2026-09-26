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
# THE FORMAT IS `.jns`, AND THAT IS NOT INCIDENTAL (GH #274). The machine here
# is a Next — magic_bp_demo is a NEX — and `.jns` is the only snapshot format
# that can represent one. This row used to ask for a `.sna`, which jnext wrote
# as a silent 48K dump; that is the defect #274 fixed, so a `.sna` on a Next is
# now refused and cannot be this row's vehicle. What is under test is unchanged:
# advance_to_frame_boundary() runs BEFORE the extension dispatch in both
# frontends, so the paused-save contract is exercised exactly as before.
#
# WHAT THIS PROVES AND WHAT IT DOES NOT. It proves the end-to-end contract: a
# save requested while paused mid-frame WRITES, exits zero, reloads in a fresh
# process, and says it advanced. It does NOT compare the restored machine to the
# saved one pixel by pixel — that is snapshot-jns-roundtrip-func's job. The
# per-scanline half is pinned at the unit tier, where the oracle exists:
# rewind_test row S6-P7-HISTORY-01 breaks the advance and watches the frame's
# change log vanish (the Task 40 defect).
if want snapshot-paused-advance-func; then
    begin_func snapshot-paused-advance-func
    bp_nex="$PROJECT_DIR/test/00regression/nex/magic_bp_demo.nex"
    paused_jns="$TMP_DIR/paused-advance.jns"
    control_jns="$TMP_DIR/paused-control.jns"
    reload_png="$TMP_DIR/paused-advance-reload.png"
    rm -f "$paused_jns" "$control_jns" "$reload_png"

    # PAUSED: the demo traps into the debugger long before frame 200, so the
    # snapshot comes due on a machine with a frame half-executed.
    if out=$(timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine next \
                "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
                --magic-breakpoint --load "$bp_nex" \
                --delayed-snapshot "$paused_jns" --delayed-snapshot-frames 200 \
                --delayed-automatic-exit 20 2>&1)
    then paused_rc=0; else paused_rc=1; fi

    # CONTROL: identical minus --magic-breakpoint, so the machine is never
    # paused and the advance must NOT report itself. Without this the row
    # could pass on a build that advanced unconditionally.
    if ctrl=$(timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine next \
                "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
                --load "$bp_nex" \
                --delayed-snapshot "$control_jns" --delayed-snapshot-frames 200 \
                --delayed-automatic-exit 20 2>&1)
    then ctrl_rc=0; else ctrl_rc=1; fi

    # And the file a paused save produced must be a real snapshot: a fresh
    # process loads it and renders a frame.
    reload_rc=1
    if [[ -s "$paused_jns" ]] && timeout --foreground --kill-after=5s 60s "$JNEXT" \
            --headless --machine next "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
            --load "$paused_jns" \
            --delayed-screenshot "$reload_png" --delayed-screenshot-frames 2 \
            --delayed-automatic-exit 20 >/dev/null 2>&1
    then reload_rc=0; fi

    advanced=0
    echo "$out"  | grep -q "advanced to the next frame boundary" && advanced=1
    ctrl_quiet=1
    echo "$ctrl" | grep -q "advanced to the next frame boundary" && ctrl_quiet=0

    if [[ "$paused_rc" -eq 0 ]] && [[ -s "$paused_jns" ]] && [[ "$advanced" -eq 1 ]] \
       && [[ "$ctrl_rc" -eq 0 ]] && [[ -s "$control_jns" ]] && [[ "$ctrl_quiet" -eq 1 ]] \
       && [[ "$reload_rc" -eq 0 ]] && [[ -s "$reload_png" ]]; then
        pass_row " (paused save wrote + advanced + reloads; control never advances)"
    else
        fail_row " (paused_rc=$paused_rc advanced=$advanced reload_rc=$reload_rc control_rc=$ctrl_rc ctrl_quiet=$ctrl_quiet)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
