#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# rzx-record-status-func: an --rzx-record that cannot be written is an ERROR —
# the process exits non-zero — in all three frontends (headless, the Qt GUI
# offscreen, the SDL-only build), the same contract as a --load that fails
# (load-exit-status-func) and a --record that fails (video-record-status-func).
#
# WHY THIS ROW EXISTS. Emulator::stop_rzx_recording() dropped the bool from
# RzxRecorder::stop(), and rzx::write() judged the file by good() BEFORE the
# stream was closed, so a small recording still sitting in the buffer counted
# as written. A recording that never reached the disk exited 0.
#
# THE MEASUREMENT, per frontend.
#   nodir    --rzx-record into a directory that does not exist: refused at
#            start-up with "--rzx-record: cannot write", status 1, and FAST —
#            the requested run (500000 frames, hours) is never started.
#   full     --rzx-record /dev/full (it opens; every flush fails ENOSPC): the
#            run completes, "RZX: failed to write" is logged, exit status 1 —
#            jnext's own failure status, so a timeout (124) or a kill (137)
#            cannot pass for it: an unattended exit must never stop on a
#            dialog reporting the failure (the Qt window's closeEvent shows
#            one when a USER closes it with a recording running).
#   control  a writable path: exit 0 and a file with the RZX! signature, so
#            the two failures above are failures of the path, not of the run.
if want rzx-record-status-func; then
    begin_func rzx-record-status-func

    rs_dir="$TMP_DIR/rzx-record-status"
    rm -rf "$rs_dir"; mkdir -p "$rs_dir"
    rs_sdl="$PROJECT_DIR/build/sdl-release/jnext"
    rs_faults=()

    # rs_run <frontend> <log> <timeout> <jnext args...>: one 48K run of that
    # frontend, combined output to <log>; prints the exit status.
    rs_run() {
        local fe=$1 log=$2 to=$3 rc=0; shift 3
        case $fe in
            headless)
                timeout --foreground --kill-after=5s "$to" "$JNEXT" --headless \
                    "${SD_CARD_ARGS[@]}" --machine 48k "$@" >"$log" 2>&1 || rc=$? ;;
            qt)
                env QT_QPA_PLATFORM=offscreen \
                timeout --foreground --kill-after=5s "$to" "$JNEXT" --silent \
                    "${SD_CARD_ARGS[@]}" --machine 48k "$@" >"$log" 2>&1 || rc=$? ;;
            sdl)
                env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
                timeout --foreground --kill-after=5s "$to" "$rs_sdl" --silent \
                    "${SD_CARD_ARGS[@]}" --machine 48k "$@" >"$log" 2>&1 || rc=$? ;;
        esac
        echo "$rc"
    }

    if [[ ! -x "$rs_sdl" ]]; then
        fail_row " (SDL-only binary not built: $rs_sdl; run 'make sdl-release')"
    else
        for fe in headless qt sdl; do
            # nodir: refused before the machine runs.
            log="$rs_dir/$fe-nodir.log"
            t0=$(date +%s)
            rc=$(rs_run "$fe" "$log" 60s --rzx-record "$rs_dir/no/such/dir/x.rzx" \
                    --delayed-automatic-exit-frames 500000)
            elapsed=$(( $(date +%s) - t0 ))
            if [[ "$rc" != 1 || "$elapsed" -gt 20 ]] \
               || ! grep -qF -- "--rzx-record: cannot write" "$log" \
               || grep -qF "automatic exit triggered" "$log"; then
                rs_faults+=("$fe unwritable path: rc=$rc after ${elapsed}s (want 1, fast, 'cannot write')")
            fi

            # full: the write fails when the file is saved.
            log="$rs_dir/$fe-full.log"
            rc=$(rs_run "$fe" "$log" 120s --rzx-record /dev/full \
                    --delayed-automatic-exit-frames 10)
            if [[ "$rc" != 1 ]] || ! grep -qF "RZX: failed to write '/dev/full'" "$log"; then
                rs_faults+=("$fe /dev/full: rc=$rc (want 1 — not 0, not a timeout or kill — + 'RZX: failed to write')")
            fi

            # control: a writable path.
            out="$rs_dir/$fe-ok.rzx"; log="$rs_dir/$fe-ok.log"
            rc=$(rs_run "$fe" "$log" 120s --rzx-record "$out" \
                    --delayed-automatic-exit-frames 10)
            magic=$(xxd -l 4 -p "$out" 2>/dev/null || true)
            if [[ "$rc" != 0 || "$magic" != "525a5821" ]]; then
                rs_faults+=("$fe control: rc=$rc magic=${magic:-none} (want 0 + RZX!)")
            fi
        done

        if [[ ${#rs_faults[@]} -gt 0 ]]; then
            fail_row " ($(IFS=';'; echo "${rs_faults[*]}"))"
        else
            pass_row " (headless/Qt/SDL: an unwritable --rzx-record is refused at start-up, a failed write exits non-zero, a writable one exits 0)"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
