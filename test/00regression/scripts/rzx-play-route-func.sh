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
# GH #274 — THIS ROW MOVED TO THE 128K, and what that costs is worth stating.
# It was written because a NEXT recording exposed the routing bug: one route
# replayed it under the boot ROM. RZX recording is refused on a Next now (the
# format carries a 48K/128K/+3 snapshot and an input log of values without
# ports, so a Next recording is either lossy or desynchronised), so that exact
# fixture cannot be built any more — and the code path it exercised is
# unreachable for the same reason. What remains testable, and is what this row
# now tests, is that every ROUTE into playback gives the same picture. The
# machine-of-the-recording half is rzx-machine-func's, on 48K and 128K.
#
# THE MEASUREMENT. A 128K session of a program loaded from a snapshot is
# recorded headless and screenshotted at frame 100: the truth. Then
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
    # The loaded program is a 128K .szx of a running session, built below: the
    # row needs a picture that is NOT a bare boot, and a NEX cannot load on the
    # 128K this row moved to.
    pr_prog="$pr_dir/session.szx"
    pr_rzx="$pr_dir/session.rzx"
    pr_faults=()

    # pr_run <frontend> <tag> <frames> <jnext args...>: one 128K run that
    # screenshots <dir>/<tag>.png after <frames> frames; prints the status.
    pr_run() {
        local fe=$1 tag=$2 n=$3 rc=0; shift 3
        local -a shot=(--delayed-screenshot "$pr_dir/$tag.png" --delayed-screenshot-frames "$n"
                       --delayed-automatic-exit-frames "$((n + 1))")
        case $fe in
            headless)
                timeout --foreground --kill-after=5s 120s "$JNEXT" --headless \
                    "${SD_CARD_ARGS[@]}" --machine 128k "$@" "${shot[@]}" \
                    >"$pr_dir/$tag.log" 2>&1 || rc=$? ;;
            qt)
                env QT_QPA_PLATFORM=offscreen \
                timeout --foreground --kill-after=5s 120s "$JNEXT" --silent \
                    "${SD_CARD_ARGS[@]}" --machine 128k "$@" "${shot[@]}" \
                    >"$pr_dir/$tag.log" 2>&1 || rc=$? ;;
            sdl)
                env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
                timeout --foreground --kill-after=5s 120s "$pr_sdl" --silent \
                    "${SD_CARD_ARGS[@]}" --machine 128k "$@" "${shot[@]}" \
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
        # Fixture: a 128K mid-run .szx. The injected program (DI; LD BC,7FFD;
        # LD A,1F; OUT (C),A — bank 7 at 0xC000, ROM 1, shadow screen shown;
        # fill the shadow screen with 0x47; EI; HALT; JR -3) gives a picture no
        # plain boot produces, and a .szx carries the paging that makes it.
        printf '\xf3\x01\xfd\x7f\x3e\x1f\xed\x79\x21\x00\xc0\x11\x01\xc0\x01\xff\x1a\x36\x47\xed\xb0\xfb\x76\x18\xfd' \
            > "$pr_dir/prog.bin"
        timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine 128k \
            "${SD_CARD_ARGS[@]}" --inject "$pr_dir/prog.bin" --inject-delay 100 \
            --delayed-snapshot "$pr_prog" --delayed-snapshot-frames 150 \
            --delayed-automatic-exit-frames 151 >"$pr_dir/mk-szx.log" 2>&1 || true
        if [[ ! -s "$pr_prog" ]]; then
            fail_row " (could not build the 128K .szx fixture)"
            return 2>/dev/null || true
        fi
        rc=$(pr_run headless truth 100 --load "$pr_prog" --rzx-record "$pr_rzx")
        rc_boot=$(pr_run headless boot 100)
        if [[ "$rc" != 0 || ! -s "$pr_rzx" ]]; then
            fail_row " (could not record the ground truth: rc=$rc)"
        else
            c=$(png_diff "$pr_dir/truth.png" "$pr_dir/boot.png")
            [[ "$rc_boot" == 0 && "$c" -gt 0 && "$c" -lt 999999 ]] \
                || pr_faults+=("the truth equals a plain 128K boot (png_diff=$c, rc=$rc_boot) — proves nothing")
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
                pass_row " (a 128K recording of a loaded program replays pixel-exact via --rzx-play, --load and bare .rzx in headless/Qt/SDL, and via a cold-boot load)"
            fi
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
