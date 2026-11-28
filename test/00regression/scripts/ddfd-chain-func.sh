#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable - the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #584: a run of DD/FD prefix bytes was walked inside ONE execute() call, so 64 KB of DD (RAM over
# the whole address space) never returned: the frame loop never advanced and --delayed-automatic-exit
# never fired. On the T80N a DD/FD is a 4 T instruction of its own (t80n_mcode.vhd:231-235) and only
# reset stops such a run (t80n.vhd:1765, :1768: no INT, no NMI after a prefix M1), but the machine around
# the CPU keeps running, so the emulator must come back and exit.
#
# The snapshot is built here, synthetic (no SD content): a +3 .z80 v3 with 1FFD = 1 (special paging, banks
# 0-3 over the 64 KB), every byte 0xDD, PC 0, interrupts enabled. PASS = the run exits 0 inside the bound
# and the screenshot exists; the unfixed emulator is killed by the timeout.
if want ddfd-chain-func; then
    begin_func ddfd-chain-func
    z80="$TMP_DIR/alldd.z80"; png="$TMP_DIR/alldd.png"; log="$TMP_DIR/alldd.log"
    zeros() { head -c "$1" /dev/zero; }
    {
        # 30-byte header: A F BC HL, PC = 0 (a v2/v3 file), SP = 0x8000, I = 0x3F, R, flags, the rest zero,
        # IFF1 = IFF2 = 1, IM 1.
        zeros 8; printf '\x00\x80\x3f'; zeros 16; printf '\x01\x01\x01'
        # v3 extension (55 bytes): PC = 0, hardware 7 (+3), 7FFD = 0, 1FFD = 1 (special paging, mode 0).
        printf '\x37\x00'; zeros 2; printf '\x07'; zeros 51; printf '\x01'
        # Eight 16 KB pages (page = bank + 3), uncompressed (length 0xFFFF), every byte 0xDD.
        for page in 03 04 05 06 07 08 09 0a; do
            printf '\xff\xff'; printf "\\x$page"
            zeros 16384 | tr '\0' '\335'
        done
    } >"$z80"
    rm -f "$png"
    rc=0
    timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine plus3 --load "$z80" \
        --delayed-screenshot "$png" --delayed-screenshot-time 1 --delayed-automatic-exit 2 \
        >"$log" 2>&1 || rc=$?
    if [[ "$rc" -ne 0 ]]; then
        fail_row " (64 KB of DD: exit status $rc, 124/137 = it never came back: $(grep -m1 -E '\[(error|critical)\]' "$log" || echo 'no error logged'))"
    elif [[ ! -s "$png" ]]; then
        fail_row " (64 KB of DD: the run exited 0 but wrote no screenshot)"
    else
        pass_row " (64 KB of DD runs to its automatic exit and writes the screenshot)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
