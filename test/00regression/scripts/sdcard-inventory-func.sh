#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable: the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# tools/sdcard-inventory.sh prints a normalised one-fact-per-line inventory of a
# NextZXOS card image and diffs two of them: the tool the developer guide's
# "Evaluating a new NextZXOS distribution" procedure uses to say what a new
# distribution changed. No NextZXOS byte is involved: the oracle is the bytes
# THIS row writes into two small scratch FAT32 cards (never a distribution
# file), shaped like the two generations the tool tells apart:
#   card A: a valid FAT32 card (512-byte clusters), a 16 KB zx81.rom, a 304640-byte
#           TBBLUE.FW starting 00 00 36 00, a TBBLUE.TBU with one core 3.02.01
#           record, and no enNextMf.rom;
#   card B: an under-clustered card (below the 65525-cluster FAT32 minimum), zx81.rom
#           = A's twice, a 635904-byte TBBLUE.FW starting 00 00 40 00, one core
#           3.03.00 record, and an enNextMf.rom;
#   card C: B with a 32 KB zx81.rom whose halves differ.
# TBU layout: FPGA tbumaker/src/maketbu.c (8-byte file type, 17-byte machine,
# records from byte 25: length, type, board, core_major = 16*major+minor, core_minor).
# The scratch cards live under $RUN_DIR, which the harness's own trap removes.
if want sdcard-inventory-func; then
    begin_func sdcard-inventory-func

    faults=()
    missing=()
    for tool in mcopy mdir mformat minfo mmd sha256sum; do
        command -v "$tool" &>/dev/null || missing+=("$tool")
    done

    if [[ ${#missing[@]} -gt 0 ]]; then
        fail_row " (tools absent: ${missing[*]} - install mtools)"
    else
    export MTOOLS_SKIP_CHECK=1
    INV="$PROJECT_DIR/tools/sdcard-inventory.sh"
    W="$RUN_DIR/private/sdcard-inventory"
    rm -rf "$W"; mkdir -p "$W"
    OFF=32256   # partition 0 at sector 63, as on every shipped card
    SIZE=$((48 * 1024 * 1024))

    # mbr <img>: one MBR entry (type 0x0C, LBA 63) and the 55AA signature.
    mbr() {
        printf '\x0c' | dd of="$1" bs=1 seek=$((0x1BE + 4)) conv=notrunc status=none
        printf '\x3f\x00\x00\x00' | dd of="$1" bs=1 seek=$((0x1BE + 8)) conv=notrunc status=none
        printf '\x41\x80\x01\x00' | dd of="$1" bs=1 seek=$((0x1BE + 12)) conv=notrunc status=none
        printf '\x55\xaa' | dd of="$1" bs=1 seek=510 conv=notrunc status=none
    }
    # mkcard <img> <cluster-sectors> <fw-size> <fw-magic-escape> <tbu-core-major-escape> <tbu-core-minor-escape> <zx81-file> [mf]
    mkcard() {
        local img=$1 cl=$2 fwsize=$3 magic=$4 cmaj=$5 cmin=$6 zx=$7 mf=${8:-}
        rm -f "$img"; truncate -s "$SIZE" "$img"
        mbr "$img"
        LANG=C mformat -i "$img@@$OFF" -F -T $(( (SIZE - OFF) / 512 )) -h 255 -s 63 -c "$cl" -N 01020304 \
            :: </dev/null >/dev/null 2>&1 || return 1
        LANG=C mmd -i "$img@@$OFF" ::/machines ::/machines/next </dev/null 2>/dev/null || return 1
        head -c 16384 /dev/zero | tr '\0' 'H' > "$W/48.rom"
        { printf "$magic"; head -c $((fwsize - 4)) /dev/zero; } > "$W/TBBLUE.FW"
        { printf 'TBUFILE\0ZX SPECTRUM NEXT\0'
          printf '\x0f\x01\x02'"$cmaj$cmin"; head -c 10 /dev/zero
          printf '\xff'; head -c 471 /dev/zero; } > "$W/TBBLUE.TBU"
        LANG=C mcopy -i "$img@@$OFF" "$W/48.rom" ::/machines/next/48.rom </dev/null || return 1
        LANG=C mcopy -i "$img@@$OFF" "$zx" ::/machines/next/zx81.rom </dev/null || return 1
        LANG=C mcopy -i "$img@@$OFF" "$W/TBBLUE.FW" ::/TBBLUE.FW </dev/null || return 1
        LANG=C mcopy -i "$img@@$OFF" "$W/TBBLUE.TBU" ::/TBBLUE.TBU </dev/null || return 1
        [[ -z "$mf" ]] || LANG=C mcopy -i "$img@@$OFF" "$mf" ::/machines/next/enNextMf.rom </dev/null
    }

    head -c 16384 /dev/zero | tr '\0' 'Z' > "$W/zx-a.rom"
    cat "$W/zx-a.rom" "$W/zx-a.rom" > "$W/zx-b.rom"
    { head -c 16384 /dev/zero | tr '\0' 'Z'; head -c 16384 /dev/zero | tr '\0' 'Y'; } > "$W/zx-c.rom"
    head -c 8192 /dev/zero | tr '\0' 'M' > "$W/mf.rom"
    zx_sha=$(sha256sum < "$W/zx-a.rom" | cut -d' ' -f1)
    zx_c_sha=$(sha256sum < "$W/zx-c.rom" | cut -d' ' -f1)

    if ! mkcard "$W/a.img" 1 304640 '\x00\x00\x36\x00' '\x32' '\x01' "$W/zx-a.rom" \
       || ! mkcard "$W/b.img" 4 635904 '\x00\x00\x40\x00' '\x33' '\x00' "$W/zx-b.rom" "$W/mf.rom"; then
        faults+=("harness: could not build the scratch FAT32 cards with mtools")
    else
        fw_a_sha=$(sha256sum < <({ printf '\x00\x00\x36\x00'; head -c 304636 /dev/zero; }) | cut -d' ' -f1)
        a_before=$(sha256sum < "$W/a.img"); b_before=$(sha256sum < "$W/b.img")

        # (a) card A's inventory has the exact expected lines
        rc=0; "$INV" "$W/a.img" > "$W/a.inv" 2> "$W/a.err" || rc=$?
        [[ $rc == 0 ]] || faults+=("a: the inventory of card A exited $rc: $(tail -n1 "$W/a.err")")
        for want_line in \
            "fat32-valid yes" \
            "image-size $SIZE" \
            "zx81 16384 sha256=$zx_sha" \
            "file TBBLUE.FW size=304640 sha256=$fw_a_sha" \
            "fw-magic 00003600" \
            "tbu type=1 board=2 display=0 core=3.02.01" \
            "file MACHINES/NEXT/enNextMf.rom absent" \
            "bpb serial=01020304" \
            "bpb bytes-per-sector=512 sectors-per-cluster=1 reserved=32 fats=2 fat-size=$(od -An -tu4 -j $((OFF + 0x24)) -N4 "$W/a.img" | tr -d ' ') total-sectors=$(( (SIZE - OFF) / 512 ))"; do
            grep -qFx -- "$want_line" "$W/a.inv" || faults+=("a: card A's inventory lacks the line '$want_line'")
        done
        n_tbu=$(grep -c '^tbu ' "$W/a.inv" || true)
        [[ "$n_tbu" == 1 ]] || faults+=("a: card A has one TBU record, the inventory lists $n_tbu")

        # (b) a card against itself: exit 0, nothing printed
        rc=0; out=$("$INV" --diff "$W/a.img" "$W/a.img" 2>&1) || rc=$?
        [[ $rc == 0 && -z "$out" ]] || faults+=("b: --diff A A exited $rc and printed '${out:0:80}'")

        # (c) A against B names exactly the facts that differ, and no others
        rc=0; out=$("$INV" --diff "$W/a.img" "$W/b.img" 2>&1) || rc=$?
        [[ $rc == 1 ]] || faults+=("c: --diff A B exited $rc, wanted 1")
        keys=$(awk '{k=$2; if ($2 == "bpb" || $2 == "file") k=$2 " " $3; sub(/=.*/, "", k); print k}' <<< "$out" | sort -u | tr '\n' ';')
        want_keys="bpb bytes-per-sector;bpb data-clusters;fat32-valid;file MACHINES/NEXT/enNextMf.rom;file TBBLUE.FW;file TBBLUE.TBU;fw-magic;tbu;zx81;"
        [[ "$keys" == "$want_keys" ]] || faults+=("c: --diff A B names '$keys', wanted '$want_keys'")
        grep -qFx -- "A: fat32-valid yes" <<< "$out" && grep -qFx -- "B: fat32-valid no" <<< "$out" \
            || faults+=("c: --diff A B does not say A is FAT32-valid and B is not")
        grep -qFx -- "B: zx81 32768 halves-identical half-sha256=$zx_sha" <<< "$out" \
            || faults+=("c: --diff A B does not report B's zx81.rom as A's file twice")
        grep -qFx -- "B: tbu type=1 board=2 display=0 core=3.03.00" <<< "$out" \
            || faults+=("c: --diff A B does not report B's core 3.03.00 record")
        grep -qFx -- "B: fw-magic 00004000" <<< "$out" || faults+=("c: --diff A B does not report B's firmware magic")

        # (c2) a 32 KB zx81.rom whose halves differ is not reported as doubled
        cp --reflink=auto "$W/b.img" "$W/c.img"
        LANG=C mcopy -o -i "$W/c.img@@$OFF" "$W/zx-c.rom" ::/machines/next/zx81.rom </dev/null >/dev/null 2>&1 \
            || faults+=("c2: could not write card C's zx81.rom")
        out=$("$INV" "$W/c.img" 2>&1) || true
        grep -qFx -- "zx81 32768 halves-differ sha256=$zx_c_sha" <<< "$out" \
            || faults+=("c2: a 32 KB zx81.rom with different halves is not reported as halves-differ")

        # (d) bad input: exit 2 on a non-FAT file, a missing file, and missing arguments
        head -c 65536 /dev/zero > "$W/junk.img"
        rc=0; "$INV" "$W/junk.img" >/dev/null 2>&1 || rc=$?
        [[ $rc == 2 ]] || faults+=("d: a file that is not a card image exited $rc, wanted 2")
        rc=0; "$INV" >/dev/null 2>&1 || rc=$?
        [[ $rc == 2 ]] || faults+=("d: no argument exited $rc, wanted 2")
        rc=0; "$INV" --diff "$W/a.img" >/dev/null 2>&1 || rc=$?
        [[ $rc == 2 ]] || faults+=("d: --diff with one image exited $rc, wanted 2")

        # (e) read-only: neither card changed
        [[ "$(sha256sum < "$W/a.img")" == "$a_before" && "$(sha256sum < "$W/b.img")" == "$b_before" ]] \
            || faults+=("e: the tool modified a card image")
    fi

    rm -rf "$W"

    if [[ ${#faults[@]} -eq 0 ]]; then
        pass_row " (the inventory of a synthetic valid card has the exact facts; --diff A A is empty, A against an under-clustered 3.03.00 card names exactly the differing facts and a doubled zx81.rom is told from a 32 KB one with different halves; bad input exits 2; the cards are not modified)"
    else
        fail_row " (${#faults[@]} fault(s) in tools/sdcard-inventory.sh)"
        printf '      %s\n' "${faults[@]}"
    fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
