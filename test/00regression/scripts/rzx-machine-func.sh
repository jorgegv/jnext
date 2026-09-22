#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# rzx-machine-func: an RZX recording plays on the machine it was recorded on,
# with no --machine, whichever way the playback is started.
#
# WHY THIS ROW EXISTS. Playback used to run on whatever machine was configured
# — the Next by default — so a 48K or 128K recording replayed without the
# matching --machine went out of step. A jnext recording now names its machine
# in the RZX creator block (rzx::set_recorded_machine()); every boot that plays
# one builds that machine (emulator_boot_machine()). The Next case matters as
# much as the others: a Next recording embeds a 48K SNA, so "48K SNA => 48K"
# would move it off the Next.
#
# THE MEASUREMENT (png_diff; the truth is the recording run's own picture):
#   48k   recorded on 48K from a loaded bifrost .sna; replayed with NO --machine
#         by --rzx-play (headless, Qt, SDL), --load and a bare argument
#         (headless), and a cold-boot load 5 frames into a Next boot (the
#         GUI's File > Play RZX route) — all equal the truth. Played with an
#         explicit --machine next it must DIFFER and warn: --machine wins, and
#         the equalities above are only proof if the wrong machine shows.
#   128k  recorded on 128K from power-on (150 frames into its ROM); replayed
#         with no --machine equals the truth; --machine next differs (the
#         snapshot runs on under another ROM).
#   next  recorded on the Next (test05print.nex); replayed with no --machine,
#         and by a cold-boot load into a 48K boot, equals the truth. This
#         program's picture happens to be the same on a 48K, so the case guards
#         the Next routes, not the choice of machine: emulator_boot_test EB-46
#         pins that a Next recording boots the Next.
if want rzx-machine-func; then
    begin_func rzx-machine-func

    rm_dir="$TMP_DIR/rzx-machine"
    rm -rf "$rm_dir"; mkdir -p "$rm_dir"
    rm_sdl="$PROJECT_DIR/build/sdl-release/jnext"
    rm_faults=()

    # rm_run <frontend> <tag> <frames> <jnext args...>: a run that screenshots
    # <dir>/<tag>.png after <frames> frames; prints the exit status.
    rm_run() {
        local fe=$1 tag=$2 n=$3 rc=0; shift 3
        local -a shot=(--delayed-screenshot "$rm_dir/$tag.png" --delayed-screenshot-frames "$n"
                       --delayed-automatic-exit-frames "$((n + 1))")
        case $fe in
            headless)
                timeout --foreground --kill-after=5s 120s "$JNEXT" --headless \
                    "${SD_CARD_ARGS[@]}" "$@" "${shot[@]}" >"$rm_dir/$tag.log" 2>&1 || rc=$? ;;
            qt)
                env QT_QPA_PLATFORM=offscreen \
                timeout --foreground --kill-after=5s 120s "$JNEXT" --silent \
                    "${SD_CARD_ARGS[@]}" "$@" "${shot[@]}" >"$rm_dir/$tag.log" 2>&1 || rc=$? ;;
            sdl)
                env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
                timeout --foreground --kill-after=5s 120s "$rm_sdl" --silent \
                    "${SD_CARD_ARGS[@]}" "$@" "${shot[@]}" >"$rm_dir/$tag.log" 2>&1 || rc=$? ;;
        esac
        echo "$rc"
    }
    # rm_same <truth> <tag> <rc>: the run exited 0 and its picture is the truth.
    rm_same() {
        local d
        [[ "$3" == 0 ]] || { rm_faults+=("$2: rc=$3"); return; }
        d=$(png_diff "$rm_dir/$2.png" "$rm_dir/$1.png")
        [[ "$d" == 0 ]] || rm_faults+=("$2: differs from the recording (png_diff=$d)")
    }
    # rm_differs <truth> <tag> <rc>: an explicit wrong --machine replays a
    # different picture, and says why.
    rm_differs() {
        local d
        d=$(png_diff "$rm_dir/$2.png" "$rm_dir/$1.png")
        [[ "$3" == 0 && "$d" -gt 0 && "$d" -lt 999999 ]] \
            || rm_faults+=("$2: the wrong machine replays the same picture (png_diff=$d, rc=$3) — proves nothing")
        grep -qF "was recorded on the" "$rm_dir/$2.log" \
            || rm_faults+=("$2: no warning that --machine disagrees with the recording")
    }

    if [[ ! -x "$rm_sdl" ]]; then
        fail_row " (SDL-only binary not built: $rm_sdl; run 'make sdl-release')"
    elif ! $HAS_COMPARE; then
        skip_row " (no ImageMagick — cannot compare the replayed pictures)"
    else
        # Fixture: bifrost running on a 48K.
        timeout --foreground --kill-after=5s 120s "$JNEXT" --headless "${SD_CARD_ARGS[@]}" \
            --machine 48k --load "$PROJECT_DIR/test/00regression/tap/bifrost.tap" \
            --delayed-snapshot "$rm_dir/bifrost.sna" --delayed-snapshot-frames 300 \
            --delayed-automatic-exit-frames 301 >"$rm_dir/mk-sna.log" 2>&1 || true

        r48="$rm_dir/r48.rzx"; r128="$rm_dir/r128.rzx"; rnext="$rm_dir/rnext.rzx"
        rc48=$(rm_run headless t48 100 --machine 48k --load "$rm_dir/bifrost.sna" --rzx-record "$r48")
        rc128=$(rm_run headless t128 150 --machine 128k --rzx-record "$r128")
        rcn=$(rm_run headless tnext 100 --machine next \
                --load "$PROJECT_DIR/test/00regression/nex/test05print.nex" --rzx-record "$rnext")
        if [[ "$rc48" != 0 || "$rc128" != 0 || "$rcn" != 0 ||
              ! -s "$r48" || ! -s "$r128" || ! -s "$rnext" ]]; then
            fail_row " (could not record the ground truths: rc48=$rc48 rc128=$rc128 rcnext=$rcn)"
        else
            for fe in headless qt sdl; do
                rm_same t48 "48-$fe" "$(rm_run "$fe" "48-$fe" 100 --rzx-play "$r48")"
            done
            rm_same t48 48-load "$(rm_run headless 48-load 100 --load "$r48")"
            rm_same t48 48-bare "$(rm_run headless 48-bare 100 "$r48")"
            rm_same t48 48-cold "$(JNEXT_DELAYED_RESET_FRAMES=5 \
                JNEXT_DELAYED_RESET_TYPE="loadnex:$r48" rm_run headless 48-cold 105)"
            rm_differs t48 48-on-next "$(rm_run headless 48-on-next 100 --machine next --rzx-play "$r48")"

            rm_same t128 128-play "$(rm_run headless 128-play 150 --rzx-play "$r128")"
            rm_differs t128 128-on-next "$(rm_run headless 128-on-next 150 --machine next --rzx-play "$r128")"

            rm_same tnext next-play "$(rm_run headless next-play 100 --rzx-play "$rnext")"
            rm_same tnext next-cold "$(JNEXT_DELAYED_RESET_FRAMES=5 \
                JNEXT_DELAYED_RESET_TYPE="loadnex:$rnext" rm_run headless next-cold 105 --machine 48k)"

            if [[ ${#rm_faults[@]} -gt 0 ]]; then
                fail_row " ($(IFS=';'; echo "${rm_faults[*]}"))"
            else
                pass_row " (48K, 128K and Next recordings replay on their own machine without --machine, in every route and frontend; --machine wins, with a warning)"
            fi
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
