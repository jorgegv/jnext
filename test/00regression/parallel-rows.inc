#!/usr/bin/env bash
# shellcheck disable=SC2034  # variables set here are consumed by the driver
# The functional-row scheduler of the regression suite (GH #295): every
# functional row runs in a process of its own, several at once, and the driver
# merges their results in declared order. Sourced by regression.sh after
# test-functions.inc; test/harness-selftest.sh drives it directly against stub
# rows.
#
# WHY A PROCESS PER ROW. Rows used to be SOURCED into the driver one at a time,
# which is the only reason they were serial: they shared one shell's counters,
# one TMP_DIR and one SD clone. The CPU sat ~85% idle for twelve minutes,
# because most rows wait on real time, not on the CPU. A row now gets:
#
#   - its own shell (row-runner.sh), into which it is still SOURCED, so the
#     counter guard and lint-traps keep their meaning — that shell holds the
#     library's trap, and a row's trap would replace it;
#   - its own TMP_DIR ($TMP_DIR/rows/<row>) and RUN_DIR ($RUN_DIR/rows/<row>),
#     which is also its JNEXT_CONFIG_DIR, so nothing a row writes there (GUI
#     preferences, the warm-start cache, private card copies) reaches another;
#   - the run's SD clone as a HARD LINK, or with the `private-sd` tag a copy of
#     its own (reflink where the filesystem can). Untagged rows therefore share
#     one inode, read-mostly, exactly as the screenshot rows always have — and
#     the run fingerprints the clone before and after, so a row that writes to
#     it without the tag is a harness fault, not a silent cross-row effect;
#   - a copy of the run's warm-start cache as the screenshot rows left it, so
#     every row starts from the same cache no matter which rows ran before it.
#
# WHAT IS CHECKED, per row: it reported exactly one result under its own name
# (in the row process, by the counter guard, and again here from the result
# file); a harness fault in the row process is a harness fault of the run;
# a row that dies without a result, or outlives its bound, is a FAIL naming it;
# a row that leaves its directories behind is a harness fault. At the end of
# the run: no directory is left under rows/ (a row that wrote into ANOTHER
# row's directory after that row finished, or before it started — mkdir
# refuses an existing one), and the shared clone is unchanged.
# NOT checked: a row writing into a directory of a row that is running at the
# same moment — that row's own cleanup then removes the evidence. Keep a row's
# paths under its own $TMP_DIR / $RUN_DIR.

# Seconds a row may run before it is killed and FAILed. 10x the slowest row
# (qt-keypress-burst-func, ~56 s on a quiet host): a bound for a hang, not a
# performance budget.
ROW_BOUND=${JNEXT_REGRESSION_ROW_TIMEOUT:-600}
ROW_RUNNER="$SCRIPT_DIR/row-runner.sh"
ROW_RUNNING_PIDS=""

# regression_lanes — set PAR_LANES (screenshot and parallel functional rows)
# and QUIET_LANES (rows tagged `quiet`). JNEXT_TEST_JOBS caps both; unset, the
# parallel phases use every CPU and the quiet phase one lane, which
# JNEXT_TEST_QUIET_JOBS can widen.
regression_lanes() {
    local cpus v
    cpus=$(nproc 2>/dev/null || echo 4)
    for v in JNEXT_TEST_JOBS JNEXT_TEST_QUIET_JOBS; do
        [[ -z "${!v:-}" || "${!v}" =~ ^[1-9][0-9]*$ ]] \
            || harness_fault "${BOLD}$v=${!v}${RESET} is not a positive integer"
    done
    PAR_LANES=${JNEXT_TEST_JOBS:-$cpus}
    QUIET_LANES=${JNEXT_TEST_QUIET_JOBS:-1}
    (( QUIET_LANES <= PAR_LANES )) || QUIET_LANES=$PAR_LANES
}

# sd_fingerprint <image> — content hash and mode, or "absent".
sd_fingerprint() {
    [[ -f "$1" ]] || { echo absent; return 0; }
    echo "$(sha256sum "$1" | awk '{print $1}') $(stat -c '%a %s' "$1")"
}

# func_phases_begin — make the per-row parents, and fingerprint the run's clone.
func_phases_begin() {
    ROWS_TMP="$TMP_DIR/rows"
    ROWS_RUN="$RUN_DIR/rows"
    ROWS_OUT="$TMP_DIR/row-out"
    mkdir -p "$ROWS_TMP" "$ROWS_RUN" "$ROWS_OUT"
    SHARED_SD="$RUN_DIR/sdcard/cspect-next-1gb-fixed.img"
    SHARED_SD_PRINT=${SHARED_SD_PRINT:-$(sd_fingerprint "$SHARED_SD")}
}

# run_row_launch <row> — start one row process in the background; its pid in
# REPLY. Its directories are made here with plain mkdir, so one that already
# exists — something wrote into it before the row started — is refused.
run_row_launch() {
    local row=$1 tdir="$ROWS_TMP/$1" rdir="$ROWS_RUN/$1" mode=$SD_CLONE_MODE
    mkdir "$tdir" 2>/dev/null && mkdir "$rdir" 2>/dev/null \
        || harness_fault "the directory of row ${BOLD}$row${RESET} already exists before the row started" \
                         "Another row wrote into $tdir or $rdir. A row keeps its paths under its own \$TMP_DIR / \$RUN_DIR."
    mkdir "$rdir/sdcard"
    if [[ -f "$SHARED_SD" ]]; then
        if [[ -n "${IS_PRIVATE_SD[$row]:-}" ]]; then
            if cp --reflink=always "$SHARED_SD" "$rdir/sdcard/" 2>/dev/null; then
                mode=reflink
            elif cp --reflink=auto "$SHARED_SD" "$rdir/sdcard/"; then
                mode=copy
            else
                harness_fault "could not give row ${BOLD}$row${RESET} its private SD card in $rdir/sdcard"
            fi
        else
            ln "$SHARED_SD" "$rdir/sdcard/" \
                || harness_fault "could not link the run's SD clone into row ${BOLD}$row${RESET}'s $rdir/sdcard"
        fi
    fi
    if [[ -d "$RUN_DIR/warm-start" ]]; then
        cp -r --reflink=auto "$RUN_DIR/warm-start" "$rdir/" \
            || harness_fault "could not copy the warm-start cache for row ${BOLD}$row${RESET}"
    fi
    JNEXT="$JNEXT" \
    JNEXT_REGRESSION_ROW_TMP="$tdir" JNEXT_REGRESSION_ROW_RUN="$rdir" \
    JNEXT_REGRESSION_ROW_RESULT="$ROWS_OUT/$row.result" \
    JNEXT_REGRESSION_ROW_MASTER_STAMP="$SD_MASTER_STAMP" JNEXT_REGRESSION_ROW_CLONE_MODE="$mode" \
        timeout --kill-after=5s "${ROW_BOUND}s" bash "$ROW_RUNNER" "$row" "$SCRIPTS_DIR/$row.sh" \
        < /dev/null > "$ROWS_OUT/$row.log" 2>&1 &
    REPLY=$!
    ROW_RUNNING_PIDS+=" $REPLY"
}

# run_row_merge <row> <rc> — print a finished row's output and fold its result
# into this shell's counters, exactly once.
run_row_merge() {
    local row=$1 rc=$2 res="$ROWS_OUT/$1.result" k v
    cat "$ROWS_OUT/$row.log"
    if [[ -f "$res.fault" ]]; then
        local msgs=()
        mapfile -t msgs < "$res.fault"
        harness_fault "row ${BOLD}$row${RESET} raised a harness fault in its row process:" "${msgs[@]}"
    fi
    if [[ $rc -eq 124 || $rc -eq 137 ]]; then
        echo ""
        begin_func "$row"
        fail_row " (row process outlived its ${ROW_BOUND} s bound and was killed, exit $rc)"
        show_output "row output" "$(cat "$ROWS_OUT/$row.log")"
        rm -rf "${ROWS_TMP:?}/$row" "${ROWS_RUN:?}/$row"
        return 0
    fi
    if [[ $rc -ne 0 || ! -f "$res" ]]; then
        echo ""
        begin_func "$row"
        fail_row " (row process died with exit $rc before reporting its result)"
        show_output "row output" "$(cat "$ROWS_OUT/$row.log")"
        rm -rf "${ROWS_TMP:?}/$row" "${ROWS_RUN:?}/$row"
        return 0
    fi
    local rp=0 rf=0 rs=0 rl=0 names=() frows=()
    while read -r k v; do
        case $k in
            pass) rp=$v ;; fail) rf=$v ;; skip) rs=$v ;; loaded) rl=$v ;;
            reported) names+=("$v") ;; failed) frows+=("$v") ;;
            *) harness_fault "row ${BOLD}$row${RESET}'s result file has an unknown line: '$k $v'" ;;
        esac
    done < "$res"
    [[ $(( rp + rf + rs )) -eq 1 && ${#names[@]} -eq 1 && "${names[0]}" == "$row" ]] \
        || harness_fault "row ${BOLD}$row${RESET} reported pass/fail/skip $rp/$rf/$rs under the name(s) '${names[*]-}'" \
                         "A row reports exactly one result, under its own name."
    pass=$(( pass + rp )); fail=$(( fail + rf )); skip=$(( skip + rs ))
    LOADED_FAILS=$(( LOADED_FAILS + rl ))
    REPORTED_FUNC+=("${names[@]}")
    FAILED_ROWS+=("${frows[@]+"${frows[@]}"}")
    [[ ! -e "$ROWS_TMP/$row" && ! -e "$ROWS_RUN/$row" ]] \
        || harness_fault "row ${BOLD}$row${RESET} exited but its directories are still there" \
                         "$ROWS_TMP/$row / $ROWS_RUN/$row — its cleanup did not run (a trap of its own replaced the library's?), or another row wrote into them."
}

# run_func_phase <label> <lanes> <row...> — run the rows, at most <lanes> at
# once, launched in the order given; each row's output is printed, and its
# result merged, as soon as it and every row before it have finished.
run_func_phase() {
    local label=$1 lanes=$2; shift 2
    local rows=("$@") n=$#
    (( n > 0 )) || return 0
    (( lanes <= n )) || lanes=$n
    echo ""
    echo -e "${BOLD}Running functional tests: $label — $n row(s), $lanes at a time...${RESET}"
    echo ""
    local -A idx_of=()
    local -a rc_of=() t_start=()
    local next=0 shown=0 running=0 pid rc i phase_t0=$SECONDS
    while (( shown < n )); do
        while (( next < n && running < lanes )); do
            run_row_launch "${rows[next]}"
            idx_of[$REPLY]=$next
            t_start[next]=$SECONDS
            next=$(( next + 1 )); running=$(( running + 1 ))
        done
        rc=0
        # shellcheck disable=SC2086  # the pid list is meant to split
        wait -n -p pid $ROW_RUNNING_PIDS || rc=$?
        [[ -n "${pid:-}" && -n "${idx_of[$pid]:-}" ]] \
            || harness_fault "lost track of a row process (wait returned $rc with no pid)"
        i=${idx_of[$pid]}
        unset "idx_of[$pid]"
        ROW_RUNNING_PIDS=" $ROW_RUNNING_PIDS "
        ROW_RUNNING_PIDS=${ROW_RUNNING_PIDS/ $pid / }
        rc_of[i]=$rc
        running=$(( running - 1 ))
        # JNEXT_REGRESSION_ROW_TIMES: append "<row> <seconds> <exit>" per row —
        # how the quiet set and the lane count were measured (GH #295).
        if [[ -n "${JNEXT_REGRESSION_ROW_TIMES:-}" ]]; then
            echo "${rows[i]} $(( SECONDS - t_start[i] )) $rc" >> "$JNEXT_REGRESSION_ROW_TIMES"
        fi
        pid=""
        while (( shown < n )) && [[ -n "${rc_of[shown]+x}" ]]; do
            run_row_merge "${rows[shown]}" "${rc_of[shown]}"
            shown=$(( shown + 1 ))
        done
    done
    echo -e "  (${label}: $(( SECONDS - phase_t0 )) s)"
}

# func_phases_end — after the last phase: nothing may be left under rows/,
# and the shared SD clone must be what it was.
func_phases_end() {
    local left
    left=$(find "$ROWS_TMP" "$ROWS_RUN" -mindepth 1 -maxdepth 1 2>/dev/null | head -n 5)
    [[ -z "$left" ]] \
        || harness_fault "entries left under the per-row directories after every row finished:" "$left" \
                         "A row wrote into another row's directory after that row had finished."
    local now
    now=$(sd_fingerprint "$SHARED_SD")
    [[ "$now" == "$SHARED_SD_PRINT" ]] \
        || harness_fault "the run's shared SD clone CHANGED during the run ($SHARED_SD_PRINT -> $now)" \
                         "A row without the ${BOLD}private-sd${RESET} tag wrote to the card every untagged row boots." \
                         "Find it (run rows alone and fingerprint the card) and tag it private-sd in functional_tests.conf."
}
