#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/dzrp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../dzrp-functions.inc"
# shellcheck source=test/00regression/debugger-start-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../debugger-start-functions.inc"

# GH #548 (--debugger on the Qt loop owner), three launches under QT_QPA_PLATFORM=offscreen
# (as dzrp-qt-func):
#   1. `--debugger --dzrp-port 0 NEX`: paused at the NEX entry; the debugger window opens through
#      the manager's own check for a pause it did not make (the window itself is PBPUI-15's); a
#      DZRP client reads the PC, held, and CONTINUE runs it.
#   2. `--debugger NEX` with NO protocol port: the Qt GUI has a debugger window, which IS the
#      debugger, so nothing listens: the log has the stop line and no `dzrp: listening` (the
#      value main.cpp feeds the implied-port rule).
#   3. `--break-at nosuch --script s.jds NEX`: refused at the landing with exit 1 and NOT ONE
#      instruction of the program runs: a script rule on 0x8000 never logs.
if want debugger-qt-func; then
    begin_func debugger-qt-func
    log="$TMP_DIR/debugger-qt-func.log"
    qt_env=(env QT_QPA_PLATFORM=offscreen SDL_AUDIODRIVER=dummy)
    fails=()
    if ! dbgstart_have_python debugger-qt-func; then
        :
    else
        if ! dzrp_launch_cmd "$log" "${qt_env[@]}" \
                "$JNEXT" "${SD_CARD_ARGS[@]}" --silent --rewind-buffer-size 0 --debugger --dzrp-port 0 \
                --load "$DBGSTART_NEX"; then
            fails+=("the Qt frontend never logged 'dzrp: listening on': $(tail -n 3 "$log" | tr '\n' ' ')")
        elif ! dbgstart_wait_stop "$log" "$DZRP_PID"; then
            fails+=("QtApp: no stop within 90 s: $(tail -n 3 "$log" | tr '\n' ' ')")
        elif dzrp_peer startup "$(dbgstart_pc_of nex "$DBGSTART_NEX")"; then
            ok_msg="QtApp: ${DZRP_OUT#PASS }"
        else
            fails+=("QtApp: ${DZRP_OUT#FAIL }; $(grep -a 'debugger:' "$log" | tr '\n' '|')")
        fi
        dzrp_stop

        # 2: no port, a window: nothing listens.
        log2="$TMP_DIR/debugger-qt-noport.log"
        LANG=C timeout --foreground --kill-after=5s 60s "${qt_env[@]}" "$JNEXT" "${SD_CARD_ARGS[@]}" \
            --silent --rewind-buffer-size 0 --debugger --load "$DBGSTART_NEX" >"$log2" 2>&1 &
        qpid=$!
        for _ in $(seq 1 150); do
            grep -aq "debugger: stopped at" "$log2" && break
            kill -0 "$qpid" 2>/dev/null || break
            sleep 0.1
        done
        sleep 0.5
        grep -aq 'debugger: stopped at \$8000 (the NEX entry)' "$log2" ||
            fails+=("--debugger with no port: no stop line: $(tail -n 3 "$log2" | tr '\n' ' ')")
        ! grep -aq "dzrp: listening" "$log2" || fails+=("--debugger with no port in the Qt GUI bound DZRP")
        kill -TERM "$qpid" 2>/dev/null || true
        wait "$qpid" 2>/dev/null || true

        # 3: an unresolved --break-at never lets the program run.
        jds="$TMP_DIR/debugger-qt-ran.jds"
        echo 'on execute 0x8000 once do log "REV-RAN-8000" end' >"$jds"
        out=$(LANG=C timeout --foreground --kill-after=5s 60s "${qt_env[@]}" "$JNEXT" "${SD_CARD_ARGS[@]}" \
            --silent --rewind-buffer-size 0 --break-at nosuch --script "$jds" --load "$DBGSTART_NEX" 2>&1) && rc=0 || rc=$?
        [[ $rc -eq 1 ]] || fails+=("--break-at nosuch exited $rc, not 1")
        grep -aq -- "--break-at nosuch: not an address, a symbol or a source line" <<<"$out" ||
            fails+=("--break-at nosuch: the refusal does not name nosuch")
        ! grep -aq "REV-RAN-8000" <<<"$out" || fails+=("--break-at nosuch: the program ran an instruction at 0x8000 before the exit")

        if (( ${#fails[@]} == 0 )); then
            pass_row " (${ok_msg:-}; no port in the Qt GUI binds nothing; an unresolved --break-at exits 1 with 0x8000 never run)"
        else
            fail_row " (${fails[*]})"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
