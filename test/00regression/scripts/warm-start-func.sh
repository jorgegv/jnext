#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #234 — the warm start, end to end. This is the half the unit suite
# (test/warm_start/warm_start_test.cpp) cannot reach: taking a recording needs
# a real SD image with firmware on it, so record -> cache -> restore ->
# invalidate only exists here.
#
# Four things are asserted, in one run of each kind:
#
#   A. RECORD   a cold cache cold-boots the firmware and ends with a machine
#               that passes Emulator::nextzxos_resident(). This is the whole
#               mechanism: if the boot silently does nothing, the residency
#               check refuses and the log says so (it did, on the first
#               implementation — re-init()ing the live emulator is not a cold
#               boot, so config mode stayed clear and TBBLUE.FW's ROM
#               streaming never reached the ROM area).
#   B. RESTORE  the second run uses the cache and does NOT boot again. A warm
#               start that re-boots every time is not broken, just slow —
#               which is exactly the kind of regression nothing else notices.
#   C. INVALIDATE  a cache whose recorded SD digest no longer matches is
#               REFUSED and re-recorded. A stale recording silently served to
#               the wrong machine is the worst failure this feature can have,
#               because the machine it produces LOOKS booted.
#   D. RENDER   a warm-started NEX still renders what the committed reference
#               says. tilemap-demo is the row chosen deliberately: it writes
#               its tilemap through $6000-$7FFF, and before NexLoader::apply()
#               established MMU2-5 (nexload.asm:280-283) a warm start sent
#               those writes into Layer 2's bank 8 and the frame came out
#               BLACK. The reference is NOT regenerated — it is the existing,
#               committed, cold-path one, and the assertion is that the warm
#               path lands on it byte for byte.
if want warm-start-func; then
    begin_func warm-start-func

    ws_faults=()
    ws_cache_dir="${JNEXT_CONFIG_DIR:-$HOME/.jnext}/warm-start"
    ws_nex="test/00regression/nex/tilemap_demo.nex"
    ws_ref="test/00regression/img/tilemap-demo-reference.png"
    ws_shot="$TMP_DIR/warm-start-tilemap.png"

    # A — cold cache: must boot and record.
    rm -rf "$ws_cache_dir"
    ws_a=$(timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --rtc "$NEXTZXOS_RTC" --warm-start --load "$ws_nex" \
        --delayed-automatic-exit-frames 10 2>&1 || true)
    echo "$ws_a" | grep -q "cold-booting the firmware" \
        || ws_faults+=("A: a cold cache did not boot the firmware")
    echo "$ws_a" | grep -q "recorded a NextZXOS-resident machine" \
        || ws_faults+=("A: the boot did not produce a NextZXOS-resident machine")
    echo "$ws_a" | grep -q "NextZXOS is resident; the program is applied on top of it" \
        || ws_faults+=("A: the restored machine failed the residency re-check")

    ws_file=$(ls "$ws_cache_dir"/*.jwss 2>/dev/null | head -1 || true)
    [[ -s "$ws_file" ]] || ws_faults+=("A: no cache file was written to $ws_cache_dir")
    # compgen -G, NOT `[[ -e "$dir"/*.tmp ]]`: `[[ -e ]]` does not perform
    # pathname expansion on its operand, so that form tests for a file
    # literally named `*.jwss.tmp` and can never fire. It was written that way
    # here and was VACUOUS (proved by planting a .tmp and watching it pass) —
    # the same illusory-assertion class as WSR-RES-01.
    if compgen -G "$ws_cache_dir/*.tmp" >/dev/null; then
        ws_faults+=("A: a .tmp file was left behind in $ws_cache_dir")
    fi

    # B — warm cache: must restore, and must NOT boot.
    if [[ -s "$ws_file" ]]; then
        ws_b=$(timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --rtc "$NEXTZXOS_RTC" --warm-start --load "$ws_nex" \
            --delayed-screenshot "$ws_shot" --delayed-screenshot-frames 150 \
            --delayed-automatic-exit-frames 160 2>&1 || true)
        echo "$ws_b" | grep -q "restored a recorded NextZXOS machine" \
            || ws_faults+=("B: the second run did not restore the cached state")
        echo "$ws_b" | grep -q "cold-booting the firmware" \
            && ws_faults+=("B: the second run booted again although a cache was present")

        # D — the render, against the COMMITTED cold-path reference.
        if [[ -f "$ws_shot" ]]; then
            ws_diff=$(png_diff "$ws_shot" "$ws_ref")
            [[ "$ws_diff" == "0" ]] \
                || ws_faults+=("D: warm-started tilemap-demo differs from its reference by $ws_diff pixels")
        else
            ws_faults+=("D: no screenshot was written by the warm-started run")
        fi

        # C — corrupt the recorded SD digest in place. Offset 24 is the first
        # byte of the 64-char ASCII digest field (warm_start_cache.cpp), so one
        # byte is enough to make the identity disagree with the mounted image.
        printf 'Z' | dd of="$ws_file" bs=1 seek=24 count=1 conv=notrunc status=none
        ws_c=$(timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --rtc "$NEXTZXOS_RTC" --warm-start --load "$ws_nex" \
            --delayed-automatic-exit-frames 10 2>&1 || true)
        echo "$ws_c" | grep -q "recorded from a different SD image" \
            || ws_faults+=("C: a cache with the wrong SD digest was NOT refused")
        echo "$ws_c" | grep -q "recorded a NextZXOS-resident machine" \
            || ws_faults+=("C: the refused cache was not replaced by a fresh recording")
    fi

    rm -rf "$ws_cache_dir"

    if [[ ${#ws_faults[@]} -eq 0 ]]; then
        pass_row " (record, restore, digest invalidation, and the warm render matches its reference)"
    else
        fail_row " ($(IFS='; '; echo "${ws_faults[*]}"))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
