#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable; the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# esxdos-write-func (GH #457): a real guest program (the kept probe NEX, test/
# 00regression/esxdos-write-probe.py) calls F_UNLINK, F_RENAME, F_MKDIR, F_RMDIR,
# F_TRUNCATE, F_FTRUNCATE, F_CHMOD and M_GETERR on a host folder, and every answer
# it prints is compared, step by step, with what REAL NextZXOS 2.09 answered to the
# same program (test/fixtures/esxdos-write-probe/nextzxos.txt, measured once by
# esxdos-write-oracle-func). The differences that remain are the declared ones, each
# with its reason in the generator's DIVERGE table.
#   1. the probe NEX files the generator writes are the ones the recording was made
#      with (their sha256 is in its header): a probe edited without re-measuring fails;
#   2. rw: --esxdos-stub-root DIR --esxdos-stub-writable, an empty DIR: the transcript
#      matches the recording;
#   3. ro: the same DIR without --esxdos-stub-writable (a write-protected volume): the
#      transcript matches the recording's [ro] section, and the host tree (names, sizes,
#      write bits, sha256) is identical after it.
if want esxdos-write-func; then
    begin_func esxdos-write-func

    W="$TMP_DIR/write-probe"
    mkdir -p "$W/root"
    probe="$SCRIPT_DIR/esxdos-write-probe.py"
    rec="$PROJECT_DIR/test/fixtures/esxdos-write-probe/nextzxos.txt"

    ver_rc=0
    ver=$(python3 -I "$probe" verify --recorded "$rec" 2>&1) || ver_rc=$?
    python3 -I "$probe" nex --mode rw "$W/W457RW.NEX"
    python3 -I "$probe" nex --mode ro "$W/W457RO.NEX"

    rw_rc=0
    timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --magic-port 0xCAFE --magic-port-mode line \
        --esxdos-stub-root "$W/root" --esxdos-stub-writable --load "$W/W457RW.NEX" \
        --delayed-automatic-exit-frames 400 > "$W/rw.log" 2>&1 || rw_rc=$?
    grep -E '^(S[0-9A-F]{4} |END)' "$W/rw.log" > "$W/raw-rw.txt" || true
    cmp_rw_rc=0
    cmp_rw=$(python3 -I "$probe" compare --recorded "$rec" --mode rw "$W/raw-rw.txt" 2>&1) || cmp_rw_rc=$?

    before=$(python3 -I "$probe" tree "$W/root")
    ro_rc=0
    timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --magic-port 0xCAFE --magic-port-mode line \
        --esxdos-stub-root "$W/root" --load "$W/W457RO.NEX" \
        --delayed-automatic-exit-frames 400 > "$W/ro.log" 2>&1 || ro_rc=$?
    grep -E '^(S[0-9A-F]{4} |END)' "$W/ro.log" > "$W/raw-ro.txt" || true
    cmp_ro_rc=0
    cmp_ro=$(python3 -I "$probe" compare --recorded "$rec" --mode ro "$W/raw-ro.txt" 2>&1) || cmp_ro_rc=$?
    after=$(python3 -I "$probe" tree "$W/root")

    if [[ $ver_rc -eq 0 && $rw_rc -eq 0 && $ro_rc -eq 0 && $cmp_rw_rc -eq 0 && $cmp_ro_rc -eq 0 \
          && "$before" == "$after" && "$before" == *"W457/RO/nd/n.txt F "* ]]; then
        pass_row " (the probe NEX matches its recording; every write-call answer on a writable root and on a write-protected one equals NextZXOS 2.09's except the declared divergences; the write-protected tree is untouched)"
    else
        fail_row " (verify rc=$ver_rc '${ver:0:200}'; rw rc=$rw_rc compare rc=$cmp_rw_rc '$(head -n 3 <<<"$cmp_rw" | tr '\n' ' ')'; ro rc=$ro_rc compare rc=$cmp_ro_rc '$(head -n 3 <<<"$cmp_ro" | tr '\n' ' ')'; tree unchanged=$([[ "$before" == "$after" ]] && echo yes || echo no))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
