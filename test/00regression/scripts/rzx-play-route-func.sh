#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# rzx-play-route-func: every way of starting RZX playback starts from the same
# machine, so one recording replays identically whichever way it is played.
#
# WHY THIS ROW EXISTS. Starting a playback is `init()` followed by
# Emulator::load_rzx(), and EmulatorConfig::load_file changes what init()
# builds: on the Next an empty one arms the boot-ROM overlay. --load x.rzx, a
# bare x.rzx, a cold-boot load (the route the GUI's File > Open takes) set it;
# --rzx-play did not, and the GUI's Play RZX item played on the running
# machine with no reboot at all. A Next program that runs through the ROM then
# replayed its recording under the boot ROM — a different picture from the
# same file. rzx-frontends-func could not see it: it records a 48K machine,
# where load_file changes nothing.
#
# THE MEASUREMENT. A Next session of test05print.nex (which prints through the
# ROM) is recorded headless and screenshotted at frame 100: the truth. Then
# each of these must give a picture identical to it at the same point of the
# replay (png_diff 0):
#   --rzx-play, --load x.rzx, bare x.rzx     in headless, the Qt GUI and SDL
#   a cold-boot load of x.rzx after 5 frames (headless; the shared cold boot
#                                             the GUI's menu items use)
# and the truth must DIFFER from a plain Next boot at frame 100, or the
# equalities would prove nothing. (The GUI menu items reaching that cold boot
# is rzx_menu_test RZXGUI-10's business.)
if want rzx-play-route-func; then
    begin_func rzx-play-route-func

    pr_dir="$TMP_DIR/rzx-play-route"
    rm -rf "$pr_dir"; mkdir -p "$pr_dir"
    pr_sdl="$PROJECT_DIR/build/sdl-release/jnext"
    pr_nex="$PROJECT_DIR/test/00regression/nex/test05print.nex"
    pr_rzx="$pr_dir/session.rzx"
    pr_faults=()

    # pr_run <frontend> <tag> <frames> <jnext args...>: one Next run that
    # screenshots <dir>/<tag>.png after <frames> frames; prints the status.
    pr_run() {
        local fe=$1 tag=$2 n=$3 rc=0; shift 3
        local -a shot=(--delayed-screenshot "$pr_dir/$tag.png" --delayed-screenshot-frames "$n"
                       --delayed-automatic-exit-frames "$((n + 1))")
        case $fe in
            headless)
                timeout --foreground --kill-after=5s 120s "$JNEXT" --headless \
                    "${SD_CARD_ARGS[@]}" --machine next "$@" "${shot[@]}" \
                    >"$pr_dir/$tag.log" 2>&1 || rc=$? ;;
            qt)
                env QT_QPA_PLATFORM=offscreen \
                timeout --foreground --kill-after=5s 120s "$JNEXT" --silent \
                    "${SD_CARD_ARGS[@]}" --machine next "$@" "${shot[@]}" \
                    >"$pr_dir/$tag.log" 2>&1 || rc=$? ;;
            sdl)
                env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
                timeout --foreground --kill-after=5s 120s "$pr_sdl" --silent \
                    "${SD_CARD_ARGS[@]}" --machine next "$@" "${shot[@]}" \
                    >"$pr_dir/$tag.log" 2>&1 || rc=$? ;;
        esac
        echo "$rc"
    }
    # pr_same <tag> <rc>: the run exited 0 and its picture is the truth.
    pr_same() {
        local d
        [[ "$2" == 0 ]] || { pr_faults+=("$1: rc=$2"); return; }
        d=$(png_diff "$pr_dir/$1.png" "$pr_dir/truth.png")
        [[ "$d" == 0 ]] || pr_faults+=("$1: differs from the recording (png_diff=$d)")
    }

    if [[ ! -x "$pr_sdl" ]]; then
        fail_row " (SDL-only binary not built: $pr_sdl; run 'make sdl-release')"
    elif ! $HAS_COMPARE; then
        skip_row " (no ImageMagick — cannot compare the replayed pictures)"
    else
        rc=$(pr_run headless truth 100 --load "$pr_nex" --rzx-record "$pr_rzx")
        rc_boot=$(pr_run headless boot 100)
        if [[ "$rc" != 0 || ! -s "$pr_rzx" ]]; then
            fail_row " (could not record the ground truth: rc=$rc)"
        else
            c=$(png_diff "$pr_dir/truth.png" "$pr_dir/boot.png")
            [[ "$rc_boot" == 0 && "$c" -gt 0 && "$c" -lt 999999 ]] \
                || pr_faults+=("the truth equals a plain Next boot (png_diff=$c, rc=$rc_boot) — proves nothing")
            for fe in headless qt sdl; do
                pr_same "$fe-rzx-play" "$(pr_run "$fe" "$fe-rzx-play" 100 --rzx-play "$pr_rzx")"
                pr_same "$fe-load"     "$(pr_run "$fe" "$fe-load" 100 --load "$pr_rzx")"
                pr_same "$fe-bare"     "$(pr_run "$fe" "$fe-bare" 100 "$pr_rzx")"
            done
            # The cold boot: 5 frames of a plain boot, then the frontend's own
            # cold boot loads the recording, which then runs 100 frames.
            rc=$(JNEXT_DELAYED_RESET_FRAMES=5 JNEXT_DELAYED_RESET_TYPE="loadnex:$pr_rzx" \
                    pr_run headless cold-boot 105)
            pr_same cold-boot "$rc"

            if [[ ${#pr_faults[@]} -gt 0 ]]; then
                fail_row " ($(IFS=';'; echo "${pr_faults[*]}"))"
            else
                pass_row " (a Next recording replays pixel-exact via --rzx-play, --load and bare .rzx in headless/Qt/SDL, and via a cold-boot load)"
            fi
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
