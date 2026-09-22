#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# sdl-options-func: the options the SDL-only build accepted and silently
# ignored (GH #138) either work there now or are refused, loudly.
#
# THE MEASUREMENT (build/sdl-release/jnext, dummy SDL drivers, --silent so the
# loop paces on the wall clock):
#   speed     100 frames at --speed 400, 100 and 50, timed from the log's own
#             timestamps (first pacing line -> "automatic exit triggered"):
#             ~0.5 s, ~2 s and ~4 s. Before, all three took ~2 s.
#   tape      --tape-realtime with a .tap and with a .tzx: the loader logs
#             "mode: realtime" (it logged "mode: fast" whatever the flag said).
#   refused   the headless-only automation options, and every option that only
#             qualifies another one when that other one is absent, exit 1 with
#             a message and never start the machine.
#   headless  --speed under --headless warns that it has no effect there.
if want sdl-options-func; then
    begin_func sdl-options-func

    so_dir="$TMP_DIR/sdl-options"
    rm -rf "$so_dir"; mkdir -p "$so_dir"
    so_sdl="$PROJECT_DIR/build/sdl-release/jnext"
    so_faults=()

    # so_sdl_run <log> <jnext args...>: one SDL-only 48K run; prints the status.
    so_sdl_run() {
        local log=$1 rc=0; shift
        env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
        timeout --foreground --kill-after=5s 60s "$so_sdl" --silent \
            "${SD_CARD_ARGS[@]}" --machine 48k "$@" >"$log" 2>&1 || rc=$?
        echo "$rc"
    }
    # so_ms <log> <fixed string>: the first matching line's log timestamp, in ms.
    so_ms() {
        local ts
        ts=$(grep -F -- "$2" "$1" | head -1 | grep -oP '^\[\K[0-9:.]+(?=\])' || true)
        [[ -n "$ts" ]] || { echo -1; return; }
        awk -F'[:.]' '{ print (($1 * 60 + $2) * 60 + $3) * 1000 + $4 }' <<< "$ts"
    }

    if [[ ! -x "$so_sdl" ]]; then
        fail_row " (SDL-only binary not built: $so_sdl; run 'make sdl-release')"
    else
        # speed
        declare -A so_ms_at=()
        for sp in 400 100 50; do
            log="$so_dir/speed-$sp.log"
            rc=$(so_sdl_run "$log" --speed "$sp" --delayed-automatic-exit-frames 100)
            t0=$(so_ms "$log" "frame pacing:"); t1=$(so_ms "$log" "automatic exit triggered")
            if [[ "$rc" != 0 || "$t0" -lt 0 || "$t1" -lt 0 ]]; then
                so_faults+=("--speed $sp: rc=$rc, no pacing/exit lines")
            else
                so_ms_at[$sp]=$(( t1 - t0 ))
            fi
        done
        if [[ ${#so_ms_at[@]} -eq 3 ]]; then
            (( so_ms_at[400] < 1000 )) \
                || so_faults+=("--speed 400: 100 frames took ${so_ms_at[400]} ms (want ~500)")
            (( so_ms_at[100] >= 1800 && so_ms_at[100] < 3000 )) \
                || so_faults+=("--speed 100: 100 frames took ${so_ms_at[100]} ms (want ~2000)")
            (( so_ms_at[50] >= 3500 )) \
                || so_faults+=("--speed 50: 100 frames took ${so_ms_at[50]} ms (want ~4000)")
        fi

        # tape
        log="$so_dir/tap.log"
        rc=$(so_sdl_run "$log" --load "$PROJECT_DIR/test/00regression/tap/beeper_demo.tap" \
                --tape-realtime --delayed-automatic-exit-frames 5)
        [[ "$rc" == 0 ]] && grep -qF "TAP: tape attached" "$log" && grep -qF "mode: realtime" "$log" \
            || so_faults+=("--tape-realtime .tap: rc=$rc, not attached in realtime mode")
        log="$so_dir/tzx.log"
        rc=$(so_sdl_run "$log" --load "$PROJECT_DIR/test/tzx/Xevious_ZX0_DeciLoad12k8.tzx" \
                --tape-realtime --delayed-automatic-exit-frames 105)
        [[ "$rc" == 0 ]] && grep -qF "TZX: tape attached, mode: realtime" "$log" \
            || so_faults+=("--tape-realtime .tzx: rc=$rc, not attached in realtime mode")

        # refused
        so_png="$so_dir/x.png"
        so_refused=(
            "--delayed-keypress-frames 5 a"
            "--delayed-keypress 1 a"
            "--delayed-nmi-frames 5 nmi"
            "--delayed-snapshot $so_dir/x.sna"
            "--delayed-screenshot-time 1"
            "--delayed-screenshot-frames 1"
            "--delayed-snapshot-frames 1"
            "--inject-org 9000"
            "--inject-pc 9000"
            "--inject-delay 5"
            "--compositor-trace-frame 3"
            "--profile-output $so_dir/p.dat"
            "--magic-port-mode dec"
        )
        for args in "${so_refused[@]}"; do
            log="$so_dir/refused-${args%% *}.log"
            # shellcheck disable=SC2086  # the case's arguments, word-split on purpose
            rc=$(so_sdl_run "$log" $args --delayed-automatic-exit-frames 5)
            if [[ "$rc" != 1 ]] || ! grep -qF "requires" "$log" \
               || grep -qF "Initializing emulator" "$log"; then
                so_faults+=("'$args' without its base/--headless: rc=$rc (want 1, refused up front)")
            fi
        done

        # headless
        log="$so_dir/headless-speed.log"
        rc=0
        timeout --foreground --kill-after=5s 60s "$JNEXT" --headless "${SD_CARD_ARGS[@]}" \
            --machine 48k --speed 200 --delayed-automatic-exit-frames 5 >"$log" 2>&1 || rc=$?
        [[ "$rc" == 0 ]] && grep -qF "warning: --speed has no effect with --headless" "$log" \
            || so_faults+=("--headless --speed: rc=$rc, no warning")

        if [[ ${#so_faults[@]} -gt 0 ]]; then
            fail_row " ($(IFS=';'; echo "${so_faults[*]}"))"
        else
            pass_row " (SDL: --speed 400/100/50 = ${so_ms_at[400]}/${so_ms_at[100]}/${so_ms_at[50]} ms per 100 frames; --tape-realtime loads .tap/.tzx in real time; ${#so_refused[@]} meaningless options refused; headless warns on --speed)"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
