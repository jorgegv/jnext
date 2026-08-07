#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #226 — soft-resetting out of a SHADOW-SCREEN program must not leave the
# last game frame frozen on screen.
#
# WHAT MAKES THIS ROW DIFFERENT FROM soft-reset-to-nextzxos-func, which it
# otherwise closely resembles. That row resets out of the NextZXOS main menu,
# which draws in bank 5, so it can never observe the defect. This one runs a
# program that has switched the ULA to bank 7 through port 0x7FFD bit 3 first.
#
# In VHDL bit 3 is ONE wire: reset clears it (zxnext.vhd:3646-3648), it becomes
# port_7ffd_shadow (:3768), and it enters the ULA as the single i_ula_shadow_en
# (:4453) that drives both the standard-mode force (zxula.vhd:191) AND the
# bank-5/bank-7 fetch select (zxula.vhd:210,267 -> zxnext.vhd:6651-6654). jnext
# mirrors that one wire in two members, and Ula::reset() used to clear only the
# first — so after the reset the ULA kept fetching the now-dead bank-7 buffer
# while the rebooted OS painted bank 5. Measured on this exact run: 168476 px
# of difference before the fix, 0 after.
#
# WHY THE BROWSER AND NOT `--load beast.nex`. The CLI path cannot express this
# assertion. A --load session never runs tbblue.fw, so there is no NextZXOS ROM
# to return to and F4 lands in 48K BASIC (documented in jnext.1). Reaching the
# program through NextZXOS's own file browser is what makes the post-reset
# screen the welcome screen, which is why this row needs NO new reference PNG:
# it reuses boot-nextzxos-welcome-reference.png, the same image four other rows
# already compare against.
#
# It also means F4 has to actually work, so this row covers the SECOND half of
# #226 for free: on a firmware-less boot the stuck nr_03_config_mode swallowed
# F4 at the VHDL:6370 gate entirely. `JNEXT_DELAYED_RESET_TYPE=f4`, never
# "soft": "soft" bypasses both the gate and the reset_type FSM, so a row
# written against it would pass on a build where the real GUI path is dead —
# which is exactly how this class went uncaught.
#
# NO mtools, NO new dependency, and the MASTER IMAGE IS NEVER TOUCHED:
# `sdfile_tool` puts beast.nex into /demos on THIS RUN'S private clone through
# the in-tree FAT32 writer (src/core/fat32_image.h), the same
# read_tree/upsert/format_and_populate sequence nextsync-func uses for its dot
# command and sdcard_provisioner.cpp uses for MACHINES/NEXT/config.ini. The
# pristine image ships no .nex under /demos, and adding one there is invisible
# to every other row: the only row that reads a directory listing
# (boot-nextzxos-dotls) is a SCREENSHOT row, so it has already run, and it
# lists the ROOT, which gains nothing.
#
# THE KEY SCHEDULE. 400 space skips the welcome tour; 500 enter opens the
# Browser; 730/745 down + 760 enter descends into /demos; 790/805/820 down +
# 835 enter runs beast.nex. 1300 is ~130 frames of beast in shadow mode, and
# 1800 is ~500 frames after the reset — the same settling budget
# soft-reset-to-nextzxos-func gives its capture.
if want soft-reset-shadow-screen-func; then
    begin_func soft-reset-shadow-screen-func

    shsc_png="$TMP_DIR/jnext_test_soft_reset_shadow_screen.png"
    sdfile="$PROJECT_DIR/build/test/sdfile_tool"
    sd_image="$RUN_DIR/sdcard/cspect-next-1gb-fixed.img"
    beast_nex="$PROJECT_DIR/test/00regression/nex/beast.nex"
    inject_log="$TMP_DIR/shadow_reset_inject.log"
    verdict=""
    rm -f "$shsc_png" "$inject_log"

    # Preconditions, with the same pass/skip/fail split nextsync-func uses: a
    # missing SD clone is a SKIP (the provision row will have said why), while
    # a missing sdfile_tool is a FAILURE, because `make regression` depends on
    # unit-test-build and its absence means the build is broken — the
    # rewind_test precedent, never a silently absent row.
    if [[ ! -f "$sd_image" ]]; then
        skip_row " (no SD-card image clone for this run)"
        verdict=settled
    elif [[ ! -x "$sdfile" ]]; then
        fail_row " (build/test/sdfile_tool missing — run 'make unit-test-build')"
        verdict=settled
    elif [[ ! -f "$beast_nex" ]]; then
        fail_row " (test/00regression/nex/beast.nex missing)"
        verdict=settled
    fi

    if [[ -z "$verdict" ]]; then
        if ! "$sdfile" put "$sd_image" demos/beast.nex "$beast_nex" \
                >"$inject_log" 2>&1; then
            fail_row " (could not inject beast.nex into the SD clone: $(tr -d '\n' <"$inject_log"))"
            verdict=settled
        fi
    fi

    if [[ -z "$verdict" ]]; then
        JNEXT_DELAYED_RESET_FRAMES=1300 JNEXT_DELAYED_RESET_TYPE=f4 \
        timeout --foreground --kill-after=5s 300s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --rtc "$NEXTZXOS_RTC" \
            --delayed-keypress-frames 400 space \
            --delayed-keypress-frames 500 enter \
            --delayed-keypress-frames 730 down \
            --delayed-keypress-frames 745 down \
            --delayed-keypress-frames 760 enter \
            --delayed-keypress-frames 790 down \
            --delayed-keypress-frames 805 down \
            --delayed-keypress-frames 820 down \
            --delayed-keypress-frames 835 enter \
            --delayed-screenshot "$shsc_png" --delayed-screenshot-frames 1800 \
            --delayed-automatic-exit-frames 1820 >/dev/null 2>&1 || true

        shsc_diff=999999
        if [[ -f "$shsc_png" ]]; then
            shsc_diff=$(png_diff "$shsc_png" "$WELCOME_REF")
        fi
        if [[ "$shsc_diff" -le "$TOLERANCE" ]]; then
            pass_row " (soft reset from a shadow-screen program returned to NextZXOS: ${shsc_diff} px diff vs welcome)"
        else
            fail_row " (px_diff=$shsc_diff — the ULA is still fetching bank 7 after reset, or F4 was swallowed [GH #226])"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
