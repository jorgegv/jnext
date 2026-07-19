#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# --delayed-automatic-exit-frames N: the frames form of the exit
# bound. Two things must hold, and both are proved against the exit's own hard
# contract — a --delayed-screenshot left outstanding at exit errors and exits
# non-zero (see screenshot-pending-func), which makes the exit frame
# OBSERVABLE to the frame.
#
#   exactness  Capture at frame N is taken (exit 0); capture at frame N+1 is
#              NOT (error + exit!=0). One frame either way and one of the two
#              runs flips: an off-by-one in the frame count cannot hide.
#   override   --delayed-automatic-exit 100 (=5000 frames) + -frames 10: the
#              capture at frame 11 must NOT be taken. If seconds won, the exit
#              would be 5000 frames away and the PNG would exist.
if want exit-frames-func; then
    begin_func exit-frames-func
    at_n="$TMP_DIR/exit-frames-at-n.png"       # capture at the exit frame     -> taken
    past_n="$TMP_DIR/exit-frames-past-n.png"   # capture one frame later       -> never taken
    ovr="$TMP_DIR/exit-frames-override.png"    # frames must beat seconds      -> never taken
    rm -f "$at_n" "$past_n" "$ovr"
    N=30

    # 1. Capture due at exactly frame N, exit at frame N: taken, exit 0.
    if timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
            --delayed-screenshot "$at_n" --delayed-screenshot-frames "$N" \
            --delayed-automatic-exit-frames "$N" >/dev/null 2>&1
    then at_rc=0; else at_rc=1; fi

    # 2. Capture due at frame N+1, exit at frame N: never taken, error, exit!=0.
    if past_out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
            --delayed-screenshot "$past_n" --delayed-screenshot-frames "$((N + 1))" \
            --delayed-automatic-exit-frames "$N" 2>&1)
    then past_rc=0; else past_rc=1; fi

    # 3. Both forms given: frames (10) wins over seconds (100 = 5000 frames),
    #    so the capture at frame 11 is never taken.
    if ovr_out=$(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
            --delayed-screenshot "$ovr" --delayed-screenshot-frames 11 \
            --delayed-automatic-exit 100 --delayed-automatic-exit-frames 10 2>&1)
    then ovr_rc=0; else ovr_rc=1; fi

    if [[ "$at_rc" -eq 0 ]] && [[ -s "$at_n" ]] \
       && [[ "$past_rc" -ne 0 ]] && [[ ! -f "$past_n" ]] \
       && echo "$past_out" | grep -q "NO screenshot was written" \
       && [[ "$ovr_rc" -ne 0 ]] && [[ ! -f "$ovr" ]] \
       && echo "$ovr_out" | grep -q "NO screenshot was written"; then
        pass_row " (exit at frame $N exactly; -frames overrides seconds)"
    else
        fail_row " (at_rc=$at_rc at_png=$([[ -s "$at_n" ]] && echo y || echo n) past_rc=$past_rc past_png=$([[ -f "$past_n" ]] && echo y || echo n) override_rc=$ovr_rc override_png=$([[ -f "$ovr" ]] && echo y || echo n))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
