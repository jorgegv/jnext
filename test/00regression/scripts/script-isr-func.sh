#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #26 WP7 — #279's interrupt-exit audit (ChaseTheBug Int=, §3(c)):
# test/scripts/dsl/span_invariants.jds against the dsl_demo NEX. At every exit
# of the IM 2 handler the registers, the return address and the MMU slots are
# as at its entry, and IFF1 is set. The good build passes; the buggy build's
# handler returns with IY one higher — exit 3, the dump naming IY.
if want script-isr-func; then
    begin_func script-isr-func
    script_isr_func_run() {   # $1 = dsl_demo | dsl_demo_buggy; the rest: more jnext arguments
        LANG=C timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --load "$PROJECT_DIR/test/00regression/nex/$1.nex" \
            --map "$PROJECT_DIR/test/00regression/nex/dsl_demo.map" "${@:2}" \
            --delayed-automatic-exit-frames 900 2>&1
    }
    out=$(script_isr_func_run dsl_demo --script "$PROJECT_DIR/test/scripts/dsl/span_invariants.jds") && rc=0 || rc=$?
    bad=$(script_isr_func_run dsl_demo_buggy --script "$PROJECT_DIR/test/scripts/dsl/span_invariants.jds") && brc=0 || brc=$?
    if [[ $rc -eq 0 && $brc -eq 3 ]] && grep -qF "PASS span_invariants: 64 frames of clean handler exits" <<<"$out" &&
       grep -qF "Warning: registers differ on exit from isr" <<<"$bad" && grep -qF "dump_diff isr: IY" <<<"$bad" && grep -qF "SCRIPT STOP: isr clobbered registers at PC=81E4" <<<"$bad"; then
        pass_row " (good: exit 0; buggy: exit 3 at isr_exit 81E4, IY named)"
    else
        fail_row " (good exit $rc, buggy exit $brc: $(grep -E 'PASS|STOP|ASSERT|SCRIPT ERROR|never ran' <<<"$out$bad" | tail -n 3 | tr '\n' ' '))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
