#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# rzx-loaded-program-func: an RZX recording of a program the command line
# LOADED replays that program — the picture after N recorded frames equals the
# picture after the same N replayed frames.
#
# WHY THIS ROW EXISTS. Four ways it did not:
#   1. --rzx-record started at the top of run(), but the --load is applied at
#      the first frame, so the snapshot the file embeds was the machine from
#      BEFORE the load, and playback never loaded the program (all frontends).
#   2. The embedded 48K SNA had no paging, so a 128K program with its own
#      7FFD state replayed against the wrong banks.
#   3. A fast-load tape is loaded by a ROM trap that does the loader's work
#      without executing it; nothing a recording holds can replay that.
#   4. The embedded SNA always said border 0.
# rzx-frontends-func never saw any of it: its recordings start from a bare boot.
#
# THE MEASUREMENT (png_diff; each case also checks its picture is NOT a plain
# boot, or the equality would prove nothing):
#   sna    48K, --load x.sna (bifrost, snapshotted mid-run): recorded in
#          headless, Qt and SDL; each file replayed headless.
#   szx    128K, --load x.szx of a program that paged bank 7 in and shows the
#          shadow screen (headless).
#   nex    REMOVED with GH #274 — it recorded on a Next, and RZX recording is
#          refused there now (the format carries a 48K/128K/+3 snapshot and an
#          input log of values without ports; a Next recording is either lossy
#          or desynchronised). The `.nex` half of what it proved — that the
#          recording's snapshot holds the LOADED program and not a bare boot —
#          is still proved by the sna, szx and tap cases on the machines that
#          can record. The refusal itself is asserted by rzx-machine-func and by
#          the cli case below.
#   tap    48K, --load bifrost.tap: the recording loads the tape in real time
#          (the log says so) and replays it (headless).
#   cli    the combinations that cannot work are refused up front, status 1:
#          playing and recording at once, two RZX files, --load or --inject
#          with a playback, --tape-save with RZX.
if want rzx-loaded-program-func; then
    begin_func rzx-loaded-program-func

    lp_dir="$TMP_DIR/rzx-loaded-program"
    rm -rf "$lp_dir"; mkdir -p "$lp_dir"
    lp_sdl="$PROJECT_DIR/build/sdl-release/jnext"
    lp_faults=()

    # lp_run <frontend> <machine> <log> <jnext args...>; prints the status.
    lp_run() {
        local fe=$1 m=$2 log=$3 rc=0; shift 3
        case $fe in
            headless)
                timeout --foreground --kill-after=5s 120s "$JNEXT" --headless \
                    "${SD_CARD_ARGS[@]}" --machine "$m" "$@" >"$log" 2>&1 || rc=$? ;;
            qt)
                env QT_QPA_PLATFORM=offscreen \
                timeout --foreground --kill-after=5s 120s "$JNEXT" --silent \
                    "${SD_CARD_ARGS[@]}" --machine "$m" "$@" >"$log" 2>&1 || rc=$? ;;
            sdl)
                env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
                timeout --foreground --kill-after=5s 120s "$lp_sdl" --silent \
                    "${SD_CARD_ARGS[@]}" --machine "$m" "$@" >"$log" 2>&1 || rc=$? ;;
        esac
        echo "$rc"
    }
    # lp_shot <frontend> <machine> <tag> <frames> <jnext args...>: a run that
    # screenshots <dir>/<tag>.png after <frames> frames; prints the status.
    lp_shot() {
        local fe=$1 m=$2 tag=$3 n=$4; shift 4
        lp_run "$fe" "$m" "$lp_dir/$tag.log" "$@" \
            --delayed-screenshot "$lp_dir/$tag.png" --delayed-screenshot-frames "$n" \
            --delayed-automatic-exit-frames "$((n + 1))"
    }
    # lp_round <frontend> <machine> <tag> <frames> <load args...>: record the
    # loaded program with <frontend>, replay the file headless, compare.
    lp_round() {
        local fe=$1 m=$2 tag=$3 n=$4 rc d c; shift 4
        rc=$(lp_shot "$fe" "$m" "$tag-rec" "$n" "$@" --rzx-record "$lp_dir/$tag.rzx")
        [[ "$rc" == 0 ]] || { lp_faults+=("$tag: recording run rc=$rc"); return; }
        rc=$(lp_shot headless "$m" "$tag-play" "$n" --rzx-play "$lp_dir/$tag.rzx")
        [[ "$rc" == 0 ]] || { lp_faults+=("$tag: playback run rc=$rc"); return; }
        d=$(png_diff "$lp_dir/$tag-rec.png" "$lp_dir/$tag-play.png")
        c=$(png_diff "$lp_dir/$tag-rec.png" "$lp_dir/boot-$m.png")
        [[ "$d" == 0 ]] || lp_faults+=("$tag: replay differs from the recording (png_diff=$d)")
        [[ "$c" -gt 0 && "$c" -lt 999999 ]] \
            || lp_faults+=("$tag: the recorded picture is a plain boot (png_diff=$c) — proves nothing")
    }

    if [[ ! -x "$lp_sdl" ]]; then
        fail_row " (SDL-only binary not built: $lp_sdl; run 'make sdl-release')"
    elif ! $HAS_COMPARE; then
        skip_row " (no ImageMagick — cannot compare the replayed pictures)"
    else
        # Fixtures: plain boots to compare against, a 48K snapshot of bifrost
        # running, and a 128K snapshot of a program with bank 7 paged in.
        lp_shot headless 48k boot-48k 100 >/dev/null
        lp_shot headless 128k boot-128k 60 >/dev/null
        lp_shot headless next boot-next 100 >/dev/null
        lp_run headless 48k "$lp_dir/mk-sna.log" \
            --load "$PROJECT_DIR/test/00regression/tap/bifrost.tap" \
            --delayed-snapshot "$lp_dir/bifrost.sna" --delayed-snapshot-frames 300 \
            --delayed-automatic-exit-frames 301 >/dev/null
        # DI; LD BC,7FFD; LD A,1F; OUT (C),A (bank 7 at C000, ROM 1, shadow
        # screen shown); LD HL,C000; LD DE,C001; LD BC,1AFF; LD (HL),47; LDIR
        # (fill the shadow screen); EI; HALT; JR -3.
        printf '\xf3\x01\xfd\x7f\x3e\x1f\xed\x79\x21\x00\xc0\x11\x01\xc0\x01\xff\x1a\x36\x47\xed\xb0\xfb\x76\x18\xfd' \
            > "$lp_dir/page7.bin"
        lp_run headless 128k "$lp_dir/mk-szx.log" \
            --inject "$lp_dir/page7.bin" --inject-delay 100 \
            --delayed-snapshot "$lp_dir/page7.szx" --delayed-snapshot-frames 150 \
            --delayed-automatic-exit-frames 151 >/dev/null

        if [[ ! -s "$lp_dir/bifrost.sna" || ! -s "$lp_dir/page7.szx" ]]; then
            lp_faults+=("could not make the snapshot fixtures")
        else
            for fe in headless qt sdl; do
                lp_round "$fe" 48k "sna-$fe" 100 --load "$lp_dir/bifrost.sna"
            done
            lp_round headless 128k szx 60 --load "$lp_dir/page7.szx"
            lp_round headless 48k tap 300 --load "$PROJECT_DIR/test/00regression/tap/bifrost.tap"
            grep -qF "TAP: switched to real-time loading" "$lp_dir/tap-rec.log" \
                || lp_faults+=("tap: the recording did not switch the tape to real-time loading")
        fi

        # cli: refused before anything runs.
        lp_ok_rzx="$lp_dir/sna-headless.rzx"
        printf '\x00' > "$lp_dir/x.bin"
        declare -A lp_cli=(
            [play+record]="--rzx-play $lp_ok_rzx --rzx-record $lp_dir/o.rzx"
            [two-rzx]="--load $lp_ok_rzx --rzx-play $lp_ok_rzx"
            [load+play]="--load $lp_dir/bifrost.sna --rzx-play $lp_ok_rzx"
            [inject+play]="--inject $lp_dir/x.bin --rzx-play $lp_ok_rzx"
            [tapesave+record]="--tape-save $lp_dir/t.tap --rzx-record $lp_dir/o.rzx"
        )
        # GH #274 — recording on a Next is refused too, and it is the refusal a
        # user meets by accident, because the Next is the DEFAULT machine. It is
        # driven separately from the table above because that table runs every
        # case on 48k, which is exactly the machine this one must not use.
        rc=$(lp_run headless next "$lp_dir/cli-next-record.log" \
                --rzx-record "$lp_dir/o.rzx" --delayed-automatic-exit-frames 5)
        [[ "$rc" != 0 ]] \
            || lp_faults+=("next-record: recording on a Next exited 0; it must be refused")
        [[ ! -f "$lp_dir/o.rzx" ]] \
            || lp_faults+=("next-record: a file was written for a refused recording")
        grep -qF "cannot record on a ZX Spectrum Next" "$lp_dir/cli-next-record.log" \
            || lp_faults+=("next-record: no refusal naming the machine")
        grep -qF -e "--machine 48k" "$lp_dir/cli-next-record.log" \
            || lp_faults+=("next-record: the refusal does not say which machines CAN record")
        for k in "${!lp_cli[@]}"; do
            # shellcheck disable=SC2086  # the case's arguments, word-split on purpose
            rc=$(lp_run headless 48k "$lp_dir/cli-$k.log" ${lp_cli[$k]} \
                    --delayed-automatic-exit-frames 5)
            if [[ "$rc" != 1 ]] || grep -qF "Initializing emulator" "$lp_dir/cli-$k.log"; then
                lp_faults+=("cli $k: rc=$rc (want 1, refused before the machine starts)")
            fi
        done

        if [[ ${#lp_faults[@]} -gt 0 ]]; then
            fail_row " ($(IFS=';'; echo "${lp_faults[*]}"))"
        else
            pass_row " (a loaded .sna (headless/Qt/SDL), a paged 128K .szx and a fast-load .tap replay their recordings exactly; impossible RZX combinations are refused, recording on a Next among them)"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
