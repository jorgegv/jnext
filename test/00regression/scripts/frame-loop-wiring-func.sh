#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# frame-loop-wiring-func (GH #155): the real Qt frontend's frame-timer and
# tick-entry-paint WIRING. The unit suites pin the helpers (frame_timer_test,
# present_count_test); only a running QtApp can show that it actually uses
# them, so this row runs the real GUI binary (offscreen platform, --silent) and
# reads its own `ticks:` debug lines, one per second:
#
#   timer re-armed=N  must equal the window's tick count n: every tick arms
#                     the single-shot frame timer at its end. Restoring the old
#                     "only when the interval changed" guard makes it smaller.
#   rescued=R         must be 0: the post-tick check never had to re-anchor a
#                     tick that ended with the timer unarmed (that guard would
#                     have it fire on every same-value tick).
#   pending-paint checked=C  must equal n: the tick-entry paint is asked on
#                     every tick. Its `painted` count is not asserted — on Linux
#                     the event loop always paints first, so it is ~0 here; it
#                     is the Windows residual (see gui/frame_timer.h).
#
# Counting, not timing: the assertions hold on a loaded host too.
if want frame-loop-wiring-func; then
    begin_func frame-loop-wiring-func
    flw_out=$(QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 60s "$JNEXT" \
        "${SD_CARD_ARGS[@]}" --machine 48k --silent --log-level platform=debug \
        --delayed-automatic-exit-frames 250 2>&1) || true
    flw_lines=$(grep -E '\] ticks: n=[0-9]+ .*timer re-armed=[0-9]+ rescued=[0-9]+ \| pending-paint checked=[0-9]+ painted=[0-9]+' <<<"$flw_out" || true)
    flw_windows=0 flw_bad=0 flw_first_bad=""
    while IFS= read -r line; do
        [[ -n "$line" ]] || continue
        n=$(sed -E 's/.*ticks: n=([0-9]+) .*/\1/' <<<"$line")
        re=$(sed -E 's/.*timer re-armed=([0-9]+) .*/\1/' <<<"$line")
        rs=$(sed -E 's/.*rescued=([0-9]+) .*/\1/' <<<"$line")
        ck=$(sed -E 's/.*pending-paint checked=([0-9]+) .*/\1/' <<<"$line")
        flw_windows=$((flw_windows + 1))
        if [[ "$re" -ne "$n" || "$rs" -ne 0 || "$ck" -ne "$n" ]]; then
            flw_bad=$((flw_bad + 1))
            [[ -n "$flw_first_bad" ]] || flw_first_bad="n=$n re-armed=$re rescued=$rs checked=$ck"
        fi
    done <<<"$flw_lines"
    if [[ "$flw_windows" -ge 3 && "$flw_bad" -eq 0 ]]; then
        pass_row " ($flw_windows windows: every tick re-armed the timer, 0 rescues, tick-entry paint asked every tick)"
    else
        fail_row " (windows=$flw_windows (want >=3), bad=$flw_bad, first bad: ${flw_first_bad:-none})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
