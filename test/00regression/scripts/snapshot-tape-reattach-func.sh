#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #402 - a `.jns` load puts the recorded tape back where it was.
# Design: doc/design/NEXT-SNAPSHOT-FORMAT.md section 10.2 P4.
#
# The user scenario, ACROSS PROCESSES: run 1 loads a tape in real time and saves
# a snapshot part-way through the load; run 3 is a NEW process that loads the
# snapshot and must carry on loading. Oracle: the uninterrupted run (run 2) of
# the same machine, pixel for pixel, with the same one-frame offset as
# snapshot-jns-roundtrip-func (a save advances to the next frame boundary, so
# the snapshot of frame S holds S+1 and restore + K frames == continuous S+1+K).
# Before this change run 3 showed a plain border and an empty screen for ever:
# the ROM waited for edges from a deck that was empty.
#
# Legs:
#   1  real time, a plain .tap          0 px against the uninterrupted run
#   2  real time, a tape inside a .zip  0 px; the scratch copy of run 1 died with
#                                       its process, so only the archive route
#                                       can find the tape; log says "reattached"
#   3  the session is FAST (no --tape-realtime) while the snapshot's tape was
#      real time: it must finish that block in real time and hand over, with
#      the load completing (the loaded picture differs from the control's)
#   4  control: the tape moved away -> the picture differs and the log warns
if want snapshot-tape-reattach-func; then
    begin_func snapshot-tape-reattach-func
    if ! $HAS_COMPARE; then
        skip_row " (no ImageMagick - the whole point of this row is a pixel comparison)"
    else
    ta_dir="$TMP_DIR/ta"
    mkdir -p "$ta_dir"
    ta_bif="$ta_dir/bifrost.tap"
    cp "$PROJECT_DIR/test/00regression/tap/bifrost.tap" "$ta_bif"
    ta_zip="$ta_dir/beeper.tap.zip"
    cp "$PROJECT_DIR/test/fixtures/zip/beeper.tap.zip" "$ta_zip"
    ta_faults=()

    # ta_run <log> <jnext args...>: a headless 48K run; prints nothing, returns the status.
    ta_run() {
        local log=$1; shift
        timeout --foreground --kill-after=5s 180s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 "$@" >"$log" 2>&1
    }
    # ta_leg <tag> <tape> <S> <K> <session args...>: saves at S, then compares
    # restore + K with the continuous run at S + 1 + K; prints the differing pixels.
    ta_leg() {
        local tag=$1 tape=$2 s=$3 k=$4; shift 4
        local jns="$ta_dir/$tag.jns"
        ta_run "$ta_dir/$tag-1.log" --tape-realtime --load "$tape" \
            --delayed-snapshot "$jns" --delayed-snapshot-frames "$s" \
            --delayed-automatic-exit-frames "$((s + 2))" || { echo "-1"; return; }
        [[ -s "$jns" ]] || { echo "-2"; return; }
        ta_run "$ta_dir/$tag-2.log" --tape-realtime --load "$tape" \
            --delayed-screenshot "$ta_dir/$tag-cont.png" --delayed-screenshot-frames "$((s + 1 + k))" \
            --delayed-automatic-exit-frames "$((s + 2 + k))" || { echo "-3"; return; }
        # The tape is gone from the saving run's scratch directory (a .zip) or
        # still where it was (a plain file): run 3 must cope with both.
        ta_run "$ta_dir/$tag-3.log" "$@" --load "$jns" \
            --delayed-screenshot "$ta_dir/$tag-rest.png" --delayed-screenshot-frames "$k" \
            --delayed-automatic-exit-frames "$((k + 1))" || { echo "-4"; return; }
        png_diff "$ta_dir/$tag-rest.png" "$ta_dir/$tag-cont.png"
    }

    # Leg 1: bifrost.tap, real time; frame 600 is mid-load, 3000 more frames show the stripes.
    d1=$(ta_leg bif "$ta_bif" 600 3000 --tape-realtime)
    [[ "$d1" == 0 ]] || ta_faults+=("leg 1 (real time): $d1 px differ (<0 = a run failed)")
    grep -qF "reattached" "$ta_dir/bif-3.log" || ta_faults+=("leg 1: no 'reattached' line in the log")

    # Leg 2: the same through a .zip; run 3 runs in a NEW process, whose scratch directory is not run 1's.
    d2=$(ta_leg zip "$ta_zip" 300 400 --tape-realtime)
    [[ "$d2" == 0 ]] || ta_faults+=("leg 2 (zip): $d2 px differ (<0 = a run failed)")
    grep -qF "reattached" "$ta_dir/zip-3.log" || ta_faults+=("leg 2: no 'reattached' line in the log")
    ! grep -qF "is not available" "$ta_dir/zip-3.log" || ta_faults+=("leg 2: the tape was reported unavailable")

    # Control (leg 4): the tape moved away, the same load as leg 1.
    mv "$ta_bif" "$ta_dir/moved.tap"
    ta_run "$ta_dir/ctl-3.log" --tape-realtime --load "$ta_dir/bif.jns" \
        --delayed-screenshot "$ta_dir/ctl.png" --delayed-screenshot-frames 3000 \
        --delayed-automatic-exit-frames 3001 || ta_faults+=("control run failed")
    dc=$(png_diff "$ta_dir/ctl.png" "$ta_dir/bif-cont.png")
    [[ "$dc" -gt 0 ]] || ta_faults+=("control: the picture without the tape equals the loaded one (png_diff=$dc): the legs prove nothing")
    grep -qF "is not available" "$ta_dir/ctl-3.log" || ta_faults+=("control: no 'not available' warning in the log")
    mv "$ta_dir/moved.tap" "$ta_bif"

    # Leg 3: a FAST session loading the real-time snapshot of leg 1. The block in
    # progress finishes in real time, the deck hands over to fast loading at the
    # next gap, and the load completes: the demo's screen, not the loading
    # stripes' border, so far from the control's, and the log names the handover.
    ta_run "$ta_dir/mis-3.log" --load "$ta_dir/bif.jns" \
        --delayed-screenshot "$ta_dir/mis.png" --delayed-screenshot-frames 3000 \
        --delayed-automatic-exit-frames 3001 || ta_faults+=("leg 3: the fast-session run failed")
    grep -qF "differ from the snapshot's" "$ta_dir/mis-3.log" || ta_faults+=("leg 3: the log does not name the mode conversion")
    grep -qF "handed over to fast loading" "$ta_dir/mis-3.log" || ta_faults+=("leg 3: the deck never handed over to fast loading")
    grep -qF "tape" <(grep -E "WARN|warn" "$ta_dir/mis-3.log" || true) && ta_faults+=("leg 3: a tape warning was raised")
    dm=$(png_diff "$ta_dir/mis.png" "$ta_dir/ctl.png")
    [[ "$dm" -gt 0 ]] || ta_faults+=("leg 3: the fast-session picture equals the no-tape control (png_diff=$dm)")

    if [[ ${#ta_faults[@]} -eq 0 ]]; then
        pass_row " (real time restore+3000 == continuous, 0 px; zip restore+400 == continuous, 0 px; fast session hands over and loads; control differs by $dc px and warns)"
    else
        fail_row " ($(IFS=';'; echo "${ta_faults[*]}"))"
    fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
