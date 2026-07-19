#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# A --delayed-screenshot that is requested and never taken must FAIL LOUDLY:
# an `error` log line and a non-zero exit, never a silent "no PNG, status 0"
# that a CI script would read as success.
#
# Two cases, each with its own positive control so the test cannot pass by
# simply never producing a PNG:
#
#   screenshot-pending-func  headless — --delayed-automatic-exit fires before
#                            the capture comes due.
#   screenshot-paused-func   GUI (Qt, offscreen) — --magic-breakpoint pauses the
#                            debugger, so run_frame() stops and the capture is
#                            deferred; auto-exit then arrives with it still
#                            outstanding. This is the case that has no headless
#                            equivalent (headless has no debugger pause), so it
#                            is driven through the real Qt frontend.
if want screenshot-pending-func; then
    begin_func screenshot-pending-func
    png="$TMP_DIR/pending.png"
    rm -f "$png"
    # Capture due at frame 5000; auto-exit at 1 s (~50 frames). Never taken.
    if out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless \
                "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
                --delayed-screenshot "$png" --delayed-screenshot-frames 5000 \
                --delayed-automatic-exit 1 2>&1)
    then pend_rc=0; else pend_rc=1; fi

    # Positive control: same flags, capture comfortably before the exit.
    png_ok="$TMP_DIR/pending-ok.png"
    rm -f "$png_ok"
    if timeout --foreground --kill-after=5s 60s "$JNEXT" --headless \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
            --delayed-screenshot "$png_ok" --delayed-screenshot-frames 60 \
            --delayed-automatic-exit 5 >/dev/null 2>&1
    then ctrl_rc=0; else ctrl_rc=1; fi

    if [[ "$pend_rc" -ne 0 ]] && [[ ! -f "$png" ]] \
       && echo "$out" | grep -q "NO screenshot was written" \
       && [[ "$ctrl_rc" -eq 0 ]] && [[ -s "$png_ok" ]]; then
        pass_row " (pending capture: error + exit!=0, no PNG; control writes one)"
    else
        fail_row " (pending_rc=$pend_rc png_exists=$([[ -f "$png" ]] && echo y || echo n) control_rc=$ctrl_rc)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
