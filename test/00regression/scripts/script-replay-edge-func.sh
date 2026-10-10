#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #26 WP7 — input lands at the FRAME EDGE, in a script and in a recording
# (§2.6, §7.2 item 1, §8 replay_edge). The dsl_demo program polls Q in its main
# loop and latches its frame counter at the first Q it sees (`first_key`).
#
#   1. test/scripts/dsl/replay_edge.jds presses Q at `main_loop` of the
#      program's frame 30, BEFORE that frame's poll: queued for the edge, it is
#      first seen in frame 31 (at once it would be 30; a frame late, 32);
#   2. a real SDL window under Xvfb (the build/sdl-release binary, as
#      sdl-keypress-func) records a held Q typed with xdotool through the key
#      Router, with --record-script, and a probe script logs the FRAME the
#      program latched it in (`LATCH n at FRAME F`). The emitted script must
#      press Q `on frame F-1` (the recorder's K-1 stamp), and replaying it
#      headless must latch the same value in the same FRAME;
#   3. the control: the same recording with its press moved one frame later
#      latches one frame later.
# Like sdl-keypress-func, half 2 SKIPS (never fails) when the X server cannot
# deliver the key at all — the recording then holds no press.
if want script-replay-edge-func; then
    begin_func script-replay-edge-func
    nexdir="$PROJECT_DIR/test/00regression/nex"
    sdl_bin="$JNEXT_SDL"
    dir="$TMP_DIR/replay-edge"
    mkdir -p "$dir"
    cat > "$dir/probe.jds" <<'JDS'
on write @first_key when mem16[@magic] == 0xD5D5 do
    log "LATCH ${VALUE} at FRAME ${FRAME}"
end
JDS
    edge_run() {   # replay headless; the rest: more jnext arguments
        LANG=C timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --load "$nexdir/dsl_demo.nex" --map "$nexdir/dsl_demo.map" "$@" \
            --delayed-automatic-exit-frames 900 2>&1
    }
    # 1. the script
    out=$(edge_run --script "$PROJECT_DIR/test/scripts/dsl/replay_edge.jds") && rc=0 || rc=$?
    fails=()
    if [[ $rc -ne 0 ]] || ! grep -qF "PASS replay_edge: pressed in frame 30, first seen in frame 31" <<<"$out"; then
        fails+=("script: exit $rc $(grep -E 'ASSERT|STOP|SCRIPT ERROR' <<<"$out" | tail -n 1)")
    fi
    # 2. the recording, from a real window
    edge_skip_reason=""
    if [[ ! -x "$sdl_bin" ]]; then
        fails+=("SDL-only binary not built: $sdl_bin")
    elif ! command -v xvfb-run &>/dev/null || ! command -v xdotool &>/dev/null; then
        edge_skip_reason="xvfb-run or xdotool not available"
    else
        # shellcheck disable=SC2016
        env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=x11 SDL_AUDIODRIVER=dummy LANG=C \
        timeout --foreground --kill-after=5s 120s \
        xvfb-run -d --server-args="-screen 0 1280x1024x24" bash -c '
            set -uo pipefail
            bin="$1"; nexdir="$2"; dir="$3"; shift 3
            "$bin" --machine next --silent "$@" --load "$nexdir/dsl_demo.nex" \
                --map "$nexdir/dsl_demo.map" --script "$dir/probe.jds" \
                --record-script "$dir/rec.jds" --delayed-automatic-exit-frames 700 \
                > "$dir/record.log" 2>&1 &
            pid=$!
            wid=""
            for _ in $(seq 1 100); do
                wid=$(xdotool search --onlyvisible --name JNEXT 2>/dev/null | head -1) || true
                [ -n "$wid" ] && break
                sleep 0.2
            done
            sleep 4
            if [ -n "$wid" ]; then
                xdotool windowactivate --sync "$wid" 2>/dev/null || true
                xdotool mousemove --window "$wid" 100 100 click 1 2>/dev/null || true
                sleep 0.3
                xdotool keydown q 2>/dev/null || true
                sleep 0.3
                xdotool keyup q 2>/dev/null || true
            fi
            wait $pid
        ' _ "$sdl_bin" "$nexdir" "$dir" "${SD_CARD_ARGS[@]}" >/dev/null 2>&1 || true
        rec_latch=$(grep -oE 'LATCH [0-9]+ at FRAME [0-9]+' "$dir/record.log" 2>/dev/null | head -n 1) || rec_latch=""
        press=$(grep -oE '^on frame [0-9]+ do press "q" end' "$dir/rec.jds" 2>/dev/null | head -n 1) || press=""
        if [[ -z "$press" ]]; then
            edge_skip_reason="the X server delivered no Q to the window"
        elif [[ -z "$rec_latch" ]]; then
            fails+=("recording: Q pressed but the program latched nothing")
        else
            f=${rec_latch##* }
            p=${press#on frame }
            p=${p%% *}
            [[ $p -eq $((f - 1)) ]] || fails+=("recorder stamped the press at $p, the program saw it in $f")
            rp=$(cd "$dir" && edge_run --script rec.jds --script probe.jds) && prc=0 || prc=$?
            rp_latch=$(grep -oE 'LATCH [0-9]+ at FRAME [0-9]+' <<<"$rp" | head -n 1) || rp_latch=""
            [[ $prc -eq 0 && "$rp_latch" == "$rec_latch" ]] ||
                fails+=("replay: exit $prc, '$rp_latch' vs recorded '$rec_latch'")
            # 3. the control: the press one frame later
            sed "s/^on frame $p do press \"q\" end/on frame $((p + 1)) do press \"q\" end/" "$dir/rec.jds" > "$dir/late.jds"
            lt=$(cd "$dir" && edge_run --script late.jds --script probe.jds) && lrc=0 || lrc=$?
            lt_latch=$(grep -oE 'LATCH [0-9]+ at FRAME [0-9]+' <<<"$lt" | head -n 1) || lt_latch=""
            [[ $lrc -eq 0 && "${lt_latch##* }" == "$((f + 1))" ]] ||
                fails+=("control: a press one frame later latched '$lt_latch' (recorded '$rec_latch')")
        fi
    fi
    if [[ ${#fails[@]} -gt 0 ]]; then
        fail_row " (${fails[*]})"
    elif [[ -n "$edge_skip_reason" ]]; then
        skip_row " (script half PASS; recording half: $edge_skip_reason)"
    else
        pass_row " (script: seen in frame 31; recorded: press at F-1 for $rec_latch, replayed identically, a frame late latches later)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
