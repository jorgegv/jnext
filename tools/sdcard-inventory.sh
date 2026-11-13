#!/usr/bin/env bash
# sdcard-inventory.sh: a read-only, normalised, one-fact-per-line inventory of a
# NextZXOS SD card image, to compare two distributions (or a raw and a fixed
# card) without opening either by hand.
#
#   tools/sdcard-inventory.sh <image>
#   tools/sdcard-inventory.sh --diff <a.img> <b.img>
#
# Inventory: the image size; MBR partition 0 (type, start LBA, sectors); the
# FAT32 BPB (geometry, data-cluster count, "fat32-valid" against the 65525
# cluster minimum, volume serial); size + sha256 of the ROMs, menu.def and the
# firmware files jnext reads; the first four bytes of TBBLUE.FW; the shape of
# zx81.rom; the records of TBBLUE.TBU (layout from the FPGA repository's
# tbumaker/src/maketbu.c); and a digest of the /dot directory's name list.
# Every file is read through mtools (mcopy to stdout), the image is opened
# read-only, nothing is written to it, and no card byte is printed beyond the
# four-byte firmware magic.
#
# --diff prints "A: <line>" / "B: <line>" for each line that differs and exits 0
# when the two inventories are identical, 1 when they differ. Exit 2: bad
# arguments, a missing tool, or a file that is not a FAT32 card image.
#
# This tool prints facts ABOUT a card. The card itself is copyrighted and is
# never committed or posted; see the developer guide, "Evaluating a new
# NextZXOS distribution".
set -euo pipefail

# Children run under LANG=C, and LC_ALL beats LANG: set both.
export LANG=C LC_ALL=C
export MTOOLS_SKIP_CHECK=1

die() { echo "sdcard-inventory: $*" >&2; exit 2; }

for tool in mcopy mdir od sha256sum diff mktemp wc awk sort tr; do
    command -v "$tool" >/dev/null 2>&1 || die "'$tool' is required (mtools / coreutils / diffutils)"
done

# u <image> <byte offset> <length>: a little-endian unsigned integer.
u() {
    local v
    v=$(od -An -tu"$3" -j "$2" -N "$3" -- "$1" 2>/dev/null | tr -d ' ')
    [[ -n "$v" ]] || die "$1: short read at byte $2"
    echo "$v"
}

# inventory <image>: the facts, on stdout.
inventory() {
    local img=$1 w size sig ptype plba psect off
    [[ -f "$img" ]] || die "$img: no such file"
    size=$(stat -c %s -- "$img")
    [[ "$size" -ge 1024 ]] || die "$img: too small to be a card image"
    sig=$(od -An -tx1 -j 510 -N 2 -- "$img" | tr -d ' ')
    [[ "$sig" == 55aa ]] || die "$img: no MBR signature"
    ptype=$(od -An -tx1 -j $((0x1BE + 4)) -N 1 -- "$img" | tr -d ' ')
    plba=$(u "$img" $((0x1BE + 8)) 4)
    psect=$(u "$img" $((0x1BE + 12)) 4)
    [[ "$plba" -gt 0 ]] || die "$img: MBR partition 0 is empty"
    off=$((plba * 512))

    local bps spc rsv nfats fatsz total clusters valid serial
    bps=$(u "$img" $((off + 0x0B)) 2); spc=$(u "$img" $((off + 0x0D)) 1)
    rsv=$(u "$img" $((off + 0x0E)) 2); nfats=$(u "$img" $((off + 0x10)) 1)
    total=$(u "$img" $((off + 0x20)) 4); fatsz=$(u "$img" $((off + 0x24)) 4)
    serial=$(od -An -tx4 -j $((off + 0x43)) -N 4 -- "$img" | tr -d ' ')
    [[ "$bps" -gt 0 && "$spc" -gt 0 ]] || die "$img: not a FAT32 volume at LBA $plba"
    clusters=$(( (total - rsv - nfats * fatsz) / spc ))
    valid=no; [[ "$clusters" -ge 65525 ]] && valid=yes

    echo "image-size $size"
    echo "mbr0 type=$ptype lba=$plba sectors=$psect"
    echo "bpb bytes-per-sector=$bps sectors-per-cluster=$spc reserved=$rsv fats=$nfats fat-size=$fatsz total-sectors=$total"
    echo "bpb data-clusters=$clusters"
    echo "fat32-valid $valid"
    echo "bpb serial=$serial"

    w=$(mktemp -d)
    # shellcheck disable=SC2064  # $w is expanded now on purpose
    trap "rm -rf '$w'" RETURN
    # fetch <name-on-card> <local>: 0 when present.
    fetch() { mcopy -n -i "$img@@$off" "::/$1" "$2" </dev/null >/dev/null 2>&1; }

    local f
    for f in MACHINES/NEXT/48.rom MACHINES/NEXT/128.rom MACHINES/NEXT/plus3.rom \
             MACHINES/NEXT/enNextZX.rom MACHINES/NEXT/enNxtmmc.rom MACHINES/NEXT/enNextMf.rom \
             MACHINES/NEXT/enAltZX.rom MACHINES/NEXT/menu.def TBBLUE.FW TBBLUE.TBU; do
        rm -f "$w/f"
        if fetch "$f" "$w/f"; then
            echo "file $f size=$(stat -c %s -- "$w/f") sha256=$(sha256sum < "$w/f" | cut -d' ' -f1)"
        else
            echo "file $f absent"
        fi
    done

    rm -f "$w/f"
    if fetch TBBLUE.FW "$w/f"; then
        echo "fw-magic $(od -An -tx1 -N 4 -- "$w/f" | tr -d ' ')"
    else
        echo "fw-magic absent"
    fi

    # zx81.rom: the size, and for a 32768-byte file whether its two halves are
    # identical (the 24.11 file doubled) with the half's hash, so no hash of a
    # particular distribution is written here.
    rm -f "$w/f"
    if fetch MACHINES/NEXT/zx81.rom "$w/f"; then
        local zs; zs=$(stat -c %s -- "$w/f")
        if [[ "$zs" == 32768 ]]; then
            head -c 16384 "$w/f" > "$w/h1"; tail -c 16384 "$w/f" > "$w/h2"
            if cmp -s "$w/h1" "$w/h2"; then
                echo "zx81 $zs halves-identical half-sha256=$(sha256sum < "$w/h1" | cut -d' ' -f1)"
            else
                echo "zx81 $zs halves-differ sha256=$(sha256sum < "$w/f" | cut -d' ' -f1)"
            fi
        else
            echo "zx81 $zs sha256=$(sha256sum < "$w/f" | cut -d' ' -f1)"
        fi
    else
        echo "zx81 absent"
    fi

    # TBBLUE.TBU: 512-byte header (8-byte file type, 17-byte machine name, then
    # records from byte 25): length(1) type(1) board(1) core_major(1)
    # core_minor(1) ..., length 0xFF ends the list. core_major byte = 16 * major
    # + the two-digit minor, core_minor byte = the two-digit sub-version.
    rm -f "$w/f"
    if fetch TBBLUE.TBU "$w/f"; then
        local p=25 len typ brd cmaj cmin
        while [[ $p -lt 512 ]]; do
            len=$(u "$w/f" "$p" 1)
            [[ "$len" -ne 255 && "$len" -ne 0 ]] || break
            typ=$(u "$w/f" $((p + 1)) 1); brd=$(u "$w/f" $((p + 2)) 1)
            cmaj=$(u "$w/f" $((p + 3)) 1); cmin=$(u "$w/f" $((p + 4)) 1)
            printf 'tbu type=%d board=%d display=%d core=%d.%02d.%02d\n' \
                "$typ" $((brd & 15)) $(((brd >> 4) & 3)) $((cmaj >> 4)) $((cmaj & 15)) "$cmin"
            p=$((p + len))
        done
    else
        echo "tbu absent"
    fi

    # /dot: how many names, and a digest of the sorted, case-folded list.
    if mdir -b -i "$img@@$off" ::/dot </dev/null > "$w/dot" 2>/dev/null; then
        echo "dot names=$(wc -l < "$w/dot") digest=$(tr 'A-Z' 'a-z' < "$w/dot" | sort | sha256sum | cut -d' ' -f1)"
    else
        echo "dot absent"
    fi
}

case "${1:-}" in
    --diff)
        [[ $# -eq 3 ]] || die "usage: $0 --diff <a.img> <b.img>"
        t=$(mktemp -d); trap 'rm -rf "$t"' EXIT
        inventory "$2" > "$t/a"
        inventory "$3" > "$t/b"
        rc=0
        diff --old-line-format='A: %L' --new-line-format='B: %L' --unchanged-line-format='' \
            "$t/a" "$t/b" || rc=$?
        [[ $rc -le 1 ]] || die "diff failed ($rc)"
        exit "$rc"
        ;;
    ""|-*) die "usage: $0 <image>  |  $0 --diff <a.img> <b.img>" ;;
    *)
        [[ $# -eq 1 ]] || die "usage: $0 <image>  |  $0 --diff <a.img> <b.img>"
        inventory "$1"
        ;;
esac
