#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# Audio underrun test: verify the emulator's audio clock tracks the sound
# card's, so the device queue never runs dry.
#
# Audio is synthesised on the EMULATED clock (44100 samples per emulated
# second). A 48K frame is 69888 T @3.5 MHz = 1/50.08 s, so pacing emulation on a
# 20 ms wall-clock timer (50.00 fps) synthesises only ~44030 samples per REAL
# second while the sound card consumes 44100. The device queue drains to empty
# and SDL pads it with ZEROS — a hole punched into a live waveform, i.e. an
# audible click, a few times a second, for as long as the emulator runs.
#
# Captured via SDL's `disk` audio driver (raw S16LE stereo 44100), which writes
# exactly what jnext hands the sound card. --headless has no audio at all, so
# this must run the GUI binary under a virtual X server.
if want audio-underrun-func; then
    begin_func audio-underrun-func
    tone_bin="$SCRIPT_DIR/bin/beeper_tone.bin"
    checker="$SCRIPT_DIR/check-audio-underruns.py"
    raw_file="$TMP_DIR/audio_underrun.raw"
    if ! command -v xvfb-run &>/dev/null; then
        skip_row " (xvfb-run not available; audio needs a display)"
    elif ! command -v python3 &>/dev/null; then
        skip_row " (python3 not available for capture analysis)"
    else
        rm -f "$raw_file"
        # A bare 18-byte square-wave loop: sound starts immediately, with none
        # of the tape-fastload burst that would pre-fill the queue and mask the
        # leak. This is the ONE test that cannot use --headless (it needs a
        # real audio path), so it runs the windowed emulator under xvfb-run.
        # xvfb-run only sets DISPLAY, and on a Wayland session Qt/SDL prefer
        # the Wayland backend — so WAYLAND_DISPLAY is unset and the X11
        # backends forced, making Qt render into the Xvfb display instead of
        # the real compositor. Deliberately NOT done: pointing WAYLAND_DISPLAY
        # at a dead socket — that makes SDL fail to bring up an audio backend
        # and the test SKIPs, trading a harmless residual probe connect for a
        # silently-disabled test.
        SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE="$raw_file" \
        timeout --foreground --kill-after=5s 40s \
        env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=xcb SDL_VIDEODRIVER=x11 \
        xvfb-run -a "$JNEXT" \
            "${SD_CARD_ARGS[@]}" \
            --machine 48k \
            --inject "$tone_bin" --inject-org 8000 --inject-pc 8000 --inject-delay 100 \
            --delayed-automatic-exit 12 &>/dev/null || true
        if [[ ! -s "$raw_file" ]]; then
            skip_row " (no audio captured; no SDL audio backend?)"
        elif underrun_out=$(python3 "$checker" "$raw_file" --skip-secs 3 2>&1); then
            pass_row " (no audio underruns)"
        else
            fail_row " ($(echo "$underrun_out" | head -1))"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
