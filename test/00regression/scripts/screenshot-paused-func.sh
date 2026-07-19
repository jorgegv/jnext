#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GUI half of the pending-screenshot loud-failure contract — see
# screenshot-pending-func.sh for the two-case design.

if want screenshot-paused-func; then
    begin_func screenshot-paused-func
    png="$TMP_DIR/paused.png"
    png_ok="$TMP_DIR/paused-ok.png"
    rm -f "$png" "$png_ok"
    bp_nex="$PROJECT_DIR/test/00regression/nex/magic_bp_demo.nex"

    # --magic-breakpoint: the demo's ED FF traps into the debugger and PAUSES it.
    # run_frame() then stops, so the capture (frame 200) is deferred forever and
    # --delayed-automatic-exit (6 s) arrives with it still pending.
    if out=$(QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 60s "$JNEXT" \
                "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
                --magic-breakpoint --load "$bp_nex" \
                --delayed-screenshot "$png" --delayed-screenshot-frames 200 \
                --delayed-automatic-exit 6 2>&1)
    then paused_rc=0; else paused_rc=1; fi

    # Positive control: identical, minus --magic-breakpoint, so it never pauses.
    if QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 60s "$JNEXT" \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 --load "$bp_nex" \
            --delayed-screenshot "$png_ok" --delayed-screenshot-frames 200 \
            --delayed-automatic-exit 6 >/dev/null 2>&1
    then pctrl_rc=0; else pctrl_rc=1; fi

    if [[ "$paused_rc" -ne 0 ]] && [[ ! -f "$png" ]] \
       && echo "$out" | grep -q "NO screenshot was written" \
       && [[ "$pctrl_rc" -eq 0 ]] && [[ -s "$png_ok" ]]; then
        pass_row " (paused debugger: error + exit!=0, no PNG; control writes one)"
    else
        fail_row " (paused_rc=$paused_rc png_exists=$([[ -f "$png" ]] && echo y || echo n) control_rc=$pctrl_rc)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
