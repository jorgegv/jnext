#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# SdlApp's three debugger-backend hosting call sites (GH #276 B5, carried from
# B4's reviews): the SES-07 driver registration, on_cold_boot_begin()/done()
# around the cold boot SdlApp itself decides on, and one pump() per tick.
# With no client attached none of them changes what a run does, so every other
# SDL row exercises all three and asserts none — only HeadlessApp's are pinned
# (HOST-03..05 in debugger_backend_test).
#
# THE OBSERVABLE is the JNEXT_HOST_PROBE fixture (src/platform/host_probe.h): a
# client that runs INSIDE pump() as a Service. It pauses the machine as itself
# and raises the guest hard-reset request (the flag F1 and NR 0x02 set), lets
# SdlApp's own poll cold-boot, reports what came back, resumes, and then asks
# the backend for reset(Hard). HOST-07 proves the same probe through the real
# HeadlessApp. Each removed call site turns exactly one line wrong:
#
#   pump() removed          no "guest-boot:" line at all (the probe never runs)
#   done removed            "guest-boot: reset=0" (no Reset{Hard} pushed)
#   begin removed           "guest-boot: reset=1 paused=0" (the rebuilt machine
#                           comes back RUNNING — a done with no begin, CTL-12-44)
#   driver not registered   "reset(Hard) -> refused_unavailable"
#
# SDL_VIDEODRIVER=dummy, as sdl-render-func: no X server, no key delivery
# needed — the probe drives everything from inside the loop.
if want sdl-host-probe-func; then
    begin_func sdl-host-probe-func

    sdl_bin="$PROJECT_DIR/build/sdl-release/jnext"
    log="$TMP_DIR/sdl-host-probe.log"
    rm -f "$log"

    if [[ ! -x "$sdl_bin" ]]; then
        # A build artifact the Makefile guarantees, so this is loud.
        fail_row " (SDL-only binary not built: $sdl_bin; run 'make sdl-release')"
    else
        env -u WAYLAND_DISPLAY JNEXT_HOST_PROBE=1 SDL_VIDEODRIVER=dummy \
            SDL_AUDIODRIVER=dummy \
        timeout --foreground --kill-after=5s 120s \
            "$sdl_bin" --silent "${SD_CARD_ARGS[@]}" --machine 48k \
            --delayed-automatic-exit-frames 150 >"$log" 2>&1 || true

        sdl_init_re="SDL_Init:|SDL_CreateWindow:|SDL_CreateRenderer:|SDL_CreateTexture:"
        armed=$(grep -c "HOSTPROBE armed" "$log" || true)
        guest=$(grep -c "HOSTPROBE guest-boot: reset=1 paused=1 owner=probe" "$log" || true)
        reset=$(grep -c "HOSTPROBE reset(Hard) -> ok" "$log" || true)
        seen=$(grep -o "HOSTPROBE [^|]*" "$log" | tr '\n' '|' || true)

        if [[ "$armed" -eq 0 ]] && grep -qE "$sdl_init_re" "$log"; then
            skip_row " (SDL could not initialise on this host; the probe never started)"
        elif [[ "$armed" -eq 1 && "$guest" -eq 1 && "$reset" -eq 1 ]]; then
            pass_row " (pumped; guest cold boot bracketed by begin/done; driver registered)"
        else
            fail_row " (armed=$armed guest_ok=$guest reset_ok=$reset: $seen)"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
