#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #26 WP7 — the Copper side of the palette split (§3(f)):
# test/scripts/dsl/copper.jds against the dsl_demo NEX. The MOVE after the
# WAIT must land on line 95 every frame, the WAIT is satisfied on the Copper's
# own line 95 at or past its threshold (GH #181), and the list HALTs. The
# buggy build WAITs for line 96 — exit 3.
if want script-copper-func; then
    begin_func script-copper-func
    script_copper_func_run() {   # $1 = dsl_demo | dsl_demo_buggy; the rest: more jnext arguments
        LANG=C timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --load "$PROJECT_DIR/test/00regression/nex/$1.nex" \
            --map "$PROJECT_DIR/test/00regression/nex/dsl_demo.map" "${@:2}" \
            --delayed-automatic-exit-frames 900 2>&1
    }
    out=$(script_copper_func_run dsl_demo --script "$PROJECT_DIR/test/scripts/dsl/copper.jds") && rc=0 || rc=$?
    bad=$(script_copper_func_run dsl_demo_buggy --script "$PROJECT_DIR/test/scripts/dsl/copper.jds") && brc=0 || brc=$?
    if [[ $rc -eq 0 && $brc -eq 3 ]] && grep -qF "PASS copper: 64 frames, the split on line 95 every time" <<<"$out" &&
       grep -qF "Copper MOVE NR43=02 at copper PC 2 on cvc 96" <<<"$bad" && grep -qF "ASSERT FAILED: the palette split lands on line 95" <<<"$bad"; then
        pass_row " (good: exit 0; buggy: exit 3, the split on line 96)"
    else
        fail_row " (good exit $rc, buggy exit $brc: $(grep -E 'PASS|STOP|ASSERT|SCRIPT ERROR|never ran' <<<"$out$bad" | tail -n 3 | tr '\n' ' '))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
