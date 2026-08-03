#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# --delayed-nmi / --delayed-nmi-frames: press a hardware NMI button headlessly
# so NMI handlers can be tested (GH #209).
#
# WHAT MAKES THIS DISCRIMINATIVE. The observable is the CPU actually executing
# the NMI vector at 0x0066, read from the instruction trace — not a log line
# saying a button was pressed, which would still be printed by an
# implementation that dispatched the press into a dead end. bin/nmi_enable.bin
# (source: bin/nmi_enable.asm) opens the NextREG 0x06 gates and parks the CPU
# in `jr $` at 0x800E, so 0x0066 is UNREACHABLE unless an NMI is taken.
#
# Four claims, each with the control that makes it mean something:
#
#   fires    press divmmc -> 0x0066 executed.
#   control  identical run WITHOUT the press -> 0x0066 never executed. Without
#            this, a machine that happened to take an NMI on its own would pass.
#   gated    press with NO fixture injected (NR 0x06 bits clear, as NextZXOS
#            leaves them) -> 0x0066 never executed. The press must be IGNORED
#            when its enable gate is closed, exactly as on hardware
#            (zxnext.vhd:2090-2091) — a press that fired regardless would be a
#            defect, not a feature, and this is the row that catches it.
#   rejects  an unknown button name is refused loudly and exits non-zero,
#            never silently dropped (same contract as --delayed-keypress).
#
# The inject lands at frame 450, AFTER NextZXOS has finished booting: while the
# firmware still holds NR 0x03 config_mode the NMI latches are force-cleared
# (zxnext.vhd:2101) and no press of either button can produce an NMI. That is
# correct hardware behaviour, and it is why this test boots first rather than
# injecting at frame 0.
if want delayed-nmi-func; then
    begin_func delayed-nmi-func
    nmi_bin="$SCRIPT_DIR/bin/nmi_enable.bin"
    common=(--headless --machine next "${SD_CARD_ARGS[@]}"
            --rtc 2026-07-10T08:55:00
            --delayed-automatic-exit-frames 540 --log-level warn)
    inject=(--inject "$nmi_bin" --inject-org 8000 --inject-delay 450)

    # Count executions of the NMI vector in the traced window. The trace is
    # per-instruction, so a handler entered even once is visible.
    #
    # The window is ONE frame wide and is the frame the press is scheduled for,
    # which makes it the frame-exactness assertion as well: measured, the
    # handler runs inside frame 500 and a window at 501 sees nothing, so an
    # implementation that fired a frame late would score fire=0 here rather
    # than passing on a loose bracket.
    count_0066() {
        local csv="$1"
        [[ -s "$csv" ]] || { echo 0; return; }
        cut -d, -f2 "$csv" | grep -cx '0066' || true
    }

    fire_csv="$TMP_DIR/nmi-fire.csv"
    ctrl_csv="$TMP_DIR/nmi-ctrl.csv"
    gate_csv="$TMP_DIR/nmi-gate.csv"

    # 1. Gate open + press -> the handler runs.
    JNEXT_G46B_ITRACE="$fire_csv" JNEXT_G46B_ITRACE_START=500 JNEXT_G46B_ITRACE_FRAMES=1 \
    timeout --foreground --kill-after=5s 180s "$JNEXT" "${common[@]}" "${inject[@]}" \
        --delayed-nmi-frames 500 divmmc >/dev/null 2>&1 || true
    fire=$(count_0066 "$fire_csv")

    # 2. Same run, no press at all.
    JNEXT_G46B_ITRACE="$ctrl_csv" JNEXT_G46B_ITRACE_START=500 JNEXT_G46B_ITRACE_FRAMES=1 \
    timeout --foreground --kill-after=5s 180s "$JNEXT" "${common[@]}" "${inject[@]}" \
        >/dev/null 2>&1 || true
    ctrl=$(count_0066 "$ctrl_csv")

    # 3. Press with the NR 0x06 gate closed (no fixture) -> ignored.
    JNEXT_G46B_ITRACE="$gate_csv" JNEXT_G46B_ITRACE_START=500 JNEXT_G46B_ITRACE_FRAMES=1 \
    timeout --foreground --kill-after=5s 180s "$JNEXT" "${common[@]}" \
        --delayed-nmi-frames 500 divmmc >/dev/null 2>&1 || true
    gate=$(count_0066 "$gate_csv")

    # 4. An unknown button name must be refused, not dropped.
    if bad_out=$(timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --delayed-nmi-frames 10 sideways \
            --delayed-automatic-exit-frames 20 2>&1); then
        bad_rc=0
    else
        bad_rc=1
    fi

    if [[ "$fire" -ge 1 ]] && [[ "$ctrl" -eq 0 ]] && [[ "$gate" -eq 0 ]] \
       && [[ "$bad_rc" -ne 0 ]] \
       && grep -q "Unknown --delayed-nmi button name" <<<"$bad_out"; then
        pass_row " (press -> NMI vector 0x0066 run; absent without press; ignored when NR 0x06 gate closed; bad name refused)"
    else
        fail_row " (fire=$fire want>=1, control=$ctrl want 0, gated=$gate want 0, badname_rc=$bad_rc want!=0)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
