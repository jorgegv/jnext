#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# load-exit-status-func: a program that fails to load exits non-zero, in every
# format, so a script can tell "loaded fine" from "failed to load, ran anyway".
# Headless: a truncated .nex, a too-small .sna and a garbage .rzx given to
# --rzx-play must each log the failure and exit non-zero; a valid .nex must
# exit 0 (the positive control that keeps the failures from passing merely
# because the emulator never ran). The SDL-only and Qt frontends enforce the
# same rule in their own code, so each gets one failing and one valid .nex.
if want load-exit-status-func; then
    begin_func load-exit-status-func
    good_nex="$TMP_DIR/les-good.nex"
    trunc_nex="$TMP_DIR/les-truncated.nex"
    small_sna="$TMP_DIR/les-small.sna"
    bad_rzx="$TMP_DIR/les-bad.rzx"
    python3 - "$good_nex" "$trunc_nex" "$small_sna" "$bad_rzx" <<'PY'
import sys, struct
h = bytearray(512)
h[0:4] = b"Next"; h[4:8] = b"V1.2"; h[9] = 1
h[12:14] = struct.pack("<H", 0x8000)  # SP
h[14:16] = struct.pack("<H", 0x8000)  # PC
h[18 + 0] = 1                         # bank 0 present
nex = bytes(h) + b"\x00" * 16384
open(sys.argv[1], "wb").write(nex)            # exact declared size -> loads
open(sys.argv[2], "wb").write(nex[:10000])    # truncated -> NEX load fails
open(sys.argv[3], "wb").write(b"\x00" * 100)  # < 48K SNA minimum -> fails
open(sys.argv[4], "wb").write(b"NOTRZX" * 20) # not an RZX -> parse fails
PY
    # run_rc <out-var-name> <cmd...>: capture combined output and the exit
    # status without tripping set -e.
    run_rc() {
        local __out=$1; shift
        local __o __rc=0
        __o=$("$@" 2>&1) || __rc=$?
        printf -v "$__out" '%s' "$__o"
        return "$__rc"
    }
    hl=(timeout --foreground --kill-after=5s 30s "$JNEXT" --headless
        "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 10)
    rc_nex=0;  run_rc o_nex  "${hl[@]}" --machine next --load "$trunc_nex" || rc_nex=$?
    rc_sna=0;  run_rc o_sna  "${hl[@]}" --machine 48k  --load "$small_sna" || rc_sna=$?
    rc_rzx=0;  run_rc o_rzx  "${hl[@]}" --machine 48k  --rzx-play "$bad_rzx" || rc_rzx=$?
    rc_good=0; run_rc o_good "${hl[@]}" --machine next --load "$good_nex" || rc_good=$?
    e_nex=$(echo "$o_nex" | grep -cF "failed to load" || true)
    e_sna=$(echo "$o_sna" | grep -cF "failed to load" || true)
    e_rzx=$(echo "$o_rzx" | grep -cF "RZX: failed to load" || true)
    e_good=$(echo "$o_good" | grep -cF "failed to load" || true)
    headless_ok=0
    if [[ "$rc_nex" -ne 0 && "$e_nex" -ge 1 && "$rc_sna" -ne 0 && "$e_sna" -ge 1 \
          && "$rc_rzx" -ne 0 && "$e_rzx" -ge 1 && "$rc_good" -eq 0 && "$e_good" -eq 0 ]]; then
        headless_ok=1
    fi

    # SDL-only frontend (dummy drivers: no X server, no audio device).
    sdl_bin="$PROJECT_DIR/build/sdl-release/jnext"
    sdl_ok=0; sdl_note=""
    if [[ ! -x "$sdl_bin" ]]; then
        sdl_note="SDL-only binary not built: $sdl_bin; run 'make sdl-release'"
    else
        sdl=(env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
             timeout --foreground --kill-after=5s 60s "$sdl_bin" --machine next --silent
             "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 10)
        rc_sdl_bad=0;  run_rc o_sdl_bad  "${sdl[@]}" --load "$trunc_nex" || rc_sdl_bad=$?
        rc_sdl_good=0; run_rc o_sdl_good "${sdl[@]}" --load "$good_nex"  || rc_sdl_good=$?
        e_sdl_bad=$(echo "$o_sdl_bad" | grep -cF "failed to load" || true)
        if [[ "$rc_sdl_bad" -ne 0 && "$e_sdl_bad" -ge 1 && "$rc_sdl_good" -eq 0 ]]; then
            sdl_ok=1
        fi
        sdl_note="sdl bad rc=$rc_sdl_bad err=$e_sdl_bad, good rc=$rc_sdl_good"
    fi

    # Qt frontend ($JNEXT is the Qt build), offscreen.
    qt=(env QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 120s
        "$JNEXT" --machine next --silent "${SD_CARD_ARGS[@]}"
        --delayed-automatic-exit-frames 10)
    rc_qt_bad=0;  run_rc o_qt_bad  "${qt[@]}" --load "$trunc_nex" || rc_qt_bad=$?
    rc_qt_good=0; run_rc o_qt_good "${qt[@]}" --load "$good_nex"  || rc_qt_good=$?
    e_qt_bad=$(echo "$o_qt_bad" | grep -cF "failed to load" || true)
    qt_ok=0
    if [[ "$rc_qt_bad" -ne 0 && "$e_qt_bad" -ge 1 && "$rc_qt_good" -eq 0 ]]; then
        qt_ok=1
    fi

    if [[ "$headless_ok" -eq 1 && "$sdl_ok" -eq 1 && "$qt_ok" -eq 1 ]]; then
        pass_row " (failed .nex/.sna/--rzx-play exit!=0, valid .nex exits 0; SDL + Qt same)"
    else
        fail_row " (headless nex rc=$rc_nex err=$e_nex, sna rc=$rc_sna err=$e_sna, rzx rc=$rc_rzx err=$e_rzx, good rc=$rc_good err=$e_good; $sdl_note; qt bad rc=$rc_qt_bad err=$e_qt_bad, good rc=$rc_qt_good)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
