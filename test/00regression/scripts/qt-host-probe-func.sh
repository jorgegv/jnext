#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# QtApp's three debugger-backend hosting call sites (GH #276 B5): the SES-07
# driver registration, on_cold_boot_begin()/done() around the cold boot QtApp
# decides on (its post_frames() hard-reset poll), and the pump() in
# post_frames(). The twin of sdl-host-probe-func — see there for the probe
# (src/platform/host_probe.h) and which line each removed call site turns wrong.
#
# Qt polls the hard-reset request in post_frames(), after the frames and before
# the pump — the order SDL and headless use (GH #278 WP2; qt-host-order-func
# pins the order itself) — and it keeps polling while paused, so the probe's
# paused machine is still cold-booted.
# QT_QPA_PLATFORM=offscreen, as screenshot-io-qt-func: no X server needed.
if want qt-host-probe-func; then
    begin_func qt-host-probe-func

    log="$TMP_DIR/qt-host-probe.log"
    rm -f "$log"

    JNEXT_HOST_PROBE=1 QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy \
    timeout --foreground --kill-after=5s 120s \
        "$JNEXT" "${SD_CARD_ARGS[@]}" --silent --machine 48k --rewind-buffer-size 0 \
        --delayed-automatic-exit-frames 150 >"$log" 2>&1 || true

    armed=$(grep -c "HOSTPROBE armed" "$log" || true)
    guest=$(grep -c "HOSTPROBE guest-boot: reset=1 paused=1 owner=probe" "$log" || true)
    reset=$(grep -c "HOSTPROBE reset(Hard) -> ok" "$log" || true)
    seen=$(grep -o "HOSTPROBE [^|]*" "$log" | tr '\n' '|' || true)

    if [[ "$armed" -eq 1 && "$guest" -eq 1 && "$reset" -eq 1 ]]; then
        pass_row " (pumped; guest cold boot bracketed by begin/done; driver registered)"
    else
        fail_row " (armed=$armed guest_ok=$guest reset_ok=$reset: $seen)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
