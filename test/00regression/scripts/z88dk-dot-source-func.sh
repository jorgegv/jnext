#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable; the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# z88dk-dot-source-func (CAP-SRC, GH #351): a z88dk DOT COMMAND's map, given with
# --source-map and no --load, attaches pending and is judged when NextZXOS runs the
# command, in every frontend. The command is the committed fixture
# test/fixtures/z88dk/v2.4/sccz80-dot: its `PROG` is put on the card with the product's own
# --sdcard-file-add, NextZXOS is booted to its prompt (the keys are boot-nextzxos-dotls's)
# and `.prog` is typed. NextZXOS copies the file into DivMMC RAM bank 2 and runs it
# there with E3 = $82 (fixtures README, G24; the oracle of the counts is that run: the
# dot's `dot.c:8` code at $2D4F runs 4 times, `dot.c:14` at $2D62 once, and $2D62 ran
# once from ROM during the boot, before the command existed).
#   1. the right map: exit 0; the attach line says `identity check pending: the dot
#      command loads when it runs`; `Verified ... the dot command in DivMMC RAM bank 2
#      matches its binary identity` once; the script (raw addresses and the E3 value,
#      which is what a `file:line` rule would resolve to) counts $2D4F 4 times and $2D62
#      once in the bank and once elsewhere (the boot's ROM);
#   2. a map whose section binary has one wrong byte inside dot.c:14: exit 0, no
#      `Verified`, the 'not the one ... stays pending' line once, nothing dropped (the
#      command that ran is, to the map, another dot command);
#   3. a NEX map and no --load: exit 1 and `--source-map requires --load FILE`.
# The script names dot.c:8 and dot.c:14 (#346's `on execute FILE:LINE`): the rules resolve at start-up
# because the map is attached before the scripts load; the ROM's $2D62 is still counted by address.
# Matched on the file name: under wine jnext reports the path in Windows form.
if want z88dk-dot-source-func; then
    begin_func z88dk-dot-source-func
    fx="$PROJECT_DIR/test/fixtures/z88dk/v2.4/sccz80-dot"
    nex="$PROJECT_DIR/test/fixtures/z88dk/v2.4/sccz80-debug"
    BASE="$RUN_DIR/private/z88dk-dot-source"
    W="$BASE/work"
    mkdir -p "$W/good" "$W/bad" "$W/nex"
    cp "$fx/PROG" "$fx/prog.map" "$fx/prog_CODE.bin" "$W/good/"
    cp "$fx/PROG" "$fx/prog.map" "$fx/prog_CODE.bin" "$W/bad/"
    cp "$nex/prog.map" "$nex/prog_CODE.bin" "$W/nex/"
    # dot.c:14 is $2D62 = offset $0D62 of the code binary; the byte there is changed.
    printf '\356' | dd of="$W/bad/prog_CODE.bin" bs=1 seek=$((0xD62)) conv=notrunc status=none

    # The script types the keys (the boot-nextzxos-dotls menu keys, then `.prog` + ENTER; --delayed-keypress
    # is headless-only, a script's `press` works in every frontend) and counts the executions.
    {
        cat <<'JDS'
var e3 = 0
var h8 = 0
var h14 = 0
var rom14 = 0
on io_write 0xE3 do set e3 = VALUE end
on execute dot.c:8 do set h8 = h8 + 1 end
on execute dot.c:14 do set h14 = h14 + 1 end
on execute 0x2D62 when (e3 & 0x8F) != 0x82 do set rom14 = rom14 + 1 end
on frame 800 do log "DOTSRC 2D4F=${h8:d} 2D62=${h14:d} 2D62-elsewhere=${rom14:d}" end
JDS
        for k in "400 space" "470 down" "500 enter" "560 ." "575 p" "590 r" "605 o" "620 g" "635 enter"; do
            printf 'on frame %d do press "%s" end\non frame %d do release "%s" end\n' \
                "${k%% *}" "${k#* }" "$(( ${k%% *} + 5 ))" "${k#* }"
        done
    } > "$W/count.jds"

    faults=()

    new_card() {    # new_card <name> — a private clone with PROG at /DOT/PROG; prints the config dir
        local rc=0
        mkdir -p "$BASE/$1/sdcard"
        cp --reflink=auto "$RUN_DIR/sdcard/cspect-next-1gb-fixed.img" \
                          "$BASE/$1/sdcard/cspect-next-1gb-fixed.img"
        JNEXT_CONFIG_DIR="$BASE/$1" timeout --foreground --kill-after=5s 120s \
            "$JNEXT" --sdcard-file-add "$fx/PROG" --sdcard-file-dest /DOT/PROG \
            > "$W/add-$1.log" 2>&1 || rc=$?
        [[ $rc -eq 0 ]] || faults+=("$1: --sdcard-file-add exited $rc (log: $(tail -n 2 "$W/add-$1.log" 2>/dev/null | tr '\n' ' ' || true))")
    }

    FRAMES=810

    # run_dot <frontend> <name> <map-dir> <args...>: the output of one run is in $W/<name>-<frontend>.log
    # and its exit status in $W/<name>-<frontend>.rc.
    run_dot() {
        local fe=$1 name=$2 map=$3; shift 3
        local rc=0 log="$W/$name-$fe.log"
        case $fe in
            hl) JNEXT_CONFIG_DIR="$BASE/$name-$fe" LANG=C timeout --foreground --kill-after=5s 180s \
                    "$JNEXT" --headless --machine next --rtc "$NEXTZXOS_RTC" \
                    --source-map "$map/prog.map" --script "$W/count.jds" \
                    --delayed-automatic-exit-frames $FRAMES "$@" > "$log" 2>&1 || rc=$? ;;
            qt) JNEXT_CONFIG_DIR="$BASE/$name-$fe" QT_QPA_PLATFORM=offscreen LANG=C \
                    timeout --foreground --kill-after=5s 240s \
                    "$JNEXT" --machine next --rtc "$NEXTZXOS_RTC" --silent --speed 400 \
                    --source-map "$map/prog.map" --script "$W/count.jds" \
                    --delayed-automatic-exit-frames $FRAMES "$@" > "$log" 2>&1 || rc=$? ;;
            sdl) if [[ -x "$JNEXT_SDL" ]]; then
                    JNEXT_CONFIG_DIR="$BASE/$name-$fe" env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy \
                        SDL_AUDIODRIVER=dummy LANG=C timeout --foreground --kill-after=5s 240s \
                        "$JNEXT_SDL" --machine next --rtc "$NEXTZXOS_RTC" --silent --speed 400 \
                        --source-map "$map/prog.map" --script "$W/count.jds" \
                        --delayed-automatic-exit-frames $FRAMES "$@" > "$log" 2>&1 || rc=$?
                 else
                    echo "(SDL-only binary not built: $JNEXT_SDL; run 'make sdl-release')" > "$log"; rc=1
                 fi ;;
        esac
        echo "$rc" > "$W/$name-$fe.rc"
    }

    for fe in hl qt sdl; do
        new_card "good-$fe"
        new_card "bad-$fe"
    done
    if [[ ${#faults[@]} -eq 0 ]]; then
        for fe in hl qt sdl; do
            run_dot "$fe" good "$W/good"
            run_dot "$fe" bad "$W/bad"
        done
    fi

    pend="--source-map '[^']*prog\.map': [0-9]+ source lines \(identity check pending: the dot command loads when it runs\)"
    ver="Verified z88dk source map '[^']*prog\.map': the dot command in DivMMC RAM bank 2 matches its binary identity"
    other="The dot command started in DivMMC RAM bank 2 is not the one source map '[^']*prog\.map' describes; it stays pending"
    counts="DOTSRC 2D4F=4 2D62=1 2D62-elsewhere=1"
    good_ok= bad_ok= rcs=
    if [[ ${#faults[@]} -eq 0 ]]; then
        for fe in hl qt sdl; do
            g=$(cat "$W/good-$fe.log") b=$(cat "$W/bad-$fe.log")
            rcs+="$(cat "$W/good-$fe.rc"),$(cat "$W/bad-$fe.rc") "
            if [[ $(grep -Ec -- "$pend" <<<"$g") -eq 1 && $(grep -Ec -- "$ver" <<<"$g") -eq 1 &&
                  $g == *"$counts"* && $(cat "$W/good-$fe.rc") -eq 0 ]]; then good_ok+=y; else good_ok+=n; fi
            if [[ $(grep -Ec -- "$pend" <<<"$b") -eq 1 && $(grep -Ec -- "$other" <<<"$b") -eq 1 &&
                  $b != *"Verified z88dk"* && $b != *"Rejected"* && $b != *"Dropped"* &&
                  $(cat "$W/bad-$fe.rc") -eq 0 ]]; then bad_ok+=y; else bad_ok+=n; fi
        done
    fi

    # 3. a NEX map and no --load (headless: the refusal is the same in every frontend).
    no_load_rc=0
    no_load=$(LANG=C timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
        "${SD_CARD_ARGS[@]}" --silent --source-map "$W/nex/prog.map" \
        --delayed-automatic-exit-frames 5 2>&1) || no_load_rc=$?

    if [[ ${#faults[@]} -gt 0 ]]; then
        fail_row " (${faults[*]})"
    elif [[ $good_ok == yyy && $bad_ok == yyy && $no_load_rc -eq 1 &&
            "$no_load" == *"--source-map requires --load FILE"* ]]; then
        pass_row " (--source-map of a dot command's map with no --load attaches pending and is verified at the command's entry in DivMMC bank 2, in headless, Qt and SDL; another dot command's bytes keep it pending; a NEX map without --load exits 1)"
    else
        fail_row " (good=$good_ok bad=$bad_ok rc(good,bad)=$rcs no-load rc=$no_load_rc; sdl good: $(tail -n 1 "$W/good-sdl.log" 2>/dev/null || true))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
