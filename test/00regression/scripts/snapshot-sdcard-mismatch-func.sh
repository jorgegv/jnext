#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #27 S7 — the `.jns` snapshot's two-tier SD identity, end to end.
# Design: doc/design/NEXT-SNAPSHOT-FORMAT.md §11.3, §16.2.
#
# WHAT A SEPARATE PROCESS ADDS over the unit rows. `sd_identity_test`'s
# JNSI-P* rows prove the producer and the reader agree in-process. This row
# proves the part only a process can show: a Tier-1 mismatch EXITS NON-ZERO and
# a Tier-2 drift does NOT — the difference between "your save will not load"
# and "it loaded, here is what changed", which is what a user and a script both
# act on. §16.2 names exactly that ("assert the Tier-1 refusal and a non-zero
# exit"; "the run proceeds").
#
# WHY IT DRIVES `sd_identity_test --verdict` AND NOT `jnext --load out.jns`.
# The `.jns` CLI does not exist until S8 — the same situation S6's
# snapshot-paused-advance-func was in, and handled the same way. `--verdict` is
# not a test double: it calls `describe_sdcard_for_snapshot`, writes a real
# `.jns` through `SnapshotWriter`, and opens it through `open_snapshot`. It is
# the shipped code with a `main()` in front of it, and S8 will replace the
# front without touching the legs below.
#
# WHY THE MUTATED CARDS ARE SMALL IMAGES AND LEG 0 IS THE REAL ONE. Three
# mutated copies of the 1 GB card would cost 3 GB per regression run wherever
# reflink is unavailable — CI included — and buy nothing: a Tier-1 field is the
# same 66 + 4 + 11 bytes whatever the image's size. So the mutation legs use a
# real MBR + FAT32 image this suite builds (the binary emits it AND the byte
# offsets, so no copy of the FAT32 layout lives in this file), and leg 0 runs
# the real per-run NextZXOS card — read-only, no copy — so the row still proves
# the actual card yields a usable identity.
if want snapshot-sdcard-mismatch-func; then
    begin_func snapshot-sdcard-mismatch-func
    SDID="$PROJECT_DIR/build/test/sd_identity_test"
    base="$TMP_DIR/sdid-base.img"
    mutant="$TMP_DIR/sdid-mutant.img"
    real_card="$RUN_DIR/sdcard/cspect-next-1gb-fixed.img"

    # A missing binary is a LOUD failure, never a silent skip: `make clean`
    # deletes it, and a row that quietly disappears is the Task 35 defect.
    if [[ ! -x "$SDID" ]]; then
        fail_row " (build/test/sd_identity_test missing — run 'make unit-test-build')"
    else
    rm -f "$base" "$mutant"
    mk=$("$SDID" --make-image "$base" 2>&1) || mk="FAILED: $mk"
    off_data=$(  printf '%s\n' "$mk" | sed -n 's/^OFFSET_DATA //p')
    off_volid=$( printf '%s\n' "$mk" | sed -n 's/^OFFSET_VOLID //p')
    off_vollab=$(printf '%s\n' "$mk" | sed -n 's/^OFFSET_VOLLAB //p')

    # sdid_poke <offset> <byte-as-printf-escape> — rebuild the mutant from the
    # pristine base every time, so each leg differs from the base in exactly
    # one byte and a leg cannot inherit the previous leg's mutation.
    sdid_poke() {
        cp "$base" "$mutant"
        printf "$2" | dd of="$mutant" bs=1 seek="$1" conv=notrunc status=none
    }

    # ── LEG 0: the REAL card against itself — silent, exit 0 ──────────────
    leg0_rc=1; leg0_out=""
    if [[ -f "$real_card" ]]; then
        if leg0_out=$(timeout --foreground --kill-after=5s 120s \
                        "$SDID" --verdict "$real_card" "$real_card" 2>/dev/null)
        then leg0_rc=0; fi
    fi
    leg0_ok=0
    [[ "$leg0_rc" -eq 0 ]] \
        && grep -q '^VERDICT: ok$' <<<"$leg0_out" \
        && ! grep -q '^WARNING:' <<<"$leg0_out" \
        && ! grep -q '^REFUSAL:' <<<"$leg0_out" && leg0_ok=1

    # ── LEG 1: a mutated DATA SECTOR — Tier 2 warns, the run PROCEEDS ─────
    sdid_poke "$off_data" '\x5a'
    leg1_rc=1
    if leg1_out=$(timeout --foreground --kill-after=5s 60s \
                    "$SDID" --verdict "$base" "$mutant" 2>/dev/null)
    then leg1_rc=0; fi
    leg1_ok=0
    [[ "$leg1_rc" -eq 0 ]] \
        && grep -q '^VERDICT: ok$' <<<"$leg1_out" \
        && grep -q "^WARNING: the SD card's contents changed since the snapshot" <<<"$leg1_out" \
        && ! grep -q '^REFUSAL:' <<<"$leg1_out" && leg1_ok=1

    # ── LEG 1b: the SAME drift MID-TRANSFER — refusal, non-zero exit ──────
    # §11.3's last row. The strictness is earned exactly here: a half-finished
    # sector read against changed bytes is the "streams garbage" failure.
    leg1b_rc=0
    if ! leg1b_out=$(timeout --foreground --kill-after=5s 60s \
                       "$SDID" --verdict "$base" "$mutant" --mid-transfer 2>/dev/null)
    then leg1b_rc=1; fi
    leg1b_ok=0
    [[ "$leg1b_rc" -eq 1 ]] \
        && grep -q '^VERDICT: refused$' <<<"$leg1b_out" \
        && grep -q 'mid-transfer' <<<"$leg1b_out" && leg1b_ok=1

    # ── LEG 2: a mutated BS_VolID — Tier 1 REFUSES, exit NON-ZERO ─────────
    sdid_poke "$off_volid" '\x99'
    leg2_rc=0
    if ! leg2_out=$(timeout --foreground --kill-after=5s 60s \
                      "$SDID" --verdict "$base" "$mutant" 2>/dev/null)
    then leg2_rc=1; fi
    leg2_ok=0
    [[ "$leg2_rc" -eq 1 ]] \
        && grep -q '^VERDICT: refused$' <<<"$leg2_out" \
        && grep -q 'not the one the snapshot was taken on' <<<"$leg2_out" \
        && grep -q '1a2b3c4d' <<<"$leg2_out" \
        && grep -q '1a2b3c99' <<<"$leg2_out" && leg2_ok=1

    # ── LEG 2b: …and --snapshot-force-sdcard downgrades it to a warning ───
    leg2b_rc=1
    if leg2b_out=$(timeout --foreground --kill-after=5s 60s \
                     "$SDID" --verdict "$base" "$mutant" --force 2>/dev/null)
    then leg2b_rc=0; fi
    leg2b_ok=0
    [[ "$leg2b_rc" -eq 0 ]] \
        && grep -q '^VERDICT: ok$' <<<"$leg2b_out" \
        && grep -q '^WARNING:.*--snapshot-force-sdcard.*1a2b3c4d.*1a2b3c99' <<<"$leg2b_out" \
        && leg2b_ok=1

    # ── LEG 3: a mutated BS_VolLab — NO refusal, NO label warning ─────────
    # The label is never compared (§11.3): it is a stale copy of the
    # authoritative root-directory entry, and several tools rewrite it, so a
    # refusal here would fire on the same physical card.
    #
    # The Tier-2 drift warning IS expected and IS true — BS_VolLab is a byte of
    # the image, so the whole-image digest moves with it. §16.2's "assert no
    # warning" could never hold for an on-disk mutation; what holds, and what
    # this leg asserts, is that NOTHING mentions the label or the identity.
    # The design doc carries that correction (§11.3 / §16.2, S7 append).
    sdid_poke "$off_vollab" '\x58'
    leg3_rc=1
    if leg3_out=$(timeout --foreground --kill-after=5s 60s \
                    "$SDID" --verdict "$base" "$mutant" 2>/dev/null)
    then leg3_rc=0; fi
    leg3_ok=0
    [[ "$leg3_rc" -eq 0 ]] \
        && grep -q '^VERDICT: ok$' <<<"$leg3_out" \
        && ! grep -q '^REFUSAL:' <<<"$leg3_out" \
        && ! grep -qi 'label' <<<"$leg3_out" \
        && ! grep -q 'not the one' <<<"$leg3_out" && leg3_ok=1

    rm -f "$base" "$mutant"
    # A row is SOURCED into the harness shell, so anything it defines outlives
    # it. Take the helper back out rather than leaving a `poke` in scope for
    # every later row (GH #153 is the same lesson about `trap`).
    unset -f sdid_poke

    if [[ "$leg0_ok" -eq 1 && "$leg1_ok" -eq 1 && "$leg1b_ok" -eq 1 \
       && "$leg2_ok" -eq 1 && "$leg2b_ok" -eq 1 && "$leg3_ok" -eq 1 ]]; then
        pass_row " (real card silent; data drift warns + exit 0; drift mid-transfer refuses; VolID refuses + exit!=0, --force downgrades; VolLab neither refuses nor mentions the label)"
    else
        fail_row " (leg0=$leg0_ok leg1=$leg1_ok leg1b=$leg1b_ok leg2=$leg2_ok leg2b=$leg2b_ok leg3=$leg3_ok; real_card=$([[ -f "$real_card" ]] && echo y || echo n))"
    fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
