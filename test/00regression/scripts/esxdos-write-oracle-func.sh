#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable; the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# esxdos-write-oracle-func (GH #457): the ORACLE of esxdos-write-func is still
# reproducible. It runs the kept probe NEX under real NextZXOS (booted in jnext, the
# probe started with `.nexload`) on a scratch copy of the SD image, writable and
# write-protected (--sdcard-readonly), and requires every line to equal the recording
# in test/fixtures/esxdos-write-probe/nextzxos.txt, with NO declared divergence. So the
# fixture cannot silently stop being what the image answers (a new default image, a
# core change that alters what NextZXOS sees). If it fails after an image or core change
# the recording must be re-measured (the fixture README says how): that is the intent.
# The scratch copy is made and deleted by esxdos-write-probe-measure.sh, in this row's
# private directory, never in the repository.
if want esxdos-write-oracle-func; then
    begin_func esxdos-write-oracle-func

    W="$RUN_DIR/private/write-oracle"
    mkdir -p "$W"
    probe="$SCRIPT_DIR/esxdos-write-probe.py"
    rec="$PROJECT_DIR/test/fixtures/esxdos-write-probe/nextzxos.txt"

    m_rc=0
    PROBE_FRAMES=3500 timeout --foreground --kill-after=5s 540s bash "$SCRIPT_DIR/esxdos-write-probe-measure.sh" \
        "$JNEXT" "$W" "$RUN_DIR/sdcard/cspect-next-1gb-fixed.img" > "$W/measure.log" 2>&1 || m_rc=$?

    rw_rc=0 ro_rc=0
    rw=$(python3 -I "$probe" compare --exact --recorded "$rec" --mode rw "$W/raw-rw.txt" 2>&1) || rw_rc=$?
    ro=$(python3 -I "$probe" compare --exact --recorded "$rec" --mode ro "$W/raw-ro.txt" 2>&1) || ro_rc=$?
    rm -rf "$W/card"

    if [[ $m_rc -eq 0 && $rw_rc -eq 0 && $ro_rc -eq 0 ]]; then
        pass_row " (the probe, run under real NextZXOS 2.09 on a writable and on a write-protected card, reproduces the recorded transcript exactly)"
    else
        fail_row " (measure rc=$m_rc: '$(tail -n 2 "$W/measure.log" 2>/dev/null | tr '\n' ' ')'; rw compare rc=$rw_rc '$(grep -E -m2 '^(DIFFERS|no END)' <<<"$rw" | tr '\n' ' ' || true)'; ro compare rc=$ro_rc '$(grep -E -m2 '^(DIFFERS|no END)' <<<"$ro" | tr '\n' ' ' || true)')"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
