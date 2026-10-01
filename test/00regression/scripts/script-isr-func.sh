#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #26 WP7 — #279's interrupt-exit audit (ChaseTheBug Int=, §3(c)):
# test/scripts/dsl/span_invariants.jds against the dsl_demo NEX. At every exit
# of the IM 2 handler the registers (SP included), the return address and the
# MMU slots are as at its entry, and IFF1 is set. The good build passes. The
# buggy build's handler carries five faults, selected by its `isr_fault` byte
# (1 by default; the row sets the others with a one-line script), each breaking
# ONE invariant — and each half asserts that invariant's own log line:
#   1 IY returned one higher        -> dump_diff names IY, "isr clobbered registers"
#   2 MMU slot 7 left on page 0x0F  -> dump_diff names MMU7, "isr changed an MMU slot"
#   3 the return address rewritten  -> "top of stack modified", "isr modified return address"
#   4 the return made without EI    -> "isr exit with interrupts disabled"
#   5 SP two bytes deeper           -> dump_diff names SP, "isr clobbered registers"
if want script-isr-func; then
    begin_func script-isr-func
    script_isr_func_run() {   # $1 = dsl_demo | dsl_demo_buggy; the rest: more jnext arguments
        LANG=C timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --load "$PROJECT_DIR/test/00regression/nex/$1.nex" \
            --map "$PROJECT_DIR/test/00regression/nex/dsl_demo.map" "${@:2}" \
            --delayed-automatic-exit-frames 900 2>&1
    }
    s="$PROJECT_DIR/test/scripts/dsl/span_invariants.jds"
    out=$(script_isr_func_run dsl_demo --script "$s") && rc=0 || rc=$?
    fails=()
    if [[ $rc -ne 0 ]] || ! grep -qF "PASS span_invariants: 64 frames of clean handler exits" <<<"$out"; then
        fails+=("good build: exit $rc")
    fi
    # $1 = the fault, the rest = the lines its run must log
    isr_fault_half() {
        local n="$1"; shift
        local extra=()
        if [[ "$n" != 1 ]]; then
            printf 'on execute @main_loop once when mem16[@magic] == 0xD5D5 do set mem[@isr_fault] = %s end\n' "$n" \
                > "$TMP_DIR/isr-fault-$n.jds"
            extra=(--script "$TMP_DIR/isr-fault-$n.jds")
        fi
        local bad brc line
        bad=$(script_isr_func_run dsl_demo_buggy --script "$s" "${extra[@]}") && brc=0 || brc=$?
        [[ $brc -eq 3 ]] || { fails+=("fault $n: exit $brc"); return; }
        for line in "$@"; do
            grep -qF "$line" <<<"$bad" || { fails+=("fault $n: no '$line'"); return; }
        done
    }
    isr_fault_half 1 "dump_diff isr: IY" "SCRIPT STOP: isr clobbered registers at PC=820B"
    isr_fault_half 2 "dump_diff isr: MMU7 01 -> 0F" "SCRIPT STOP: isr changed an MMU slot at PC=820B"
    isr_fault_half 3 "Warning: top of stack modified" "SCRIPT STOP: isr modified return address at PC=820B"
    isr_fault_half 4 "SCRIPT STOP: isr exit with interrupts disabled at PC=820B"
    isr_fault_half 5 "dump_diff isr: SP" "SCRIPT STOP: isr clobbered registers at PC=820B"
    if [[ ${#fails[@]} -eq 0 ]]; then
        pass_row " (good 0; each of the five handler faults exits 3 on its own invariant: IY, MMU7, return address, IFF1, SP)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
