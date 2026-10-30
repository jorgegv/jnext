#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable; the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# z88dk-source-map-func (CAP-SRC, GH #344): a z88dk -debug map attaches as a
# program's symbols and source lines, in every frontend, from beside the program
# (<stem>.map) and from --source-map, whose rules are checked here:
#   1. prog.map and prog_CODE.bin beside prog.nex attach on --load: 19 symbols
#      and 46 source lines, identity verified (headless, Qt, SDL);
#   2. --source-map maps/prog.map with --load game.nex attaches the map once, as
#      the user's, identity verified (headless, Qt, SDL; exactly one line);
#   3. --source-map without --load is refused at once, with a message;
#   4. --source-map of a map whose section binary is another build's (one byte of
#      main.c:19's code differs from the program in memory) is a mismatch: the run
#      exits 1 and says why (headless, Qt, SDL).
# Matched on the file name: under wine jnext reports the path in Windows form.
# The fixtures are the committed z88dk ones (test/fixtures/z88dk).
if want z88dk-source-map-func; then
    begin_func z88dk-source-map-func
    fx="$PROJECT_DIR/test/fixtures/z88dk"
    side="$TMP_DIR/z88-side"
    cli="$TMP_DIR/z88-cli"
    maps="$cli/maps"
    wrong="$TMP_DIR/z88-wrong"
    mkdir -p "$side" "$maps" "$wrong"
    cp "$fx/v2.4/sccz80-debug/prog.nex" "$fx/v2.4/sccz80-debug/prog.map" \
       "$fx/v2.4/sccz80-debug/prog_CODE.bin" "$side/"
    cp "$fx/v2.4/sccz80-debug/prog.nex" "$cli/game.nex"
    cp "$fx/v2.4/sccz80-debug/prog.map" "$fx/v2.4/sccz80-debug/prog_CODE.bin" "$maps/"
    cp "$fx/v2.4/sccz80-debug/prog.nex" "$wrong/game.nex"
    cp "$fx/v2.4/sccz80-debug/prog.map" "$fx/v2.4/sccz80-debug/prog_CODE.bin" "$wrong/"
    # main.c:19 starts at $8CB5 = offset $0CB5 of the code binary; the byte there is $11.
    printf '\356' | dd of="$wrong/prog_CODE.bin" bs=1 seek=$((0x0CB5)) conv=notrunc status=none

    # run_all <variable-prefix> <args...>: the same command line in the three
    # frontends. Sets <prefix>_hl / _qt / _sdl (the output) and <prefix>_rc as
    # "h:q:s" exit codes.
    run_all() {
        local prefix=$1; shift
        local rh=0 rq=0 rs=0 oh oq os
        oh=$(LANG=C timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --silent "$@" --delayed-automatic-exit-frames 10 2>&1) || rh=$?
        oq=$(env QT_QPA_PLATFORM=offscreen LANG=C timeout --foreground --kill-after=5s 60s \
            "$JNEXT" --machine next "${SD_CARD_ARGS[@]}" --silent "$@" \
            --delayed-automatic-exit-frames 10 2>&1) || rq=$?
        os="(SDL-only binary not built: $JNEXT_SDL; run 'make sdl-release')"
        rs=1
        if [[ -x "$JNEXT_SDL" ]]; then
            rs=0
            os=$(env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy LANG=C \
                timeout --foreground --kill-after=5s 60s "$JNEXT_SDL" --machine next --silent \
                "${SD_CARD_ARGS[@]}" "$@" --delayed-automatic-exit-frames 10 2>&1) || rs=$?
        fi
        printf -v "${prefix}_hl" '%s' "$oh"
        printf -v "${prefix}_qt" '%s' "$oq"
        printf -v "${prefix}_sdl" '%s' "$os"
        printf -v "${prefix}_rc" '%s' "$rh:$rq:$rs"
    }

    # 1. the sidecar case
    run_all s --load "$side/prog.nex"
    sym="Loaded 19 z88dk symbols from '[^']*prog\.map'"
    src="Loaded 46 z88dk source lines from '[^']*prog\.map' \(program identity verified\)"
    side_ok=
    for v in "$s_hl" "$s_qt" "$s_sdl"; do
        if grep -Eq "$sym" <<<"$v" && grep -Eq "$src" <<<"$v"; then side_ok+=y; else side_ok+=n; fi
    done

    # 2. the --source-map case: the log line exactly once per frontend
    run_all c --load "$cli/game.nex" --source-map "$maps/prog.map"
    cli_line="--source-map '[^']*prog\.map': 46 source lines \(program identity verified\)"
    cli_ok=
    for v in "$c_hl" "$c_qt" "$c_sdl"; do
        if [[ $(grep -Ec -- "$cli_line" <<<"$v") -eq 1 ]]; then cli_ok+=y; else cli_ok+=n; fi
    done

    # 3. --source-map without --load
    rc3=0
    o3=$(LANG=C timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --silent --source-map "$maps/prog.map" \
        --delayed-automatic-exit-frames 10 2>&1) || rc3=$?

    # 4. a map built for another program
    run_all w --load "$wrong/game.nex" --source-map "$wrong/prog.map"
    wrong_line="--source-map '[^']*prog\.map': the program in memory does not match the source map's binary identity"
    wrong_ok=
    for v in "$w_hl" "$w_qt" "$w_sdl"; do
        if grep -Eq -- "$wrong_line" <<<"$v"; then wrong_ok+=y; else wrong_ok+=n; fi
    done

    if [[ $side_ok == yyy && $s_rc == 0:0:0 && $cli_ok == yyy && $c_rc == 0:0:0 &&
          $rc3 -eq 1 && "$o3" == *"--source-map requires --load FILE"* &&
          $wrong_ok == yyy && $w_rc == 1:1:1 ]]; then
        pass_row " (prog.map beside a .nex and --source-map attach in headless, Qt and SDL, identity verified; no --load is refused; another build's map exits 1 in each)"
    else
        fail_row " (sidecar=$side_ok rc=$s_rc; cli=$cli_ok rc=$c_rc; no-load rc=$rc3; mismatch=$wrong_ok rc=$w_rc; sdl: $(tail -1 <<<"$s_sdl"))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
