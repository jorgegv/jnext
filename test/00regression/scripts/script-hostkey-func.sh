#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #26 WP7 — arm the watches with a host key (§3(e)): test/scripts/dsl/hostkey.jds,
# headless with --script-key (Alt+1 in a window). All its watches start
# disabled:
#   1. the buggy build with no key: nothing armed, the bugs unseen — exit 0;
#   2. the buggy build with key 1 at FRAME 560 (the program runs from ~500):
#      armed, and a watch trips — exit 3;
#   3. the good build with the key: armed 40 frames, nothing trips — exit 0.
if want script-hostkey-func; then
    begin_func script-hostkey-func
    script_hostkey_func_run() {   # $1 = dsl_demo | dsl_demo_buggy; the rest: more jnext arguments
        LANG=C timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --load "$PROJECT_DIR/test/00regression/nex/$1.nex" \
            --map "$PROJECT_DIR/test/00regression/nex/dsl_demo.map" "${@:2}" \
            --delayed-automatic-exit-frames 900 2>&1
    }
    s="$PROJECT_DIR/test/scripts/dsl/hostkey.jds"
    off=$(script_hostkey_func_run dsl_demo_buggy --script "$s") && orc=0 || orc=$?
    bad=$(script_hostkey_func_run dsl_demo_buggy --script "$s" --script-key 560 1) && brc=0 || brc=$?
    out=$(script_hostkey_func_run dsl_demo --script "$s" --script-key 560 1) && rc=0 || rc=$?
    if [[ $orc -eq 0 && $brc -eq 3 && $rc -eq 0 ]] &&
       grep -qF "PASS hostkey: never armed" <<<"$off" &&
       grep -qF "MemWatch enabled" <<<"$bad" && grep -qF "SCRIPT STOP:" <<<"$bad" &&
       grep -qF "MemWatch enabled" <<<"$out" && grep -qF "PASS hostkey: armed 40 frames, nothing tripped" <<<"$out"; then
        pass_row " (buggy unarmed 0; buggy armed by key 1 3; good armed 0)"
    else
        fail_row " (exits unarmed $orc armed-buggy $brc armed-good $rc: $(grep -E 'MemWatch|PASS|STOP|SCRIPT ERROR' <<<"$off$bad$out" | tail -n 3 | tr '\n' ' '))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
