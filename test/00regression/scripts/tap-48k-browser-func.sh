#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #282 — loading a .TAP through the NextZXOS browser in 48K mode.
#
# This is NOT `--load file.tap`: jnext's own TAP loader and its LD-BYTES fast
# trap are not involved and no tape is mounted. The guest firmware drives the
# whole thing — NextZXOS's TAP Loader pages its patched 48K image in as the
# ALT ROM (NR 0x8C = 0xC0 to write it, then 0xA0 to run it read-visible with
# lock_rom1) and relies on the DivMMC ROM3-conditional automap trap at 0x056A
# to serve the tape blocks from the card.
#
# jnext's gate for that trap used `sram_rom3` alone, but zxnext.vhd:3138 ends
# in a MUX:
#     (sram_altrom_en AND sram_pre_alt_128_n)
#       OR (sram_pre_rom3 AND NOT sram_altrom_en)
# On machine_type_p3 with lock_rom1=1/lock_rom0=0 that is 1 via the FIRST
# clause (:2991 sram_alt_128_n = lock_rom1), while sram_rom3 is
# lock_rom1 AND lock_rom0 = 0 (:2990). So the trap never fired, the alt-48
# image's stock LD-BYTES ran for real, and the machine span forever in
# LD-SAMPLE (0x05ED-0x05F8) polling port 0x7FFE for an EAR edge — the
# reporter's "endless loop", a permanently blank screen.
#
# THREE BOOTS, because "a tape loaded" is not one claim but three:
#
#   auto     autostarting tape, no keys after the mode pick
#              -> the program must RUN by itself
#   noautoA  non-autostarting tape, no keys after the mode pick
#              -> the program must NOT run (this is the CONTROL that gives
#                 the next boot its meaning)
#   noautoB  same tape, then R + ENTER typed into the 48K editor
#              -> the program must run NOW
#
# `noautoA` is what makes `noautoB` mean something. Without it, "one token
# appeared" is equally consistent with the tape having autostarted and the
# typed RUN having done nothing at all.
#
# The fixture is a BASIC one-liner `10 OUT 85,199`, so the proof is the
# emulated program's own OUT reaching --magic-port, not a pixel comparison:
# the tape's BYTES have to have arrived in 48K BASIC and been executed for
# the token to appear at all. Generated rather than checked in, for the same
# reason sdcard-dsk-automount-func generates its .DSK — 50 bytes of tokenised
# BASIC is unreadable as a blob and self-evident as a generator.
#
# The reported case is the NON-autostarting tape. The autostarting one is
# here because it is the obvious neighbouring case, and measuring it was how
# the autorun/no-autorun split was ruled OUT as the discriminator: both hang
# before the fix, and the real discriminator is the 48K mode pick.
if want tap-48k-browser-func; then
    begin_func tap-48k-browser-func

    faults=()

    if ! command -v python3 &>/dev/null; then
        fail_row " (python3 is needed to generate the .TAP fixtures)"
    else
    # Same private-card shape as sdcard-dsk-automount-func: under $RUN_DIR so
    # the clone is a reflink and the harness's own EXIT trap removes it. This
    # row installs no trap of its own (GH #153).
    BASE="$RUN_DIR/private/tap-48k-browser"
    W="$BASE/work"
    mkdir -p "$W"

    # Two .TAP files holding the same BASIC program, differing ONLY in the
    # header's autostart LINE field (10 vs 32768 = "no LINE clause").
    python3 - "$W" <<'PY'
import struct, sys
out = sys.argv[1]

def num(n):
    # Spectrum inline numeric form: the digits, then 0x0E and the 5-byte
    # small-integer representation the interpreter actually evaluates.
    return b"\x0e\x00\x00" + bytes([n & 0xFF, (n >> 8) & 0xFF]) + b"\x00"

# 10 OUT 85,199     (0xDF is the OUT token)
line = b"\xdf" + b"85" + num(85) + b"," + b"199" + num(199) + b"\x0d"
prog = b"\x00\x0a" + struct.pack("<H", len(line)) + line

def tap(name, autoline):
    hdr = bytearray()
    hdr += b"\x00\x00"                          # flag 0x00, type 0 = Program
    hdr += name.ljust(10).encode()[:10]
    hdr += struct.pack("<H", len(prog))         # length of the data block
    hdr += struct.pack("<H", autoline)          # LINE; >= 32768 means none
    hdr += struct.pack("<H", len(prog))         # start of variables
    c = 0
    for b in hdr:
        c ^= b
    hdr.append(c)
    data = bytearray(b"\xff") + prog            # flag 0xFF = data block
    c = 0
    for b in data:
        c ^= b
    data.append(c)
    return (struct.pack("<H", len(hdr)) + bytes(hdr) +
            struct.pack("<H", len(data)) + bytes(data))

open(out + "/auto.tap", "wb").write(tap("gh282ar", 10))
open(out + "/noauto.tap", "wb").write(tap("gh282nr", 32768))
PY
    [[ -s "$W/auto.tap" && -s "$W/noauto.tap" ]] \
        || faults+=("the .TAP fixtures were not generated")

    # boot_tap <name> <tap> <dest-name> [extra jnext args...]
    #
    # The destination name starts with a digit so the file sorts FIRST in the
    # browser's name-ordered root listing: the navigation is then SPACE (skip
    # the welcome tour), ENTER (open Browser), ENTER (the already-highlighted
    # first entry), 4 (48K mode) — no `down` presses whose count would depend
    # on what else is on the card. Nothing on the pristine card, and nothing
    # any other row adds to it, begins with a digit; if that ever changed, the
    # wrong entry would be opened and NO token would appear, so the row fails
    # loudly rather than passing for the wrong reason.
    boot_tap() {
        local name=$1 tap=$2 dest=$3 rc=0
        shift 3
        local cfg="$BASE/$name"
        mkdir -p "$cfg/sdcard"
        cp --reflink=auto "$RUN_DIR/sdcard/cspect-next-1gb-fixed.img" \
                          "$cfg/sdcard/cspect-next-1gb-fixed.img"
        JNEXT_CONFIG_DIR="$cfg" timeout --foreground --kill-after=5s 120s \
            "$JNEXT" --sdcard-file-add "$tap" --sdcard-file-dest "$dest" \
            > "$W/add-$name.log" 2>&1 || rc=$?
        [[ $rc -eq 0 ]] || faults+=("$name: --sdcard-file-add exited $rc")
        rc=0
        # Frame-based throughout, so the capture points do not move with host
        # speed. The tape is loaded and BASIC is back at its prompt well
        # before frame 1400.
        JNEXT_CONFIG_DIR="$cfg" timeout --foreground --kill-after=5s 300s \
            "$JNEXT" --headless --machine next --rtc "$NEXTZXOS_RTC" \
                     --magic-port 0x0055 --magic-port-mode dec \
                     --delayed-keypress-frames 400 space \
                     --delayed-keypress-frames 470 enter \
                     --delayed-keypress-frames 540 enter \
                     --delayed-keypress-frames 620 4 \
                     "$@" \
            > "$W/boot-$name.log" 2>&1 || rc=$?
        [[ $rc -eq 0 ]] || faults+=("$name: the NextZXOS boot exited $rc")
    }

    # tokens <name> — how many times the emulated program reached the magic
    # port. `dec` mode prints one decimal per byte on its own line.
    tokens() { grep -cx 199 "$W/boot-$1.log" || true; }

    if [[ ${#faults[@]} -eq 0 ]]; then
        boot_tap auto    "$W/auto.tap"   /0GH282A.TAP \
                 --delayed-automatic-exit-frames 1400
        boot_tap noautoA "$W/noauto.tap" /0GH282N.TAP \
                 --delayed-automatic-exit-frames 1400
        boot_tap noautoB "$W/noauto.tap" /0GH282N.TAP \
                 --delayed-keypress-frames 1450 r \
                 --delayed-keypress-frames 1500 enter \
                 --delayed-automatic-exit-frames 1600

        n_auto=$(tokens auto)
        n_a=$(tokens noautoA)
        n_b=$(tokens noautoB)

        # The autostarting tape must load AND run on its own. Before the
        # GH #282 fix this is 0: the machine never leaves LD-SAMPLE.
        [[ "$n_auto" -eq 1 ]] \
            || faults+=("the autostarting tape produced $n_auto magic-port tokens, expected 1")
        # The non-autostarting tape must load and STOP. This is the control.
        [[ "$n_a" -eq 0 ]] \
            || faults+=("the non-autostarting tape ran by itself ($n_a tokens), so the RUN result below proves nothing")
        # ...and then RUN from the 48K editor must execute it — the exact
        # thing GH #282 reported as impossible. Before the fix this is 0.
        [[ "$n_b" -eq 1 ]] \
            || faults+=("RUN after loading the non-autostarting tape produced $n_b tokens, expected 1")
    fi

    if [[ ${#faults[@]} -eq 0 ]]; then
        pass_row " (48K TAP via the NextZXOS browser: autostart ran, non-autostart loaded and RUN worked)"
    else
        fail_row " (${#faults[@]} fault(s) loading a .TAP through the NextZXOS browser in 48K mode)"
        printf '      %s\n' "${faults[@]}"
    fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
