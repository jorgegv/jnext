#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #77 — `--sdcard-readonly` opens the SD image read-only so a run cannot
# mutate an image other runs share.
#
# WHY THIS ROW EXISTS AT ALL. sdcard_test's SD-RO-01/02 already prove that
# `SdCardDevice::mount(path, read_only=true)` rejects CMD24 with the 0x0D
# write-error token and leaves the host file untouched. What they cannot prove
# is that the CLI flag ever REACHES that parameter. Review demonstrated the gap
# rather than supposing it: reverting `emulator.cpp`'s call to the one-argument
# `mount(cfg.sd_card_image)` — a plausible refactor slip — left the flag a
# silent no-op with sdcard_test 40/40, cli_options_test 13/13 and the entire
# triplet green.
#
# WHY IT DOES NOT COMPARE THE IMAGE AFTERWARDS. The obvious assertion — boot
# with the flag, check the image is unchanged — proves NOTHING here, and this
# suite has been fooled by exactly that shape before (see the header of
# sdcard-isolation-func.sh, and the sdcard-provision row whose inverted hash
# gate stayed green because the rewrite was byte-identical). Booting NextZXOS
# to the welcome screen does not write to the card at all: measured, the image
# is byte-identical after a normal read-write boot too. So an
# image-unchanged assertion passes identically whether the flag works or is
# ignored entirely.
#
# So this row asserts what jnext DID — which open() path it took — because that
# is the thing that differs. `mount()` logs a distinct line for the deliberate
# read-only open ("by request", info) versus the accidental fallback for an
# unwritable file ("SD image opened read-only:", warn), and neither appears on
# a normal read-write mount.
if want sdcard-readonly-func; then
    begin_func sdcard-readonly-func

    W="$TMP_DIR/sdro.$$"
    mkdir -p "$W"
    faults=()

    # --- A: with the flag, jnext takes the DELIBERATE read-only path.
    timeout 60 "$JNEXT" --headless --machine next --rtc "$NEXTZXOS_RTC" \
        --sdcard-readonly --delayed-automatic-exit 3 > "$W/ro.log" 2>&1 || true
    grep -qF "SD image opened read-only by request" "$W/ro.log" \
        || faults+=("A: --sdcard-readonly did not open the image read-only (flag not wired to mount())")

    # --- B: and it is the deliberate path, not the unwritable-file fallback.
    # A fallback here would mean the image happened to be unwritable, which
    # would make row A pass for the wrong reason on a bad fixture.
    grep -qE '\[warn\].*SD image opened read-only:' "$W/ro.log" \
        && faults+=("B: the image was opened read-only by FALLBACK (unwritable file), not by request")

    # --- C: negative control. Without the flag, neither read-only line
    # appears. Without this, row A would still pass if mount() ignored the
    # parameter and always opened read-only — which is a real bug (it would
    # break every legitimate write) and is caught in sdcard_test by SD-14 and
    # four others, but this row should not depend on that.
    timeout 60 "$JNEXT" --headless --machine next --rtc "$NEXTZXOS_RTC" \
        --delayed-automatic-exit 3 > "$W/rw.log" 2>&1 || true
    grep -qi "opened read-only" "$W/rw.log" \
        && faults+=("C: a run WITHOUT --sdcard-readonly still opened the image read-only")

    # --- D: the flag does not break the boot. The screenshot rows all boot
    # read-write, so nothing else in this suite would notice if a
    # write-protected card stalled NextZXOS. Compare against the same welcome
    # reference the suite uses, so "boots identically" is measured, not assumed.
    # Frame-exact, matching the boot-nextzxos-welcome row in
    # regression_tests.conf (400 frames, same --rtc). A seconds-based capture
    # lands on a different frame and diffs against the reference for reasons
    # that have nothing to do with this flag — measured, it does.
    timeout 90 "$JNEXT" --headless --machine next --rtc "$NEXTZXOS_RTC" \
        --sdcard-readonly --delayed-screenshot "$W/welcome.png" \
        --delayed-screenshot-frames 400 --delayed-automatic-exit 21 \
        > "$W/shot.log" 2>&1 || true
    if [[ ! -s "$W/welcome.png" ]]; then
        faults+=("D: no screenshot written — a read-only card broke the boot")
    else
        # Only "== 0" is asserted: png_diff returns a scaled error sum, not a
        # pixel count (GH #76), so a nonzero magnitude is not interpretable —
        # but zero still means identical.
        diff_px=$(png_diff "$W/welcome.png" "$WELCOME_REF")
        [[ "$diff_px" == "0" ]] \
            || faults+=("D: read-only boot differs from the welcome reference (png_diff=$diff_px)")
    fi

    rm -rf "$W"

    if [[ ${#faults[@]} -eq 0 ]]; then
        pass_row " (flag reaches mount(); deliberate RO not fallback; absent without the flag; boots identically)"
    else
        fail_row " (${#faults[@]} fault(s) in --sdcard-readonly wiring)"
        printf '      %s\n' "${faults[@]}"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
