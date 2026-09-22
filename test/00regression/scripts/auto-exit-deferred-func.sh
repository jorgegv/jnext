#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# auto-exit-deferred-func: --delayed-automatic-exit(-frames) firing before
# deferred command-line work has happened is an error and a non-zero exit —
# the contract a --delayed-screenshot left outstanding already has
# (screenshot-pending-func), applied to the rest (platform/auto_exit.h).
#
# WHY THIS ROW EXISTS. A .tzx or .wav --load waits 100 frames for BASIC; an
# exit at frame 5 used to end the run with status 0 and the tape never
# attached — a script read "success" for a run that never loaded anything.
#
# THE MEASUREMENT. Each case runs twice: the exit before the work falls due
# must exit non-zero and name the option in an error; the exit after it must
# exit 0 (so the failure is about the timing, not the option):
#   headless  --load x.tzx (48K), --inject with --inject-delay,
#             --delayed-keypress-frames, --delayed-nmi-frames, --rzx-record
#             waiting for its --load, --joy-uart-rx with a start delay, and both
#             edges of the --esp-delayed-(dis)associate-frames outage
#   qt, sdl   --load x.tzx (the GUI frontends have no keypress/NMI options)
if want auto-exit-deferred-func; then
    begin_func auto-exit-deferred-func

    ad_dir="$TMP_DIR/auto-exit-deferred"
    rm -rf "$ad_dir"; mkdir -p "$ad_dir"
    ad_sdl="$PROJECT_DIR/build/sdl-release/jnext"
    ad_tzx="$PROJECT_DIR/test/tzx/GhostlyGrange.tzx"
    ad_faults=()
    printf '\xf3\x76' > "$ad_dir/prog.bin"          # DI; HALT
    printf 'jnext' > "$ad_dir/serial.bin"

    # ad_run <frontend> <tag> <exit-frame> <jnext args...>; prints the status.
    ad_run() {
        local fe=$1 tag=$2 n=$3 rc=0; shift 3
        case $fe in
            headless)
                timeout --foreground --kill-after=5s 60s "$JNEXT" --headless \
                    "${SD_CARD_ARGS[@]}" --machine 48k "$@" \
                    --delayed-automatic-exit-frames "$n" >"$ad_dir/$tag.log" 2>&1 || rc=$? ;;
            qt)
                env QT_QPA_PLATFORM=offscreen \
                timeout --foreground --kill-after=5s 60s "$JNEXT" --silent \
                    "${SD_CARD_ARGS[@]}" --machine 48k "$@" \
                    --delayed-automatic-exit-frames "$n" >"$ad_dir/$tag.log" 2>&1 || rc=$? ;;
            sdl)
                env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
                timeout --foreground --kill-after=5s 60s "$ad_sdl" --silent \
                    "${SD_CARD_ARGS[@]}" --machine 48k "$@" \
                    --delayed-automatic-exit-frames "$n" >"$ad_dir/$tag.log" 2>&1 || rc=$? ;;
        esac
        echo "$rc"
    }
    # ad_case <frontend> <tag> <option> <early> <late> <jnext args...>: an exit
    # at frame <early> cuts <option> off (error naming it, status != 0); one at
    # frame <late> does not (status 0).
    ad_case() {
        local fe=$1 tag=$2 opt=$3 early=$4 late=$5 rc; shift 5
        rc=$(ad_run "$fe" "$tag-early" "$early" "$@")
        if [[ "$rc" == 0 ]] || ! grep -qF -- "$opt: " "$ad_dir/$tag-early.log" \
           || ! grep -qF "never happened" "$ad_dir/$tag-early.log"; then
            ad_faults+=("$tag: exit at frame $early gave rc=$rc without the $opt error")
        fi
        rc=$(ad_run "$fe" "$tag-late" "$late" "$@")
        [[ "$rc" == 0 ]] || ad_faults+=("$tag: exit at frame $late (after the work) gave rc=$rc")
    }

    if [[ ! -x "$ad_sdl" ]]; then
        fail_row " (SDL-only binary not built: $ad_sdl; run 'make sdl-release')"
    else
        ad_case headless load   --load 5 150 --load "$ad_tzx"
        ad_case headless inject --inject 5 60 --inject "$ad_dir/prog.bin" --inject-delay 40
        ad_case headless key    --delayed-keypress 5 60 --delayed-keypress-frames 40 space
        ad_case headless nmi    --delayed-nmi 5 60 --delayed-nmi-frames 40 drive
        ad_case headless record --rzx-record 5 150 --load "$ad_tzx" --rzx-record "$ad_dir/r.rzx"
        ad_case headless uart   --joy-uart-rx 5 60 --joy-uart-rx "$ad_dir/serial.bin" \
                                --joy-uart-rx-delay-frames 40
        ad_case headless esp-down --esp-delayed-disassociate-frames 5 80 --esp \
                                --esp-delayed-disassociate-frames 40 --esp-delayed-associate-frames 60
        ad_case headless esp-up --esp-delayed-associate-frames 50 80 --esp \
                                --esp-delayed-disassociate-frames 40 --esp-delayed-associate-frames 60
        ad_case qt  qt-load  --load 5 150 --load "$ad_tzx"
        ad_case sdl sdl-load --load 5 150 --load "$ad_tzx"

        if [[ ${#ad_faults[@]} -gt 0 ]]; then
            fail_row " ($(IFS=';'; echo "${ad_faults[*]}"))"
        else
            pass_row " (an auto-exit before a deferred --load/--inject/keypress/NMI/--rzx-record/serial/ESP edge fails the run, in headless, Qt and SDL)"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
