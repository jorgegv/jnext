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
# NO --warm-start FLAG APPEARS BELOW, and that is the point of the row now.
# The warm start stopped being an option: a `--load` of a .nex on a Next takes
# the recorded machine because that is what the program meets on hardware. The
# invocations here are therefore ORDINARY loads, and if the default were ever
# quietly reverted to the synthetic machine every assertion in A and B would
# fail for the right reason (nothing would boot, nothing would restore).
#
# Seven things are asserted, in one run of each kind:
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
#               committed one, and the assertion is that the warm path lands
#               on it byte for byte.
#   E. DEFLATED the cached file is far smaller than the state it holds. The
#               stream is ~2.29 MB and ~90% zeros; the file must be under
#               512 KB, which a raw write cannot be and a broken compressor
#               would not be. Asserted on the FILE, because "it round-trips"
#               is equally true of an uncompressed one.
#   G. ROM 3    a warm-started program finds 48 BASIC at $0000-$3FFF, as one
#               launched by NextZXOS does. `nexload.asm` never writes 0x7FFD
#               or 0x1FFD; its last instruction is `rst $20` (:587), and it is
#               that NextZXOS handover which selects ROM 3. The recording is
#               taken at the NextZXOS menu, where the OS has its OWN ROM 0
#               paged, so without the selection in init_for_load_from_file()
#               every program that reads the character set out of ROM renders
#               NOISE. magic-bp-demo is the canary because it does exactly
#               that (`ROM_CHARSET 0x3C00`, demo/magic_bp_demo.c); the D row's
#               tilemap-demo does not, and passed throughout. Measured against
#               the real chain — boot NextZXOS, Command Line, `.nexload` —
#               which lands on the committed reference pixel for pixel.
#   F. LOUD     a Next .nex load that CANNOT warm-start says so on stderr,
#               caches nothing, and still runs. Every fallback in this feature
#               is meant to be announced — there is no flag on the command
#               line any more to remind the user one was attempted. A card
#               with no firmware on it is the case a shell can actually
#               reach: "no SD image mounted" is unreachable from the CLI
#               because main.cpp exits before the emulator when it cannot
#               resolve one (that guard has its own row, WSR-RES-04).
if want warm-start-func; then
    begin_func warm-start-func

    ws_faults=()
    ws_cache_dir="${JNEXT_CONFIG_DIR:-$HOME/.jnext}/warm-start"
    ws_nex="test/00regression/nex/tilemap_demo.nex"
    ws_ref="test/00regression/img/tilemap-demo-reference.png"
    ws_shot="$TMP_DIR/warm-start-tilemap.png"

    # A — cold cache: must boot and record. A PLAIN load, no flag.
    rm -rf "$ws_cache_dir"
    ws_a=$(timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
        "${SD_CARD_ARGS[@]}" --rtc "$NEXTZXOS_RTC" --load "$ws_nex" \
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

    # E — the payload is deflated. The log line names both sizes, but the
    # assertion is on the FILE: a log can be wrong about what was written.
    # 512 KB is chosen to be unreachable by an uncompressed write (~2.3 MB)
    # and by a compressor producing near-incompressible output, while leaving
    # room for the real figure (~126 KB) to drift with the zlib version, the
    # firmware on the card and any growth in the state stream itself.
    if [[ -s "$ws_file" ]]; then
        ws_sz=$(stat -c%s "$ws_file")
        if (( ws_sz >= 524288 )); then
            ws_faults+=("E: the cache file is $ws_sz bytes — the payload is not deflated")
        fi
    fi

    # B — warm cache: must restore, and must NOT boot.
    if [[ -s "$ws_file" ]]; then
        ws_b=$(timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --rtc "$NEXTZXOS_RTC" --load "$ws_nex" \
            --delayed-screenshot "$ws_shot" --delayed-screenshot-frames 150 \
            --delayed-automatic-exit-frames 160 2>&1 || true)
        echo "$ws_b" | grep -q "restored a recorded NextZXOS machine" \
            || ws_faults+=("B: the second run did not restore the cached state")
        echo "$ws_b" | grep -q "cold-booting the firmware" \
            && ws_faults+=("B: the second run booted again although a cache was present")

        # D — the render, against the committed reference.
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
            "${SD_CARD_ARGS[@]}" --rtc "$NEXTZXOS_RTC" --load "$ws_nex" \
            --delayed-automatic-exit-frames 10 2>&1 || true)
        echo "$ws_c" | grep -q "recorded from a different SD image" \
            || ws_faults+=("C: a cache with the wrong SD digest was NOT refused")
        echo "$ws_c" | grep -q "recorded a NextZXOS-resident machine" \
            || ws_faults+=("C: the refused cache was not replaced by a fresh recording")
    fi

    # G — the ROM-3 handover. Its own run, because it needs a DIFFERENT NEX
    # from D: the one whose glyphs come out of ROM.
    if [[ -s "$ws_file" ]]; then
        ws_rom_shot="$TMP_DIR/warm-start-rom3.png"
        timeout --foreground --kill-after=5s 120s "$JNEXT" --headless --machine next \
            "${SD_CARD_ARGS[@]}" --rtc "$NEXTZXOS_RTC" \
            --load "test/00regression/nex/magic_bp_demo.nex" \
            --delayed-screenshot "$ws_rom_shot" --delayed-screenshot-frames 150 \
            --delayed-automatic-exit-frames 160 >/dev/null 2>&1 || true
        if [[ -f "$ws_rom_shot" ]]; then
            ws_rom_diff=$(png_diff "$ws_rom_shot" \
                          "test/00regression/img/magic-bp-demo-reference.png")
            [[ "$ws_rom_diff" == "0" ]] \
                || ws_faults+=("G: a warm-started ROM-font program differs from its reference by $ws_rom_diff pixels — 48 BASIC is not paged at \$0000")
        else
            ws_faults+=("G: no screenshot was written by the ROM-font run")
        fi
    fi

    # F — the fallback is LOUD, caches nothing, and the run still works. A
    # 1 MiB file of zeroes is a readable card with no firmware on it: it gets
    # past the machine-type and no-SD guards and past the digest, so the
    # recording really boots and really fails the residency check. Its own
    # cache directory, so the verdict cannot be confused with the real card's
    # recording made above.
    ws_junk="$TMP_DIR/warm-start-no-firmware.img"
    ws_junk_cfg="$TMP_DIR/warm-start-junk-cfg"
    rm -rf "$ws_junk_cfg"; mkdir -p "$ws_junk_cfg"
    dd if=/dev/zero of="$ws_junk" bs=1024 count=1024 status=none
    ws_f_rc=0
    ws_f=$(JNEXT_CONFIG_DIR="$ws_junk_cfg" timeout --foreground --kill-after=5s 120s \
        "$JNEXT" --headless --machine next --sdcard "$ws_junk" --rtc "$NEXTZXOS_RTC" \
        --load "$ws_nex" --delayed-automatic-exit-frames 10 2>&1) || ws_f_rc=$?
    echo "$ws_f" | grep -q "Not recording; this load falls back to the synthetic machine" \
        || ws_faults+=("F: a card with no firmware fell back to the synthetic machine SILENTLY")
    [[ "$ws_f_rc" == "0" ]] \
        || ws_faults+=("F: the fallback run exited $ws_f_rc — a decline must not fail the run")
    if compgen -G "$ws_junk_cfg/warm-start/*.jwss" >/dev/null; then
        ws_faults+=("F: a failed recording was CACHED — a machine that never booted must not be")
    fi
    rm -rf "$ws_junk_cfg" "$ws_junk"

    rm -rf "$ws_cache_dir"

    if [[ ${#ws_faults[@]} -eq 0 ]]; then
        pass_row " (record, restore, digest invalidation, deflated payload, ROM-3 handover, loud fallback, and the warm render matches its reference)"
    else
        fail_row " ($(IFS='; '; echo "${ws_faults[*]}"))"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
