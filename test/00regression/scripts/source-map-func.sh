#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# source-map-func (CAP-SRC): a <stem>.sld (sjasmplus SLD v1) beside a loaded
# program attaches as its source map, in every frontend; one whose binary
# identity does not match the loaded program is rejected, and says so.
# Each frontend also warns with the reason, from its own load path (C1). Every
# run exits 0: a sidecar never turns a load into a failure. What the
# map is FOR — the Source tab, statement steps — has no command-line or
# protocol surface; debugger_source_panel_test and source_debug_test drive it.
# Discriminative: a binary without the feature attaches nothing and rejects
# nothing.
if want source-map-func; then
    begin_func source-map-func
    nex="$TMP_DIR/source-sidecar.nex"
    sld="$TMP_DIR/source-sidecar.sld"
    bad_nex="$TMP_DIR/source-wrong.nex"
    bad_sld="$TMP_DIR/source-wrong.sld"
    cp "$PROJECT_DIR/test/00regression/nex/menu.nex" "$nex"
    cp "$PROJECT_DIR/test/00regression/nex/menu.nex" "$bad_nex"
    dev='|-1|-1|Z|pages.size:8192,pages.count:224,slots.count:8,slots.adr:0,8192,16384,24576,32768,40960,49152,57344'
    printf '%s\n' '|SLD.data.version|1' "main.bas|1||0$dev" 'main.bas|1||0|4|32768|T|' > "$sld"
    # An identity no program has: the SHA-256 of 01 02 03 04 at $8000.
    printf '%s\n' '|SLD.data.version|1' '||program.name:other' \
        '||program.sha256:9f64a747e1b97f131fabb6b447296c9b6f0201e79fb3c5356e6c77e89b6a806a' \
        '||program.org:32768' '||program.size:4' \
        "main.bas|1||0$dev" 'main.bas|1||0|4|32768|T|' > "$bad_sld"
    # Matched on the file name: under wine jnext reports the path in Windows form.
    want_line="Loaded 1 SLD source traces from '[^']*source-sidecar\.sld'"

    # Every run must exit cleanly: a sidecar never makes a load fail.
    rcs=()
    run_hl() {
        local rc=0 o
        o=$(LANG=C timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --silent --load "$1" --delayed-automatic-exit-frames 10 2>&1) || rc=$?
        rcs+=("headless:$rc")
        printf '%s' "$o"
    }
    o_hl=$(run_hl "$nex"; echo; echo "RC ${rcs[*]}")
    o_bad=$(run_hl "$bad_nex"; echo; echo "RC ${rcs[*]}")
    rc_qt=0
    run_qt() {
        env QT_QPA_PLATFORM=offscreen LANG=C timeout --foreground --kill-after=5s 60s \
            "$JNEXT" --machine next "${SD_CARD_ARGS[@]}" --silent --load "$1" \
            --delayed-automatic-exit-frames 10 2>&1
    }
    o_qt=$(run_qt "$nex") || rc_qt=$?
    o_bad_qt=$(run_qt "$bad_nex") || rc_qt=$?
    sdl_bin="$JNEXT_SDL"
    o_sdl="(SDL-only binary not built: $sdl_bin; run 'make sdl-release')"
    o_bad_sdl=""
    rc_sdl=1
    if [[ -x "$sdl_bin" ]]; then
        rc_sdl=0
        run_sdl() {
            env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy LANG=C \
                timeout --foreground --kill-after=5s 60s "$sdl_bin" --machine next --silent \
                "${SD_CARD_ARGS[@]}" --load "$1" --delayed-automatic-exit-frames 10 2>&1
        }
        o_sdl=$(run_sdl "$nex") || rc_sdl=$?
        o_bad_sdl=$(run_sdl "$bad_nex") || rc_sdl=$?
    fi

    hl=0; qt=0; sdl=0; rej=0; clean=0; warned=
    grep -Eq "$want_line" <<<"$o_hl" && hl=1
    grep -Eq "$want_line" <<<"$o_qt" && qt=1
    grep -Eq "$want_line" <<<"$o_sdl" && sdl=1
    if grep -Eq "Rejected SLD source map '[^']*source-wrong\.sld'" <<<"$o_bad" &&
       ! grep -Eq "SLD source traces from '[^']*source-wrong\.sld'" <<<"$o_bad"; then
        rej=1
    fi
    # The frontend's own warning: headless says `--load:`, Qt and SDL `load:`
    # (Qt's comes from its debugger, on the debugger channel).
    warn_line="\[(platform|debugger)\] \[warn[a-z]*\] (--)?load: source map '[^']*source-wrong\.sld' rejected"
    grep -Eq "$warn_line" <<<"$o_bad" && warned+=h
    grep -Eq "$warn_line" <<<"$o_bad_qt" && warned+=q
    grep -Eq "$warn_line" <<<"$o_bad_sdl" && warned+=s
    if grep -Fq "RC headless:0" <<<"$o_hl" && grep -Fq "RC headless:0" <<<"$o_bad" &&
       [[ $rc_qt -eq 0 && $rc_sdl -eq 0 ]]; then
        clean=1
    fi
    if [[ $hl -eq 1 && $qt -eq 1 && $sdl -eq 1 && $rej -eq 1 && $warned == hqs && $clean -eq 1 ]]; then
        pass_row " (SLD attached on load in headless, Qt and SDL; a wrong identity is rejected, and each frontend warns why; every run exits 0)"
    else
        fail_row " (headless=$hl qt=$qt sdl=$sdl rejected=$rej warned=${warned:-none} clean=$clean qt-rc=$rc_qt sdl-rc=$rc_sdl; sdl: $(tail -1 <<<"$o_sdl"))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
