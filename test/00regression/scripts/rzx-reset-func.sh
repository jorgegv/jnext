#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# rzx-reset-func: a power-on reset during an --rzx-record session WRITES the
# recording and ends it there, in all three frontends (headless, the Qt GUI
# offscreen, the SDL-only build).
#
# WHY THIS ROW EXISTS. A hard reset is a host cold boot that reconstructs the
# Emulator in place — and the RZX recorder with it. The recording was destroyed
# unwritten: no file, no message, exit status 0.
#
# THE MEASUREMENT. A 48K machine is given, by --inject, a program that waits 50
# frames and then asks for a hard reset (NR 0x02 bit 1), while --rzx-record
# runs. Per frontend:
#   the log says the reset ended the recording after N frames, and that N
#   frames were saved; the run exits 0 (it was written); the file has the RZX!
#   signature; and headless plays it back with the same N frames.
#   lost: the same, recording to /dev/full (it opens; the write fails): the
#   run exits 1 and logs "RZX: failed to write". In Qt the run is unattended
#   (--delayed-automatic-exit-frames), so the window must log that it asks
#   nothing — not post a dialog nobody would answer.
if want rzx-reset-func; then
    begin_func rzx-reset-func

    rr_dir="$TMP_DIR/rzx-reset"
    rm -rf "$rr_dir"; mkdir -p "$rr_dir"
    rr_sdl="$PROJECT_DIR/build/sdl-release/jnext"
    rr_prog="$rr_dir/hardreset.bin"
    rr_faults=()
    # EI; LD B,50; loop: HALT; DJNZ loop; LD BC,0x243B; LD A,2; OUT (C),A
    # (select NR 0x02); INC B (BC = 0x253B); OUT (C),A (NR 0x02 = 2: hard
    # reset); JR $.
    printf '\xfb\x06\x32\x76\x10\xfd\x01\x3b\x24\x3e\x02\xed\x79\x04\xed\x79\x18\xfe' \
        > "$rr_prog"

    # rr_run <frontend> <log> <jnext args...>: one 48K run; prints the status.
    rr_run() {
        local fe=$1 log=$2 rc=0; shift 2
        case $fe in
            headless)
                timeout --foreground --kill-after=5s 60s "$JNEXT" --headless \
                    "${SD_CARD_ARGS[@]}" --machine 48k "$@" >"$log" 2>&1 || rc=$? ;;
            qt)
                env QT_QPA_PLATFORM=offscreen \
                timeout --foreground --kill-after=5s 120s "$JNEXT" --silent \
                    "${SD_CARD_ARGS[@]}" --machine 48k "$@" >"$log" 2>&1 || rc=$? ;;
            sdl)
                env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
                timeout --foreground --kill-after=5s 120s "$rr_sdl" --silent \
                    "${SD_CARD_ARGS[@]}" --machine 48k "$@" >"$log" 2>&1 || rc=$? ;;
        esac
        echo "$rc"
    }

    if [[ ! -x "$rr_sdl" ]]; then
        fail_row " (SDL-only binary not built: $rr_sdl; run 'make sdl-release')"
    else
        for fe in headless qt sdl; do
            out="$rr_dir/$fe.rzx"; log="$rr_dir/$fe.log"
            rc=$(rr_run "$fe" "$log" --inject "$rr_prog" --inject-delay 100 \
                    --rzx-record "$out" --delayed-automatic-exit-frames 300)
            ended=$(grep -oP "RZX: the power-on reset ends the recording to '\Q$out\E' after \K[0-9]+" \
                        "$log" | head -1 || true)
            saved=$(grep -oP "RZX: recording saved — \K[0-9]+(?= frames to '\Q$out\E')" \
                        "$log" | head -1 || true)
            magic=$(xxd -l 4 -p "$out" 2>/dev/null || true)
            if [[ "$rc" != 0 || -z "$ended" || "$saved" != "$ended" || "$magic" != "525a5821" ]]; then
                rr_faults+=("$fe: rc=$rc ended=${ended:-none} saved=${saved:-none} magic=${magic:-none}")
                continue
            fi
            log="$rr_dir/$fe-play.log"
            rc=$(rr_run headless "$log" --rzx-play "$out" --delayed-automatic-exit-frames 20)
            [[ "$rc" == 0 ]] && grep -qF "RZX: playback started — $saved frames" "$log" \
                || rr_faults+=("$fe: its $saved-frame file does not play back (rc=$rc)")

            # lost: the write fails at the reset.
            log="$rr_dir/$fe-lost.log"
            rc=$(rr_run "$fe" "$log" --inject "$rr_prog" --inject-delay 100 \
                    --rzx-record /dev/full --delayed-automatic-exit-frames 300)
            if [[ "$rc" != 1 ]] || ! grep -qF "RZX: failed to write '/dev/full'" "$log"; then
                rr_faults+=("$fe lost: rc=$rc (want 1 + 'RZX: failed to write')")
            elif [[ "$fe" == qt ]] \
                 && { ! grep -qF "unattended run, so no dialog" "$log" \
                      || grep -qF "reporting it in a dialog" "$log"; }; then
                rr_faults+=("qt lost: the unattended run was treated as interactive (dialog)")
            fi
        done

        if [[ ${#rr_faults[@]} -gt 0 ]]; then
            fail_row " ($(IFS=';'; echo "${rr_faults[*]}"))"
        else
            pass_row " (headless/Qt/SDL: a hard reset writes the running --rzx-record, ends it, exits 0, and the file plays back; a write lost there exits 1, with no dialog in an unattended Qt run)"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
