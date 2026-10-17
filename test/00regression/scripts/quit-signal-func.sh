#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# quit-signal-func: SIGTERM and SIGINT end every frontend through its normal
# teardown and leave the process dead BY the signal (GH #364).
#
# WHY THIS ROW EXISTS. SDL3's SDL_Init installs a persistent SIGINT/SIGTERM
# handler that only queues SDL_EVENT_QUIT. The Qt GUI never read it, so the
# signal was swallowed and only SIGKILL stopped it; the SDL-only frontend
# exited 0 (a stopped run read as success); headless died on the spot with its
# --wav-record header still saying 0 bytes of data.
#
# THE MEASUREMENT. Each case starts the real binary, waits (bounded) until its
# --wav-record file holds samples (the frame loop is running, in all three
# frontends, without /proc), sends the signal to its PID, and checks:
#   * it exits within 10 s TERMINATED BY the signal (a python parent reads
#     Popen.returncode: -15 / -2 = WIFSIGNALED, WTERMSIG): the signal is
#     re-raised after the teardown. Bash shows 143/130 for that AND for
#     exit(143), so the status a shell reports cannot pin the D2 decision;
#   * the log says "SIG<X> received: quitting" exactly once;
#   * the WAV header is consistent, read by python's wave module (a foreign
#     reader): AudioRecorder writes data size 0 at start and the real size only
#     at stop(), so a consistent header proves the orderly teardown ran.
#   * the *-paused cases hold headless paused through a DZRP client first: the
#     poll sits before the paused-with-a-remote wait, which is the only place its
#     position matters (no WAV assertion there: no frame ran while paused).
#   (quit-signal-rzx-func covers the Qt RZX-write-failure case.)
#
# SIGINT: a row's `&` job inherits SIGINT as IGNORED and jnext correctly keeps
# that (unit row QSIG-04), so quit-signal-run.py starts jnext as a child with
# SIGINT back at SIG_DFL and the row signals the PID that script reports. No `set -m`: it would move jnext out of the
# process group the harness kills on abort. Every wait is a bounded poll loop;
# a case that overruns is killed (KILL, by PID) and reported by name.
if want quit-signal-func; then
    begin_func quit-signal-func

    qs_dir="$TMP_DIR/quit-signal"
    rm -rf "$qs_dir"; mkdir -p "$qs_dir"
    qs_sdl="$JNEXT_SDL"
    qs_faults=()
    qs_rc=0
    source "$(dirname "${BASH_SOURCE[0]}")/quit-signal-functions.inc"

    if [[ ! -x "$qs_sdl" ]]; then
        fail_row " (SDL-only binary not built: $qs_sdl; run 'make sdl-release')"
    else
        qs_case qt       qt-term       TERM
        qs_case qt       qt-int        INT
        qs_case sdl      sdl-term      TERM
        qs_case sdl      sdl-int       INT
        qs_case headless headless-term TERM
        qs_case headless headless-int  INT
        qs_case headless headless-term-paused TERM
        qs_case headless headless-int-paused  INT
        if [[ ${#qs_faults[@]} -gt 0 ]]; then
            fail_row " ($(IFS=';'; echo "${qs_faults[*]}"))"
        else
            pass_row " (SIGTERM/SIGINT: Qt, SDL-only and headless exit BY the signal (returncode -15/-2), logged once, WAV finalized; headless also while a DZRP client holds it paused)"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
