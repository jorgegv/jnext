#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #26 WP7 — #279's code-area write guard (ChaseTheBug MemWatch, §3(a)):
# test/scripts/dsl/range_watch.jds against the dsl_demo NEX. The good build runs
# 64 of its frames with no write into code and passes; the buggy build's stray
# write into `__data_crt_head - 1` — the LAST byte of the guarded range, which
# ends where ChaseTheBug's does, at the crt's `__data_crt_head` (a `; const` of
# the MAP), so a range whose top end were exclusive would miss it — stops with
# exit 3, naming the address and the writing instruction (0x8172).
if want script-guard-func; then
    begin_func script-guard-func
    script_guard_func_run() {   # $1 = dsl_demo | dsl_demo_buggy; the rest: more jnext arguments
        LANG=C timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --load "$PROJECT_DIR/test/00regression/nex/$1.nex" \
            --map "$PROJECT_DIR/test/00regression/nex/dsl_demo.map" "${@:2}" \
            --delayed-automatic-exit-frames 900 2>&1
    }
    out=$(script_guard_func_run dsl_demo --script "$PROJECT_DIR/test/scripts/dsl/range_watch.jds") && rc=0 || rc=$?
    bad=$(script_guard_func_run dsl_demo_buggy --script "$PROJECT_DIR/test/scripts/dsl/range_watch.jds") && brc=0 || brc=$?
    if [[ $rc -eq 0 && $brc -eq 3 ]] && grep -qF "PASS range_watch: no write into code in 64 frames" <<<"$out" &&
       grep -qF "write to main code 8317 <- EE from PC 8172" <<<"$bad" && grep -qF "SCRIPT STOP: write into main code area at PC=8172" <<<"$bad"; then
        pass_row " (good: exit 0; buggy: exit 3 at the write into 8317 from PC 8172)"
    else
        fail_row " (good exit $rc, buggy exit $brc: $(grep -E 'PASS|STOP|ASSERT|SCRIPT ERROR|never ran' <<<"$out$bad" | tail -n 3 | tr '\n' ' '))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
