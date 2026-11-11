#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# esxdos-hostfs-func (GH #31): a REAL guest program reading a REAL host file
# through RST $08, end to end — NEX loaded from the CLI, esxDOS F_OPEN /
# F_SEEK / F_READ / F_CLOSE serviced by --esxdos-stub-root, and the bytes the
# guest got printed back out through the magic port so the row can compare
# them to what is actually on disk.
#
# WHY THE SEEK MATTERS. The guest seeks to offset 6 before reading, and the
# expected string is the SLICE at 6..11 of a 16-byte file, not its start. A
# F_SEEK that silently did nothing (which is what the stub did before this
# change — it was hardcoded to fail with A=5) would print "012345" instead,
# and the row fails. The assertion is the file's content at an offset, which
# nothing but a working open+seek+read can produce.
#
# DISCRIMINATIVE BOTH WAYS. The identical NEX is run a second time with
# --esxdos-stub and no --esxdos-stub-root. Its implicit root is the NEX's own
# directory ($TMP_DIR), where data.txt is absent. That run MUST NOT print the
# file content: F_OPEN fails and the program prints ERR. So a
# pass proves the content came from the directory this flag named, rather than
# from the NEX, the SD card, or anywhere else.
#
# THE FILESPEC IS DRIVE-QUALIFIED ("c:/data.txt"), not a bare name. That form
# is what real esxDOS software writes — asm_esx_f_open.asm documents "A=drive
# specifier (overridden if filespec includes a drive)" and NextZXOS's own ROM
# carries literals like "c:/nextzxos/autoexec.1st" — and it is the form that
# exposed a marshalling helper truncating the filespec at ':' (GH #31 review).
# A unit row cannot stand in for this one: the bug lived between guest memory
# and the sandbox, so only a real guest handing over a real filespec crosses it.
#
# THIRD ASSERTION — the sandbox, from inside the guest. After reading its
# file, the program tries to open "../escape.txt", which really does exist one
# level above the root. It must be refused (Fc=1), and the guest says which
# way it went: SANDBOX-OK or SANDBOX-ESCAPED. A path escape is a security
# defect, so it is asserted by the program that would exploit it, not only by
# a unit row calling the class.
#
# The NEX and the host files are synthesised here; nothing binary in git.
if want esxdos-hostfs-func; then
    begin_func esxdos-hostfs-func

    hf_root="$TMP_DIR/hostfs-root"
    hf_nex="$TMP_DIR/hostfs.nex"
    mkdir -p "$hf_root"
    printf '0123456789ABCDEF' > "$hf_root/data.txt"
    printf 'SECRET' > "$TMP_DIR/escape.txt"

    python3 "$SCRIPT_DIR/esxdos-hostfs-probe.py" "$hf_nex"

    hf_rc=0
    hf_out=$(timeout --foreground --kill-after=5s 60s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --machine next \
        --magic-port 0xCAFE --magic-port-mode line \
        --esxdos-stub-root "$hf_root" --load "$hf_nex" \
        --delayed-automatic-exit-frames 120 2>&1) || hf_rc=$?

    # Control: the same NEX, implicit root $TMP_DIR (no data.txt there).
    # Nothing may hand it the file.
    hf_ctl=$(timeout --foreground --kill-after=5s 60s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --machine next \
        --magic-port 0xCAFE --magic-port-mode line \
        --esxdos-stub --load "$hf_nex" \
        --delayed-automatic-exit-frames 120 2>&1) || true

    # FOURTH ASSERTION — a root that cannot be served is FATAL. Parsing the flag
    # also turns the stub on, so continuing would quietly serve the in-memory
    # file instead and a typo in a CI script would go green with the feature
    # silently off. Same contract --rzx-record's start-up check has.
    bad_rc=0
    bad_out=$("$JNEXT" --headless --machine 48k \
        --esxdos-stub-root "$TMP_DIR/definitely-not-here" \
        --delayed-automatic-exit-frames 5 2>&1) || bad_rc=$?
    bad_msg=$(echo "$bad_out" | grep -cF -- "--esxdos-stub-root:" || true)

    # "6789AB" is bytes 6..11 of the 16-byte host file — only a working
    # F_OPEN + F_SEEK + F_READ produces it.
    hf_slice=$(echo "$hf_out" | grep -cxF "6789AB" || true)
    hf_sandbox=$(echo "$hf_out" | grep -cxF "SANDBOX-OK" || true)
    hf_escaped=$(echo "$hf_out" | grep -cxF "SANDBOX-ESCAPED" || true)
    hf_err=$(echo "$hf_out" | grep -cxF "ERR" || true)
    ctl_slice=$(echo "$hf_ctl" | grep -cxF "6789AB" || true)
    ctl_err=$(echo "$hf_ctl" | grep -cxF "ERR" || true)

    if [[ "$hf_rc" -eq 0 && "$hf_slice" -ge 1 && "$hf_sandbox" -ge 1 \
          && "$hf_escaped" -eq 0 && "$hf_err" -eq 0 \
          && "$ctl_slice" -eq 0 && "$ctl_err" -ge 1 \
          && "$bad_rc" -ne 0 && "$bad_msg" -ge 1 ]]; then
        pass_row " (drive-qualified c:/data.txt read at offset 6 via RST \$08; escape refused; no root = no content; bad root fatal)"
    else
        fail_row " (rc=$hf_rc want0, slice=$hf_slice want>=1, sandbox_ok=$hf_sandbox want>=1, escaped=$hf_escaped want0, err=$hf_err want0, ctl_slice=$ctl_slice want0, ctl_err=$ctl_err want>=1, bad_rc=$bad_rc want!=0, bad_msg=$bad_msg want>=1)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
