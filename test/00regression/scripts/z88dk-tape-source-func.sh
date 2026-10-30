#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable; the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# z88dk-tape-source-func (CAP-SRC, GH #344 stage 3): a z88dk -debug map attaches to a
# TAPE program at once and its binary identity is judged where the CPU first runs the
# program's code, in every frontend. The program is the committed `+zx` fixture
# (test/fixtures/z88dk/v2.4/sccz80-tape), loaded by the machine's own tape loader
# (--load x.tap on a 48K machine types LOAD "" and fast-loads it), so the section
# binary is in memory only some 100 frames after the load call returns:
#   1. prog.map and prog_CODE.bin beside prog.tap: the sidecar attaches pending, then
#      is verified once the program runs, exit 0 (headless, Qt, SDL);
#   2. --source-map maps/prog.map with --load game.tap: the same through the CLI;
#   3. --source-map of a map whose section binary has one wrong byte (inside
#      main.c:19, which is $8CC4 here): dropped when the program runs, "Rejected ...
#      from tape", and the run exits 1 in each frontend.
# Matched on the file name: under wine jnext reports the path in Windows form.
if want z88dk-tape-source-func; then
    begin_func z88dk-tape-source-func
    fx="$PROJECT_DIR/test/fixtures/z88dk/v2.4/sccz80-tape"
    side="$TMP_DIR/zt-side"
    cli="$TMP_DIR/zt-cli"
    maps="$cli/maps"
    wrong="$TMP_DIR/zt-wrong"
    mkdir -p "$side" "$maps" "$wrong"
    cp "$fx/prog.tap" "$fx/prog.map" "$fx/prog_CODE.bin" "$side/"
    cp "$fx/prog.tap" "$cli/game.tap"
    cp "$fx/prog.map" "$fx/prog_CODE.bin" "$maps/"
    cp "$fx/prog.tap" "$wrong/game.tap"
    cp "$fx/prog.map" "$fx/prog_CODE.bin" "$wrong/"
    # main.c:19 starts at $8CC4 = offset $0CC4 of the code binary; the byte there is changed.
    printf '\356' | dd of="$wrong/prog_CODE.bin" bs=1 seek=$((0xCC4)) conv=notrunc status=none

    # The program is verified ~100 frames in; 400 leaves margin, and --speed 400 keeps
    # the two windowed frontends from taking eight seconds each in real time.
    FRAMES=400

    # run_all <variable-prefix> <args...>: the same command line in the three frontends.
    # Sets <prefix>_hl / _qt / _sdl (the output) and <prefix>_rc as "h:q:s" exit codes.
    run_all() {
        local prefix=$1; shift
        local rh=0 rq=0 rs=0 oh oq os
        oh=$(LANG=C timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --silent "$@" --delayed-automatic-exit-frames $FRAMES 2>&1) || rh=$?
        oq=$(env QT_QPA_PLATFORM=offscreen LANG=C timeout --foreground --kill-after=5s 90s \
            "$JNEXT" --machine 48k "${SD_CARD_ARGS[@]}" --silent --speed 400 "$@" \
            --delayed-automatic-exit-frames $FRAMES 2>&1) || rq=$?
        os="(SDL-only binary not built: $JNEXT_SDL; run 'make sdl-release')"
        rs=1
        if [[ -x "$JNEXT_SDL" ]]; then
            rs=0
            os=$(env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy LANG=C \
                timeout --foreground --kill-after=5s 90s "$JNEXT_SDL" --machine 48k --silent --speed 400 \
                "${SD_CARD_ARGS[@]}" "$@" --delayed-automatic-exit-frames $FRAMES 2>&1) || rs=$?
        fi
        printf -v "${prefix}_hl" '%s' "$oh"
        printf -v "${prefix}_qt" '%s' "$oq"
        printf -v "${prefix}_sdl" '%s' "$os"
        printf -v "${prefix}_rc" '%s' "$rh:$rq:$rs"
    }

    # order_ok <output> <first-regex> <second-regex>: both lines are there, the first before the second.
    order_ok() {
        local a b
        a=$(grep -nE -- "$2" <<<"$1" | head -n1 | cut -d: -f1) || true
        b=$(grep -nE -- "$3" <<<"$1" | head -n1 | cut -d: -f1) || true
        [[ -n "$a" && -n "$b" && "$a" -lt "$b" ]]
    }

    # 1. the sidecar case
    run_all s --load "$side/prog.tap"
    pend="Loaded 46 z88dk source lines from '[^']*prog\.map' \(identity check pending: the program loads from tape\)"
    ver="Verified z88dk source map '[^']*prog\.map': the program loaded from tape matches its binary identity"
    side_ok=
    for v in "$s_hl" "$s_qt" "$s_sdl"; do
        if order_ok "$v" "$pend" "$ver"; then side_ok+=y; else side_ok+=n; fi
    done

    # 2. the --source-map case
    run_all c --load "$cli/game.tap" --source-map "$maps/prog.map"
    cpend="--source-map '[^']*prog\.map': 46 source lines \(identity check pending: the program loads from tape\)"
    cver="Verified z88dk source map '[^']*prog\.map': the program loaded from tape matches its binary identity"
    cli_ok=
    for v in "$c_hl" "$c_qt" "$c_sdl"; do
        if order_ok "$v" "$cpend" "$cver"; then cli_ok+=y; else cli_ok+=n; fi
    done

    # 3. a map built for another program
    run_all w --load "$wrong/game.tap" --source-map "$wrong/prog.map"
    rej="Rejected z88dk source map '[^']*prog\.map': the program loaded from tape does not match its binary identity"
    wrong_ok=
    for v in "$w_hl" "$w_qt" "$w_sdl"; do
        if grep -Eq -- "$rej" <<<"$v" && ! grep -Eq -- "Verified z88dk" <<<"$v"; then wrong_ok+=y; else wrong_ok+=n; fi
    done

    if [[ $side_ok == yyy && $s_rc == 0:0:0 && $cli_ok == yyy && $c_rc == 0:0:0 &&
          $wrong_ok == yyy && $w_rc == 1:1:1 ]]; then
        pass_row " (prog.map beside a tape and --source-map attach pending and are verified when the program runs, in headless, Qt and SDL; one wrong byte is rejected and exits 1 in each)"
    else
        fail_row " (sidecar=$side_ok rc=$s_rc; cli=$cli_ok rc=$c_rc; mismatch=$wrong_ok rc=$w_rc; sdl: $(tail -1 <<<"$s_sdl"))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
