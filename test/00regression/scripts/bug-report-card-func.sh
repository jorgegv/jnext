#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable: the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# bug-report-card-func (GH #492): --bug-report on a card session that boots NextZXOS (the warm
# start), where RAM holds NextZXOS and DivMMC firmware and must NOT ship. THE HARD RULE, end to
# end: the report carries no NextZXOS, ROM or firmware content in any form.
#
#   A  headless --load im1.nex --trace --sdcard-readonly --bug-report FILE exits 0
#   B  the manifest says memory is withheld, code card-session; no memory/ member; the trace
#      has no opcode bytes and debugger.json no disassembly or stack; the program ships
#   C  cards[0].sha256 is the SHA-256 of the run's card clone (readonly: nothing wrote to it)
#   D  THE SCAN: every file under /MACHINES/NEXT, /NEXTZXOS and /TBBLUE.FW of the clone, and
#      nextboot.rom, is extracted with mcopy into this row's scratch directory (deleted with
#      it, never kept) and no 32-byte window of them occurs at any offset of any member
if want bug-report-card-func; then
    begin_func bug-report-card-func

    br_faults=()
    for tool in mcopy sha256sum; do
        command -v "$tool" &>/dev/null || br_faults+=("tool $tool absent: install mtools")
    done
    if [[ "${#br_faults[@]}" -eq 0 ]]; then
        export MTOOLS_SKIP_CHECK=1
        check_py="$SCRIPT_DIR/bug-report-check.py"
        python3 "$SCRIPT_DIR/no-sdcard-probe.py" "$TMP_DIR/im1.nex"
        card="$RUN_DIR/sdcard/cspect-next-1gb-fixed.img"
        rep="$TMP_DIR/card-report.zip"

        a_rc=0
        a_out=$(timeout --foreground --kill-after=5s 180s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --sdcard-readonly --rtc "$NEXTZXOS_RTC" --load "$TMP_DIR/im1.nex" --trace \
            --bug-report "$rep" --delayed-automatic-exit-frames 300 2>&1) || a_rc=$?
        [[ "$a_rc" -eq 0 ]] || br_faults+=("A: exit status $a_rc, want 0")

        if [[ -s "$rep" ]]; then
            chk=$(python3 -I "$check_py" "$rep" \
                --manifest memory.included false --manifest memory.withheld.code card-session \
                --manifest programs.0.included true --lacks-prefix memory/ \
                --has program/im1.nex --equals program/im1.nex "$TMP_DIR/im1.nex" \
                --absent-key debugger.json disasm_at_pc --absent-key debugger.json stack \
                --absent-key debugger.json trace.entries.0.opcode_bytes \
                --manifest cards.0.sha256 "$(sha256sum "$card" | cut -d' ' -f1)" 2>&1) || br_faults+=("B/C: $chk")

            # D: extract the firmware of the clone into the scratch directory and scan.
            fw="$TMP_DIR/fw"
            mkdir -p "$fw/machines" "$fw/nextzxos" "$fw/boot"
            off=32256
            mcopy -s -n -i "$card@@$off" "::/MACHINES/NEXT" "$fw/machines/" 2>/dev/null || br_faults+=("D: mcopy of /MACHINES/NEXT failed")
            mcopy -s -n -i "$card@@$off" "::/NEXTZXOS" "$fw/nextzxos/" 2>/dev/null || br_faults+=("D: mcopy of /NEXTZXOS failed")
            mcopy -n -i "$card@@$off" "::/TBBLUE.FW" "$fw/boot/" 2>/dev/null || br_faults+=("D: mcopy of /TBBLUE.FW failed")
            cp "$PROJECT_DIR/roms/nextboot.rom" "$fw/boot/nextboot.rom"
            scan=$(python3 -I "$check_py" "$rep" --scan "$fw/machines" --scan "$fw/nextzxos" --scan "$fw/boot" 2>&1) \
                || br_faults+=("D: $scan")
            rm -rf "$fw"
        else
            br_faults+=("B: no report file written")
        fi
    fi

    if [[ "${#br_faults[@]}" -eq 0 ]]; then
        pass_row " (card session: memory withheld, trace without opcode bytes, program shipped, card named by its SHA-256; no 32-byte window of the card's firmware or nextboot.rom in any member)"
    else
        fail_row " (${br_faults[*]})"
        show_output "jnext" "${a_out:-}"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
