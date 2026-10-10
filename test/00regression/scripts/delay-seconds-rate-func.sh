#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# delay-seconds-rate-func (GH #320): the seconds-form delays
# (--delayed-automatic-exit, --delayed-screenshot-time, --delayed-keypress,
# --delayed-nmi) count EMULATED seconds: a frame run at 50 Hz counts 1/50 s, one
# run at 60 Hz counts 1/60 s, and the delay is due on the first frame at which
# the total reaches N (src/platform/cli_delay.h).
#
# WHY THIS ROW EXISTS. All four used to be "N x 50 frames": main.cpp multiplied
# the two it handled, and HeadlessApp::run() sampled the refresh rate ONCE before
# the first frame, when it is always 50 Hz (the VHDL power-on nr_05_5060 is '0'
# and a write lands only at the next frame edge, zxnext.vhd:1302,6697-6700). So a
# program that switches to 60 Hz was timed as if it never had: "2 seconds" was 100
# frames, 1.67 s of that machine's time.
#
# THE ORACLE. beast.nex switches the machine to 60 Hz. Its refresh timeline,
# measured on the unmodified tree: frame 0 at 50 Hz, every frame from 1 on at 60
# Hz (the commit is at the frame edge). Under the definition above, after K
# decrements of the exit/screenshot countdown, taken after each frame, 2 s =
# 600 units of 1/300 s has been charged 6 + 5(K-1); K = 120 is the first with
# 600 reached. A keypress/NMI is decremented BEFORE its frame, so its first
# charge is the initial 50 Hz too and the sum is 6 + 6 + 5(K-2), also K = 120.
# The pre-fix behaviour is 100 in every leg. The 48K control never leaves 50 Hz
# and must stay at 100: a 50 Hz run is frame-for-frame what it always was.
#
# Qt and SDL: the same frame 120, measured on both (5 repeats each, the host
# loaded: load average 11-12 on 12 CPUs); both count per tick, so a catch-up
# frame cannot shift the boundary.
if want delay-seconds-rate-func; then
    begin_func delay-seconds-rate-func

    ds_dir="$TMP_DIR/delay-seconds-rate"
    rm -rf "$ds_dir"; mkdir -p "$ds_dir"
    ds_sdl="$PROJECT_DIR/build/sdl-release/jnext"
    ds_beast="$PROJECT_DIR/test/00regression/nex/beast.nex"
    ds_faults=()

    # ds_run <frontend> <tag> <jnext args...>: run it, print the exit status.
    # The log is $ds_dir/<tag>.log.
    ds_run() {
        local fe=$1 tag=$2 rc=0; shift 2
        case $fe in
            headless)
                timeout --foreground --kill-after=5s 120s "$JNEXT" --headless \
                    "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 "$@" \
                    >"$ds_dir/$tag.log" 2>&1 || rc=$? ;;
            qt)
                env -u WAYLAND_DISPLAY QT_QPA_PLATFORM=offscreen \
                timeout --foreground --kill-after=5s 120s "$JNEXT" --silent \
                    "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 "$@" \
                    >"$ds_dir/$tag.log" 2>&1 || rc=$? ;;
            sdl)
                env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
                timeout --foreground --kill-after=5s 120s "$ds_sdl" --silent \
                    "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 "$@" \
                    >"$ds_dir/$tag.log" 2>&1 || rc=$? ;;
        esac
        echo "$rc"
    }

    # ds_shot <frontend> <tag> <machine> <exit-secs> <frame> [extra args]: a
    # --delayed-automatic-exit <secs> run capturing at <frame>; sets ds_rc and
    # leaves the PNG at $ds_dir/<tag>.png (or none).
    ds_shot() {
        local fe=$1 tag=$2 machine=$3 secs=$4 frame=$5; shift 5
        rm -f "$ds_dir/$tag.png"
        ds_rc=$(ds_run "$fe" "$tag" --machine "$machine" "$@" \
                    --delayed-automatic-exit "$secs" \
                    --delayed-screenshot "$ds_dir/$tag.png" --delayed-screenshot-frames "$frame")
    }
    # ds_expect_taken <tag> <what>: the capture was taken (status 0, a PNG).
    ds_expect_taken() {
        [[ "$ds_rc" == 0 && -s "$ds_dir/$1.png" ]] || ds_faults+=("$1: $2 not taken (rc=$ds_rc)")
    }
    # ds_expect_missed <tag> <what>: the exit came first (status != 0, no PNG, said so).
    ds_expect_missed() {
        if [[ "$ds_rc" == 0 || -e "$ds_dir/$1.png" ]] \
           || ! grep -qF "NO screenshot was written" "$ds_dir/$1.log"; then
            ds_faults+=("$1: $2 was taken or not reported (rc=$ds_rc)")
        fi
    }
    # ds_exit_pair <fe> <tag> <option> <last> <jnext args...>: --delayed-automatic-exit-frames
    # <last>-1 cuts <option> off (error naming it, status != 0); <last> does not.
    ds_exit_pair() {
        local fe=$1 tag=$2 opt=$3 last=$4 rc; shift 4
        rc=$(ds_run "$fe" "$tag-early" "$@" --delayed-automatic-exit-frames $((last - 1)))
        if [[ "$rc" == 0 ]] || ! grep -qF -- "$opt: " "$ds_dir/$tag-early.log" \
           || ! grep -qF "never happened" "$ds_dir/$tag-early.log"; then
            ds_faults+=("$tag: exit at frame $((last - 1)) gave rc=$rc without the $opt error")
        fi
        rc=$(ds_run "$fe" "$tag-late" "$@" --delayed-automatic-exit-frames "$last")
        [[ "$rc" == 0 ]] || ds_faults+=("$tag: exit at frame $last gave rc=$rc")
    }

    if [[ ! -x "$ds_sdl" ]]; then
        fail_row " (SDL-only binary not built: $ds_sdl; run 'make sdl-release')"
    else
        # H1: --delayed-automatic-exit 2 on a guest that runs at 60 Hz.
        ds_shot headless h1-120 next 2 120 --load "$ds_beast"
        ds_expect_taken h1-120 "H1 capture at frame 120"
        ds_shot headless h1-121 next 2 121 --load "$ds_beast"
        ds_expect_missed h1-121 "H1 capture at frame 121"

        # H2: --delayed-screenshot-time 2 is the frame-120 picture, not frame 100's.
        # (The -frames exit bound overrides the 4 s of the two reference runs.)
        ds_rc=$(ds_run headless h2 --machine next --load "$ds_beast" \
                    --delayed-screenshot-time 2 --delayed-screenshot "$ds_dir/h2.png" \
                    --delayed-automatic-exit-frames 200)
        ds_shot headless h2-f120 next 4 120 --load "$ds_beast" --delayed-automatic-exit-frames 200
        ds_shot headless h2-f100 next 4 100 --load "$ds_beast" --delayed-automatic-exit-frames 200
        if [[ ! -s "$ds_dir/h2.png" || ! -s "$ds_dir/h2-f120.png" || ! -s "$ds_dir/h2-f100.png" ]]; then
            ds_faults+=("h2: a capture is missing")
        else
            cmp -s "$ds_dir/h2.png" "$ds_dir/h2-f120.png" || ds_faults+=("h2: --delayed-screenshot-time 2 is not frame 120")
            if cmp -s "$ds_dir/h2.png" "$ds_dir/h2-f100.png"; then
                ds_faults+=("h2: --delayed-screenshot-time 2 is still frame 100")
            fi
        fi

        # H3, H4: the keypress and the NMI press come due at frame 120 too.
        ds_exit_pair headless h3 --delayed-keypress 120 --machine next --load "$ds_beast" \
                                 --delayed-keypress 2 a
        ds_exit_pair headless h4 --delayed-nmi 120 --machine next --load "$ds_beast" \
                                 --delayed-nmi 2 drive

        # H5: the 48K control stays at 50 Hz: 2 s is frame 100, exactly as before.
        ds_shot headless h5-100 48k 2 100
        ds_expect_taken h5-100 "H5 capture at frame 100"
        ds_shot headless h5-101 48k 2 101
        ds_expect_missed h5-101 "H5 capture at frame 101"
        ds_exit_pair headless h5-key --delayed-keypress 100 --machine 48k --delayed-keypress 2 a

        # Q1, S1: the same frame 120 in the two GUI frontends.
        ds_shot qt q1-120 next 2 120 --load "$ds_beast"
        ds_expect_taken q1-120 "Q1 capture at frame 120"
        ds_shot qt q1-121 next 2 121 --load "$ds_beast"
        ds_expect_missed q1-121 "Q1 capture at frame 121"
        ds_shot sdl s1-120 next 2 120 --load "$ds_beast"
        ds_expect_taken s1-120 "S1 capture at frame 120"
        ds_shot sdl s1-121 next 2 121 --load "$ds_beast"
        ds_expect_missed s1-121 "S1 capture at frame 121"

        if [[ ${#ds_faults[@]} -gt 0 ]]; then
            fail_row " ($(IFS=';'; echo "${ds_faults[*]}"))"
        else
            pass_row " (a guest that switches to 60 Hz is timed in emulated seconds: 2 s = frame 120 for exit, screenshot, keypress and NMI in headless, Qt and SDL; a 50 Hz guest stays at frame 100)"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
