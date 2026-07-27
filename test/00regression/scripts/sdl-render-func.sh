#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# SDL frontend render + capture test (GitHub issue #134).
#
# WHY THIS ROW EXISTS. Every screenshot row runs --headless (HeadlessApp) and
# the seven windowed rows run the Qt build, so before #122 NOTHING in the suite
# instantiated SdlApp: src/main.cpp:944-951 builds it only when ENABLE_QT_UI is
# OFF, and the suite's binary is the Qt one (test-functions.inc:192). #122 then
# added exactly one SDL row, and it asserts key delivery — a run where the
# frontend rendered nothing at all makes it SKIP, not FAIL. So the SDL tick loop
# has never been asserted by anything.
#
# That loop is NOT shared code. QtApp delegates its whole tick order to
# frame_sequencer::Sequencer (src/gui/qt_app.cpp:410-416), which
# frame_sequencer_test drives over thousands of simulated ticks; SdlApp instead
# hand-rolls its own ~180-line loop (src/platform/sdl_app.cpp:250-427) and
# includes no sequencer at all. Everything in it — the pending-load countdown,
# the audio-paced frame count, the render_policy composite gate, the layer-mask
# arming, the capture — is SdlApp's own copy.
#
# THE BINARY IS NOT $JNEXT. It is build/sdl-release/jnext, which `make
# regression` builds and the driver preflight demands (regression.sh:92-95) —
# the same contract rewind-func has for build/test/rewind_test.
#
# THE MEASUREMENT. Two runs, each reproducing an EXISTING screenshot row's
# machine, frame and flags, and diffed against that row's committed reference.
# No new reference image is introduced: the claim is precisely "the SDL frontend
# composites and captures the same frame the headless frontend is already held
# to", so borrowing the references is the whole point.
#
#   boot   48k, BOOT, frame 150            -> img/boot-48k-reference.png
#          (SDL_Init, SdlDisplay window/renderer/texture, ROM extraction,
#           loop, composite gate, capture)
#   nex    next, --load beast.nex, frame 100, --delayed-screenshot-layers
#          sprites                         -> img/layers-beast-sprites-reference.png
#          (adds the pending-load countdown and the layer-mask arm/disarm)
#
# Each run also carries a free second observation: SdlApp derives its own
# SDL_Delay period from the emulated refresh (sdl_app.cpp:414-425) and logs it.
# 48k is a 50 Hz machine (20 ms); beast.nex switches to 60 Hz via NR 0x05 bit 2
# (17 ms). frame-pacing-func makes the same measurement for the Qt frontend, and
# says in its own header that it is the only row covering that path — it is, for
# Qt: SdlApp's pacer is a different mechanism (a whole-ms SDL_Delay, not the
# fractional deadline scheduler) and shares no code with it.
#
# SDL_VIDEODRIVER=dummy, not Xvfb. SdlDisplay's real code path runs either way
# (window, renderer and texture are all created, and init() fails if any does
# not), but dummy needs no X server, so the pair costs ~7 s. The real-X11 path
# is what sdl-keypress-func exercises; this row is about what gets composited,
# not about the window server.
if want sdl-render-func; then
    begin_func sdl-render-func

    sdl_bin="$PROJECT_DIR/build/sdl-release/jnext"
    boot_png="$TMP_DIR/sdl-render-boot.png"
    nex_png="$TMP_DIR/sdl-render-nex.png"
    boot_log="$TMP_DIR/sdl-render-boot.log"
    nex_log="$TMP_DIR/sdl-render-nex.log"
    boot_ref="$IMG_DIR/boot-48k-reference.png"
    nex_ref="$IMG_DIR/layers-beast-sprites-reference.png"
    rm -f "$boot_png" "$nex_png" "$boot_log" "$nex_log"

    # One run of the SDL-only binary. $1 = PNG, $2 = log, rest = jnext args.
    sdl_render_run() {
        local out="$1" log="$2"; shift 2
        env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        timeout --foreground --kill-after=5s 90s \
        "$sdl_bin" --silent "${SD_CARD_ARGS[@]}" \
            --delayed-screenshot "$out" "$@" >"$log" 2>&1 || true
    }

    if [[ ! -x "$sdl_bin" ]]; then
        # A build artifact the Makefile can guarantee, so this is loud (the
        # rewind-func rule). The driver's preflight normally catches it first.
        fail_row " (SDL-only binary not built: $sdl_bin; run 'make sdl-release')"
    elif ! command -v magick &>/dev/null && ! command -v convert &>/dev/null; then
        skip_row " (no ImageMagick — cannot diff the captures against the references)"
    else
        sdl_render_run "$boot_png" "$boot_log" --machine 48k \
            --delayed-screenshot-frames 150 --delayed-automatic-exit-frames 200
        sdl_render_run "$nex_png" "$nex_log" --machine next \
            --load "$SCRIPT_DIR/nex/beast.nex" \
            --delayed-screenshot-frames 100 --delayed-screenshot-layers sprites \
            --delayed-automatic-exit-frames 150

        # An SDL that cannot bring up a video device at all is a broken HOST,
        # not a broken emulator, and the two must not be confused. SdlApp::init
        # logs SDL's own error text and returns false in that case
        # (sdl_app.cpp:9-14, sdl_display.cpp:16-39), so the signature is
        # unambiguous — and it is checked only when there is no PNG, so it can
        # never mask a genuine capture failure.
        sdl_init_re="SDL_Init:|SDL_CreateWindow:|SDL_CreateRenderer:|SDL_CreateTexture:"
        sdl_init_err=$(( $(grep -cE "$sdl_init_re" "$boot_log" 2>/dev/null || true) +
                         $(grep -cE "$sdl_init_re" "$nex_log" 2>/dev/null || true) ))

        boot_diff=999999; nex_diff=999999
        [[ -s "$boot_png" ]] && boot_diff=$(png_diff "$boot_png" "$boot_ref")
        [[ -s "$nex_png" ]] && nex_diff=$(png_diff "$nex_png" "$nex_ref")

        # Pacing: the 50 Hz machine must derive 20 ms and NOT 17; the 60 Hz demo
        # must derive 17 (it logs 20 first, before the demo switches the
        # refresh, so only the presence of the 17 ms line is asserted there).
        boot_20=$(grep -cF -- "-> 20 ms/frame" "$boot_log" 2>/dev/null || true)
        boot_17=$(grep -cF -- "-> 17 ms/frame" "$boot_log" 2>/dev/null || true)
        nex_17=$(grep -cF -- "-> 17 ms/frame" "$nex_log" 2>/dev/null || true)

        if [[ ! -s "$boot_png" && ! -s "$nex_png" && "$sdl_init_err" -gt 0 ]]; then
            skip_row " (SDL could not open a video device on this host; no capture was possible)"
        elif [[ "$boot_diff" -le "$TOLERANCE" ]] && [[ "$nex_diff" -le "$TOLERANCE" ]] \
             && [[ "$boot_20" -ge 1 ]] && [[ "$boot_17" -eq 0 ]] && [[ "$nex_17" -ge 1 ]]; then
            pass_row " (48k boot ${boot_diff} px + NEX/layers ${nex_diff} px vs refs; paces 20 ms at 50 Hz, 17 ms at 60 Hz)"
        else
            fail_row " (boot_px=$boot_diff nex_px=$nex_diff boot_20ms=$boot_20 (want >=1) boot_17ms=$boot_17 (want 0) nex_17ms=$nex_17 (want >=1))"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
