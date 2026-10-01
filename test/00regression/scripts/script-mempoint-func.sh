#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #26 WP7 — #279's MemPoint=addr,value (§3(d)): test/scripts/dsl/value_predicate.jds.
# The backend evaluates `VALUE == 0xB7` on every write to `mempoint_addr`; the
# good build never writes 0xB7 there and passes, the buggy build writes it at
# its frame 20 and stops with exit 3.
if want script-mempoint-func; then
    begin_func script-mempoint-func
    script_mempoint_func_run() {   # $1 = dsl_demo | dsl_demo_buggy; the rest: more jnext arguments
        LANG=C timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --load "$PROJECT_DIR/test/00regression/nex/$1.nex" \
            --map "$PROJECT_DIR/test/00regression/nex/dsl_demo.map" "${@:2}" \
            --delayed-automatic-exit-frames 900 2>&1
    }
    out=$(script_mempoint_func_run dsl_demo --script "$PROJECT_DIR/test/scripts/dsl/value_predicate.jds") && rc=0 || rc=$?
    bad=$(script_mempoint_func_run dsl_demo_buggy --script "$PROJECT_DIR/test/scripts/dsl/value_predicate.jds") && brc=0 || brc=$?
    if [[ $rc -eq 0 && $brc -eq 3 ]] && grep -qF "PASS value_predicate: 0xB7 never written in 64 frames" <<<"$out" &&
       grep -qF "MemPoint hit at 840B: forbidden value B7 from PC 815C" <<<"$bad" && grep -qF "SCRIPT STOP: MemPoint at PC=815C" <<<"$bad"; then
        pass_row " (good: exit 0; buggy: exit 3 on 0xB7 into 840B from PC 815C)"
    else
        fail_row " (good exit $rc, buggy exit $brc: $(grep -E 'PASS|STOP|ASSERT|SCRIPT ERROR|never ran' <<<"$out$bad" | tail -n 3 | tr '\n' ' '))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
