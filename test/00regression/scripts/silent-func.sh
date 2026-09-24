#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# --silent test: (1) no audio device is ever opened, and (2) the frames it
# renders are pixel-identical to a normal run — muting output must not skip,
# stall, or otherwise perturb CPU/video execution.
#
#   - SDL's `disk` audio driver only ever creates its output file inside
#     SDL_OpenAudioDeviceStream. An ABSENT file is direct proof no device was opened
#     — stronger than grepping a log line, which would still pass if the
#     message were renamed while the device kept opening underneath it.
#   - The pixel-compare content-verifies that --silent didn't perturb
#     execution. "The process exited around N seconds" would NOT catch a bug
#     that also stalls run_frame(): --delayed-automatic-exit's countdown fires
#     in on_frame_tick() regardless of whether run_frame() itself ran, so a
#     stuck CPU and a live one both exit "on time".
#
# QT_QPA_PLATFORM=offscreen (no xvfb needed) keeps this fast; SDL's disk
# driver needs no display of its own, unlike audio-underrun-func's real
# playback check.
if want silent-func; then
    begin_func silent-func
    raw_normal="$TMP_DIR/silent_normal.raw"
    raw_silent="$TMP_DIR/silent_silent.raw"
    png_normal="$TMP_DIR/silent_normal.png"
    png_silent="$TMP_DIR/silent_silent.png"
    rm -f "$raw_normal" "$raw_silent" "$png_normal" "$png_silent"

    SDL_AUDIODRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE="$raw_normal" QT_QPA_PLATFORM=offscreen \
    timeout --foreground --kill-after=5s 30s "$JNEXT" \
        "${SD_CARD_ARGS[@]}" --machine 48k --rewind-buffer-size 0 \
        --delayed-screenshot "$png_normal" --delayed-screenshot-frames 150 \
        --delayed-automatic-exit 5 &>/dev/null || true

    SDL_AUDIODRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE="$raw_silent" QT_QPA_PLATFORM=offscreen \
    timeout --foreground --kill-after=5s 30s "$JNEXT" \
        "${SD_CARD_ARGS[@]}" --machine 48k --rewind-buffer-size 0 --silent \
        --delayed-screenshot "$png_silent" --delayed-screenshot-frames 150 \
        --delayed-automatic-exit 5 &>/dev/null || true

    if [[ ! -s "$raw_normal" ]]; then
        # NOT a skip (GH #57): this row's whole claim is "--silent opens no
        # audio device, a normal run does". A control run that captured
        # nothing cannot distinguish the two, so the row proves nothing — and
        # SDL's `disk` driver, forced above, needs no backend, no device and
        # no permissions. An empty control means a wrong hint name or a jnext
        # that opened no device, both of which are the defect, not the host.
        fail_row " (control run captured no audio: SDL's disk driver wrote \
nothing — wrong SDL_AUDIO_DISK_OUTPUT_FILE hint name, or jnext opened no audio \
device. Without a control this row cannot tell --silent from a broken build)"
    elif [[ ! -s "$png_normal" || ! -s "$png_silent" ]]; then
        fail_row " (screenshot missing: normal=$([[ -s "$png_normal" ]] && echo y || echo n) silent=$([[ -s "$png_silent" ]] && echo y || echo n))"
    elif [[ -e "$raw_silent" ]]; then
        fail_row " (--silent still opened an audio device)"
    elif $HAS_COMPARE; then
        diff_pixels=$(png_diff "$png_silent" "$png_normal")
        if [[ "$diff_pixels" -eq 0 ]]; then
            pass_row " (no audio device opened; frame 150 pixel-identical to a normal run)"
        else
            fail_row " (--silent changed rendered output: ${diff_pixels} pixels differ)"
        fi
    else
        skip_row " (no ImageMagick — cannot content-verify frame identity)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
