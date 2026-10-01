#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #26 WP7 — a script that changes the machine (§2.7): test/scripts/dsl/mutation.jds
# skips `trap_insn` by moving PC and patches `patch_byte`; each change is a
# backend MUTATE line, which the row requires. The good build passes; the buggy
# build writes `trap_target` a second time, unskipped — the assert fails and
# the `exit 0` after it in the same rule is not taken: exit 3.
if want script-mutation-func; then
    begin_func script-mutation-func
    script_mutation_func_run() {   # $1 = dsl_demo | dsl_demo_buggy; the rest: more jnext arguments
        LANG=C timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --load "$PROJECT_DIR/test/00regression/nex/$1.nex" \
            --map "$PROJECT_DIR/test/00regression/nex/dsl_demo.map" "${@:2}" \
            --delayed-automatic-exit-frames 900 2>&1
    }
    out=$(script_mutation_func_run dsl_demo --script "$PROJECT_DIR/test/scripts/dsl/mutation.jds") && rc=0 || rc=$?
    bad=$(script_mutation_func_run dsl_demo_buggy --script "$PROJECT_DIR/test/scripts/dsl/mutation.jds") && brc=0 || brc=$?
    if [[ $rc -eq 0 && $brc -eq 3 ]] && grep -qF "MUTATE mem cpu:0x843C 0x0 -> 0x5A by" <<<"$out" && grep -qF "MUTATE reg PC 0x816C -> 0x816F by" <<<"$out" && grep -qF "PASS mutation: trap skipped, patch copied" <<<"$out" &&
       grep -qF "ASSERT FAILED: the trapped write never happened" <<<"$bad" && grep -qF "SCRIPT EXIT 0 not taken: \"the trapped write never happened\" failed at the same boundary (exit 3)" <<<"$bad"; then
        pass_row " (good: exit 0 with both MUTATE lines; buggy: exit 3, the exit 0 not taken)"
    else
        fail_row " (good exit $rc, buggy exit $brc: $(grep -E 'PASS|STOP|ASSERT|SCRIPT ERROR|never ran' <<<"$out$bad" | tail -n 3 | tr '\n' ' '))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
