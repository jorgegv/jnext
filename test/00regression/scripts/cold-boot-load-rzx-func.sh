#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# A menu / cold-boot file-load must route by extension through the SHARED
# dispatch (platform/emulator_boot.h :: emulator_apply_load), so .rzx reaches
# load_rzx, not load_nex. Record a short RZX, then cold-boot-LOAD it via the
# headless reset facility (the SAME shared dispatch the Qt menu uses) and
# assert RZX playback started — misrouting to load_nex leaves no "RZX:
# playback started" line.
if want cold-boot-load-rzx-func; then
    begin_func cold-boot-load-rzx-func
    cb_rzx="$TMP_DIR/cold_boot_test.rzx"
    rm -f "$cb_rzx"
    timeout --foreground --kill-after=5s 40s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --rtc "$NEXTZXOS_RTC" \
        --rzx-record "$cb_rzx" --delayed-automatic-exit-frames 120 >/dev/null 2>&1 || true
    cb_out=""
    if [[ -f "$cb_rzx" ]]; then
        cb_out=$(JNEXT_DELAYED_RESET_FRAMES=420 JNEXT_DELAYED_RESET_TYPE="loadnex:$cb_rzx" \
            timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --rtc "$NEXTZXOS_RTC" \
            --delayed-automatic-exit-frames 520 2>&1 || true)
    fi
    if echo "$cb_out" | grep -q "RZX: playback started"; then
        pass_row " (cold-boot .rzx load routed to RZX playback via shared dispatch)"
    else
        fail_row " (cold-boot .rzx misrouted — shared load dispatch dropped .rzx? [Task 70 review])"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
