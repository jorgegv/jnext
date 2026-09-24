#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #27 S8 — the `.jns` whole-machine round trip, PIXEL-EXACT.
# Design: doc/design/NEXT-SNAPSHOT-FORMAT.md §16.2 (`snapshot-roundtrip-func`).
#
# WHY PIXELS, AND NOT THE STATE STREAM. `rewind_test`'s JNS-RT-02 already
# compares the binary state streams of the source and the restored machine and
# requires them byte-identical. That row passed while a `.jns` restored NO
# MEMORY AT ALL: the blobs were written and never read back, and a comparison
# of fields cannot see a defect in something that is not a field. The 48K leg
# below was 139 448 wrong pixels at the time. So the two tiers are not
# redundant — the field oracle catches a missing subsystem, and only a rendered
# frame catches a missing megabyte.
#
# THE WORKLOADS ARE NAMED, because a quiescent screen passes for a neighbouring
# reason (§16.2): a BASIC prompt would survive a restore that dropped the
# Copper, the per-scanline change logs and half the video registers.
#
#   48K BASIC   — the simplest thing that can be wrong, and the leg that
#                 caught the missing blobs.
#   beast.nex   — a Next with a live Copper palette gradient and per-scanline
#                 change logs, the §10.3 class. If the raster history does not
#                 travel, this is where it shows.
#
# ── THE ONE-FRAME OFFSET, WRITTEN DOWN BECAUSE IT LOOKS LIKE A BUG ────────
#
# A save ALWAYS advances to the next frame boundary first (§10.2 P7). So a
# snapshot requested at frame N is a snapshot of frame N+1, and a restored
# machine rendered for M frames matches the continuous run at frame **N+1+M**,
# not N+M. Comparing against N+M instead reports ~16 000 differing pixels on
# beast.nex — a plausible-looking failure that is the test's arithmetic being
# wrong, not the emulator. The control below pins the offset itself, so a
# future change that removed the advance would fail HERE rather than silently
# shifting every comparison by one frame.
if want snapshot-jns-roundtrip-func; then
    begin_func snapshot-jns-roundtrip-func
    if ! $HAS_COMPARE; then
        skip_row " (no ImageMagick — the whole point of this row is a pixel comparison)"
    else
    jns48="$TMP_DIR/rt48.jns";  png48a="$TMP_DIR/rt48a.png";  png48b="$TMP_DIR/rt48b.png"
    jnsnx="$TMP_DIR/rtnx.jns";  pngnxa="$TMP_DIR/rtnxa.png";  pngnxb="$TMP_DIR/rtnxb.png"
    pngoff="$TMP_DIR/rtoff.png"
    beast="$PROJECT_DIR/test/00regression/nex/beast.nex"
    rm -f "$jns48" "$png48a" "$png48b" "$jnsnx" "$pngnxa" "$pngnxb" "$pngoff"

    # ── LEG 1: 48K BASIC. Save at 150 and screenshot the SAME frame, then
    #    restore and render one frame. The prompt is static, so the restored
    #    picture must be identical with no offset arithmetic at all.
    leg48_rc=1
    if timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 \
            --delayed-screenshot "$png48a" --delayed-screenshot-frames 150 \
            --delayed-snapshot "$jns48" --delayed-snapshot-frames 150 \
            --delayed-automatic-exit 20 >/dev/null 2>&1 \
       && [[ -s "$jns48" ]] \
       && timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 --load "$jns48" \
            --delayed-screenshot "$png48b" --delayed-screenshot-frames 1 \
            --delayed-automatic-exit 20 >/dev/null 2>&1
    then leg48_rc=0; fi
    diff48=-1
    [[ -s "$png48a" && -s "$png48b" ]] && diff48=$(png_diff "$png48b" "$png48a")

    # ── LEG 2: beast.nex on a Next. Save at 300 (so the file holds frame 301),
    #    restore, render 10 frames, and require a pixel-exact match against the
    #    continuous run at frame 311.
    legnx_rc=1
    if timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 --load "$beast" \
            --delayed-snapshot "$jnsnx" --delayed-snapshot-frames 300 \
            --delayed-automatic-exit 30 >/dev/null 2>&1 \
       && [[ -s "$jnsnx" ]] \
       && timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 --load "$beast" \
            --delayed-screenshot "$pngnxa" --delayed-screenshot-frames 311 \
            --delayed-automatic-exit 30 >/dev/null 2>&1 \
       && timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 --load "$jnsnx" \
            --delayed-screenshot "$pngnxb" --delayed-screenshot-frames 10 \
            --delayed-automatic-exit 30 >/dev/null 2>&1
    then legnx_rc=0; fi
    diffnx=-1
    [[ -s "$pngnxa" && -s "$pngnxb" ]] && diffnx=$(png_diff "$pngnxb" "$pngnxa")

    # ── CONTROL: the workload really is animated, and the offset really is one
    #    frame. Without this, leg 2 would pass just as happily against a
    #    beast.nex that had stopped moving — and a comparison of two identical
    #    still frames proves nothing about a raster history.
    ctrl_rc=1
    if timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --rewind-buffer-size 0 --load "$beast" \
            --delayed-screenshot "$pngoff" --delayed-screenshot-frames 310 \
            --delayed-automatic-exit 30 >/dev/null 2>&1
    then ctrl_rc=0; fi
    diffoff=-1
    [[ -s "$pngoff" && -s "$pngnxb" ]] && diffoff=$(png_diff "$pngnxb" "$pngoff")

    if [[ "$leg48_rc" -eq 0 ]] && [[ "$diff48" -eq 0 ]] \
       && [[ "$legnx_rc" -eq 0 ]] && [[ "$diffnx" -eq 0 ]] \
       && [[ "$ctrl_rc" -eq 0 ]] && [[ "$diffoff" -gt 0 ]]; then
        pass_row " (48K restore 0 px; beast.nex on Next restore+10 == continuous 311, 0 px; and the frame-310 control differs by $diffoff px, so the workload moves and the +1 offset is real)"
    else
        fail_row " (leg48_rc=$leg48_rc diff48=$diff48 legnx_rc=$legnx_rc diffnx=$diffnx ctrl_rc=$ctrl_rc diff_vs_310=$diffoff)"
    fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
