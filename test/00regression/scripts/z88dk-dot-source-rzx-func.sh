#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable; the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# z88dk-dot-source-rzx-func (CAP-SRC, GH #351): --source-map with an RZX replay, in both
# spellings (--rzx-play R and --load R.rzx) and in every frontend, is the no-program case:
#   1. a z88dk dot command's map attaches at start-up, pending (an RZX of a NextZXOS session
#      can run a dot command), exit 0;
#   2. any other map is refused at start-up, loudly, naming the RZX replay, exit 1
#      (before this, --load R.rzx --source-map M exited 0 and attached nothing, silently).
if want z88dk-dot-source-rzx-func; then
    begin_func z88dk-dot-source-rzx-func
    W="$TMP_DIR/zdr"
    mkdir -p "$W/dot" "$W/nex"
    cp "$PROJECT_DIR/test/fixtures/z88dk/v2.4/sccz80-dot/prog.map" "$PROJECT_DIR/test/fixtures/z88dk/v2.4/sccz80-dot/prog_CODE.bin" "$W/dot/"
    cp "$PROJECT_DIR/test/fixtures/z88dk/v2.4/sccz80-debug/prog.map" "$PROJECT_DIR/test/fixtures/z88dk/v2.4/sccz80-debug/prog_CODE.bin" "$W/nex/"
    # A 48K recording (a Next cannot record, as in rzx-playback-func).
    timeout --foreground --kill-after=5s 20s "$JNEXT" --headless "${SD_CARD_ARGS[@]}" --machine 48k \
        --rzx-record "$W/rec.rzx" --delayed-automatic-exit 2 &>/dev/null || true

    # run_fe <frontend> <spelling> <map>: output in $out, exit status in $rc
    run_fe() {
        local fe=$1 spell=$2 map=$3 play=()
        case $spell in
            play) play=(--rzx-play "$W/rec.rzx") ;;
            load) play=(--load "$W/rec.rzx") ;;
        esac
        rc=0
        case $fe in
            hl) out=$(LANG=C timeout --foreground --kill-after=5s 60s "$JNEXT" --headless "${SD_CARD_ARGS[@]}" \
                    "${play[@]}" --source-map "$map" --delayed-automatic-exit-frames 60 2>&1) || rc=$? ;;
            qt) out=$(env QT_QPA_PLATFORM=offscreen LANG=C timeout --foreground --kill-after=5s 90s "$JNEXT" \
                    "${SD_CARD_ARGS[@]}" --silent --speed 400 "${play[@]}" --source-map "$map" \
                    --delayed-automatic-exit-frames 60 2>&1) || rc=$? ;;
            sdl) if [[ -x "$JNEXT_SDL" ]]; then
                    out=$(env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy LANG=C \
                        timeout --foreground --kill-after=5s 90s "$JNEXT_SDL" "${SD_CARD_ARGS[@]}" --silent --speed 400 \
                        "${play[@]}" --source-map "$map" --delayed-automatic-exit-frames 60 2>&1) || rc=$?
                 else out="(SDL-only binary not built: $JNEXT_SDL)"; rc=99; fi ;;
        esac
    }

    pend="--source-map '[^']*prog\.map': [0-9]+ source lines \(identity check pending: the dot command loads when it runs\)"
    refused="--source-map with an RZX replay accepts only the map of a z88dk dot command: '[^']*prog\.map' is not one"
    dot_ok= nex_ok= bad=
    if [[ -f "$W/rec.rzx" ]]; then
        for fe in hl qt sdl; do
            for spell in play load; do
                run_fe "$fe" "$spell" "$W/dot/prog.map"
                if [[ $rc -eq 0 && $(grep -Ec -- "$pend" <<<"$out") -eq 1 ]]; then dot_ok+=y; else dot_ok+=n; bad+=" dot/$fe/$spell(rc=$rc)"; fi
                run_fe "$fe" "$spell" "$W/nex/prog.map"
                if [[ $rc -eq 1 && $(grep -Ec -- "$refused" <<<"$out") -eq 1 ]]; then nex_ok+=y; else nex_ok+=n; bad+=" nex/$fe/$spell(rc=$rc)"; fi
            done
        done
    fi
    if [[ $dot_ok == yyyyyy && $nex_ok == yyyyyy ]]; then
        pass_row " (--rzx-play and --load R.rzx with --source-map: a dot command's map attaches pending, any other map is refused naming the RZX replay with exit 1, in headless, Qt and SDL)"
    else
        fail_row " (dot=$dot_ok nex=$nex_ok recording=$([[ -f "$W/rec.rzx" ]] && echo yes || echo no);$bad)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
