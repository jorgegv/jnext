#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #269, end to end — the manual procedure from GH #24 made automatic.
#
# NextZXOS auto-mounts any .DSK/.P3D it finds at /NEXTZXOS/DRV-<X>.DSK and
# presents it as logical drive <X>. That was verified by hand in GH #24 by
# putting a disk image on the card with mtools; GH #269 exists so a user does
# not need mtools. This row proves the whole chain: jnext's own
# --sdcard-file-add puts a disk image on the card, and the emulated NextZXOS
# then mounts it.
#
# THREE BOOTS, because "the screen changed" would prove almost nothing:
#
#   clean   a pristine card                          -> baseline
#   valid   a real 40-track CPCEMU .DSK at DRV-A.DSK -> must DIFFER from clean
#   zeros   194816 zero bytes at DRV-A.DSK           -> must EQUAL clean
#
# The third boot is what makes the second mean something. If --sdcard-file-add
# merely created a directory entry of the right name and got the CONTENT wrong,
# `valid` would behave exactly like `zeros` — and `zeros` is measured, not
# assumed, to leave the drive list alone. So `valid != clean` and
# `zeros == clean` together say the file's BYTES arrived intact and NextZXOS
# parsed them.
#
# The difference is also bounded: one extra drive letter in the boot menu's
# "Logical drives:" line is a few hundred pixels. A whole different screen (a
# crash, a failed boot, a corrupted card) would be tens of thousands, and would
# otherwise satisfy a bare "they differ" test.
if want sdcard-dsk-automount-func; then
    begin_func sdcard-dsk-automount-func

    faults=()

    if ! command -v python3 &>/dev/null; then
        fail_row " (python3 is needed to generate the .DSK fixture)"
    else
    # Same private-card shape as the screenshot suite's `@private-sd` sentinel,
    # under $RUN_DIR so the clone is a reflink and the harness's own EXIT trap
    # removes it. This row installs no trap of its own (GH #153).
    BASE="$RUN_DIR/private/sdcard-dsk-automount"
    W="$BASE/work"
    mkdir -p "$W"

    # A blank but STRUCTURALLY VALID +3 disk image: the 256-byte CPCEMU
    # "MV - CPCEMU Disk-File" header, then 40 tracks x 1 side of (256-byte
    # Track-Info + 9 x 512-byte sectors) filled with the 0xE5 format byte.
    # 194816 bytes, the standard 180K +3 layout. Generated rather than checked
    # in: it is pure structure, and a generator is readable where a 190 KB
    # binary blob is not.
    python3 - "$W/blank.dsk" <<'PY'
import struct, sys
TRACKS, SIDES, SPT, SSIZE = 40, 1, 9, 512
track_size = 256 + SPT * SSIZE
hdr = b"MV - CPCEMU Disk-File\r\nDisk-Info\r\n"     # 34 bytes
hdr += b"jnext gh269   "                            # 14-byte creator field
hdr += bytes([TRACKS, SIDES]) + struct.pack('<H', track_size)
hdr += b"\x00" * (256 - len(hdr))
out = bytearray(hdr)
for t in range(TRACKS):
    ti = bytearray(256)
    ti[0:13] = b"Track-Info\r\n\x00"
    ti[0x10] = t        # track number
    ti[0x11] = 0        # side
    ti[0x14] = 2        # sector size code: 2 == 512 bytes
    ti[0x15] = SPT      # sectors per track
    ti[0x16] = 0x4E     # GAP#3 length
    ti[0x17] = 0xE5     # filler byte
    for s in range(SPT):
        off = 0x18 + s * 8
        ti[off + 0] = t          # C
        ti[off + 1] = 0          # H
        ti[off + 2] = s + 1      # R (1..9)
        ti[off + 3] = 2          # N (512)
    out += ti
    out += b"\xE5" * (SPT * SSIZE)
open(sys.argv[1], 'wb').write(bytes(out))
PY
    [[ $(stat -c '%s' "$W/blank.dsk") == 194816 ]] \
        || faults+=("the generated .DSK fixture is not 194816 bytes")
    # The control payload is the SAME SIZE, so the only thing that differs
    # between the two cards is the content of the file.
    head -c 194816 /dev/zero > "$W/zeros.dsk"

    boot_card() {   # boot_card <name> [dsk-to-add] — clone, add, boot, shoot
        # Separate statements: bash expands every word of a `local` command
        # before any of its assignments takes effect, so `cfg="$BASE/$name"`
        # on the same line would read an unset `name`.
        local name=$1 dsk=${2:-} rc=0
        local cfg="$BASE/$name"
        mkdir -p "$cfg/sdcard"
        cp --reflink=auto "$RUN_DIR/sdcard/cspect-next-1gb-fixed.img" \
                          "$cfg/sdcard/cspect-next-1gb-fixed.img"
        if [[ -n "$dsk" ]]; then
            JNEXT_CONFIG_DIR="$cfg" timeout --foreground --kill-after=5s 120s \
                "$JNEXT" --sdcard-file-add "$dsk" \
                         --sdcard-file-dest /NEXTZXOS/DRV-A.DSK \
                > "$W/add-$name.log" 2>&1 || rc=$?
            [[ $rc -eq 0 ]] \
                || faults+=("$name: --sdcard-file-add exited $rc")
        fi
        rc=0
        # Frame-based throughout, so the capture point does not move with host
        # speed. SPACE at 500 skips the welcome tour; the menu is settled by
        # 600. (The existing boot-nextzxos-menu row uses 400/450 on a card with
        # no disk images; mounting one costs NextZXOS extra frames before the
        # welcome appears, which is why this row waits longer.)
        JNEXT_CONFIG_DIR="$cfg" timeout --foreground --kill-after=5s 180s \
            "$JNEXT" --headless --machine next --rtc "$NEXTZXOS_RTC" \
                     --delayed-keypress-frames 500 space \
                     --delayed-screenshot "$W/$name.png" \
                     --delayed-screenshot-frames 600 \
                     --delayed-automatic-exit-frames 620 \
            > "$W/boot-$name.log" 2>&1 || rc=$?
        [[ $rc -eq 0 ]] || faults+=("$name: the NextZXOS boot exited $rc")
        [[ -f "$W/$name.png" ]] || faults+=("$name: no screenshot was produced")
    }

    boot_card clean
    boot_card valid "$W/blank.dsk"
    boot_card zeros "$W/zeros.dsk"

    if [[ -f "$W/clean.png" && -f "$W/valid.png" && -f "$W/zeros.png" ]]; then
        d_valid=$(png_diff "$W/clean.png" "$W/valid.png")
        d_zeros=$(png_diff "$W/clean.png" "$W/zeros.png")
        # The drive appeared: the boot menu's "Logical drives:" line gained a
        # letter. A handful of character cells, not a different screen.
        if [[ "$d_valid" -eq 0 ]]; then
            faults+=("the .DSK did not change the boot menu — NextZXOS did not mount it")
        elif [[ "$d_valid" -gt 2000 ]]; then
            faults+=("the .DSK changed $d_valid pixels — that is a different screen, not one more drive letter")
        fi
        # ...and it appeared because of the file's CONTENT, not its name.
        [[ "$d_zeros" -eq 0 ]] \
            || faults+=("a 194816-byte file of zeros also changed the boot menu ($d_zeros pixels) — the valid-.DSK result proves nothing")
    else
        faults+=("one or more boots produced no screenshot")
    fi

    if [[ ${#faults[@]} -eq 0 ]]; then
        pass_row " (NextZXOS auto-mounted the copied .DSK as drive A; ${d_valid} px, control 0 px)"
    else
        fail_row " (${#faults[@]} fault(s) in the .DSK automount chain)"
        printf '      %s\n' "${faults[@]}"
    fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
