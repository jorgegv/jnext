#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# startup-stack-func: every frontend starts up (and boots NextZXOS) on a 1 MiB
# main-thread stack (GH #308).
#
# WHY THIS ROW EXISTS. main() held the frontend (HeadlessApp / QtApp / SdlApp)
# by value, and each holds the 1.17 MB Emulator by value, so the frontend was
# main()'s own stack frame. On Windows that overflowed the 2 MB MinGW reserve
# before main() ran, hidden for three months by a 16 MB --stack workaround. On
# Linux the same frame (~1.2 MB) fits the 8 MB default, so nothing noticed.
#
# THE MEASUREMENT. Each sub-run executes under `ulimit -s 1024`, read back
# inside the subshell so removing the limit turns the row red (rc 98), and must
# exit 0. Pre-fix the minimum working stack was 1216-1280 KiB (rc 139,
# SIGSEGV). ORACLE of the bound: half of the binutils-ld PE default
# SizeOfStackReserve (0x200000 = 2 MiB), the smallest main-thread stack jnext
# ships on once the reserve is not raised: a 2x margin.
#   1  --version                       (the original symptom's path)
#   2  --headless, 200 frames          (default next machine: firmware boot)
#   3  Qt GUI, offscreen, 200 frames   ($JNEXT)
#   4  SDL-only GUI, dummy drivers, 200 frames (build/sdl-release/jnext)
if want startup-stack-func; then
    begin_func startup-stack-func

    ss_dir="$TMP_DIR/startup-stack"
    rm -rf "$ss_dir"; mkdir -p "$ss_dir"
    ss_sdl="$PROJECT_DIR/build/sdl-release/jnext"
    ss_faults=()

    # ss_run <name> <env...> -- <cmd...>: run under a 1 MiB stack; prints rc.
    ss_run() {
        local name=$1 rc=0; shift
        local -a envv=()
        while [[ "$1" != -- ]]; do envv+=("$1"); shift; done
        shift
        (
            ulimit -s 1024 || exit 97
            [[ "$(ulimit -s)" == 1024 ]] || exit 98
            exec env "${envv[@]}" timeout --foreground --kill-after=5s 120s "$@"
        ) >"$ss_dir/$name.log" 2>&1 || rc=$?
        echo "$rc"
    }

    if [[ ! -x "$ss_sdl" ]]; then
        fail_row " (SDL-only binary not built: $ss_sdl; run 'make sdl-release')"
    else
        rc=$(ss_run version LANG=C -- "$JNEXT" --version)
        [[ "$rc" == 0 ]] && grep -q '^jnext ' "$ss_dir/version.log" \
            || ss_faults+=("--version rc=$rc")
        rc=$(ss_run headless LANG=C -- "$JNEXT" --headless "${SD_CARD_ARGS[@]}" \
                --delayed-automatic-exit-frames 200)
        [[ "$rc" == 0 ]] || ss_faults+=("headless rc=$rc")
        rc=$(ss_run qt QT_QPA_PLATFORM=offscreen -- "$JNEXT" --silent "${SD_CARD_ARGS[@]}" \
                --delayed-automatic-exit-frames 200)
        [[ "$rc" == 0 ]] || ss_faults+=("qt rc=$rc")
        rc=$(ss_run sdl SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy -- "$ss_sdl" --silent \
                "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 200)
        [[ "$rc" == 0 ]] || ss_faults+=("sdl rc=$rc")

        if [[ ${#ss_faults[@]} -gt 0 ]]; then
            fail_row " (on a 1 MiB stack: $(IFS=';'; echo "${ss_faults[*]}"); 139=SIGSEGV 97/98=ulimit not in force)"
        else
            pass_row " (--version, headless, Qt and SDL-only boot on a 1 MiB stack)"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
