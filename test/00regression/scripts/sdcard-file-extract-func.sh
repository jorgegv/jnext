#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #557 - --sdcard-file-extract through the real binary, validated by FOREIGN tools:
#   FX-F01 the run's real card: a directory with long names compared with mtools' own copy of it
#   FX-F02 a FAT16 and a FAT32 card made by mformat, files written by mcopy (a foreign writer)
#   FX-F03 the default card (no --sdcard)
#   FX-F04 a file added into an --sdcard-writes file is extracted with it, and is not on the card without it
#   FX-F05 the statuses at the CLI: missing path 2, existing local name 4, a card held by another jnext 5
#   FX-F06 the usage errors (1), --no-sdcard and the rescue verbs refused, --sdcard-readonly accepted
# Oracle: the issue and its plan (the file or directory lands in the current directory under its stored
# name; nothing is overwritten; exit statuses are file-add's table; a held card is "in use by another
# jnext", status 5), and mtools as the second reader/writer of the FAT.
if want sdcard-file-extract-func; then
    begin_func sdcard-file-extract-func

    faults=()
    missing=()
    for tool in mdir mcopy mformat mmd sfdisk; do
        command -v "$tool" &>/dev/null || missing+=("$tool")
    done
    if [[ ${#missing[@]} -gt 0 ]]; then
        fail_row " (foreign tools absent: ${missing[*]} - install mtools and util-linux)"
    else
    export MTOOLS_SKIP_CHECK=1
    CFG="$RUN_DIR/private/sdcard-file-extract"
    W="$CFG/work"
    rm -rf "$CFG"; mkdir -p "$CFG/sdcard" "$W"
    MASTER="$RUN_DIR/sdcard/cspect-next-1gb-fixed.img"
    CARD="$W/card.img"
    cp --reflink=auto "$MASTER" "$CARD"
    DEFAULT_CARD="$CFG/sdcard/cspect-next-1gb-fixed.img"
    cp --reflink=auto "$MASTER" "$DEFAULT_CARD"
    part_lba=$(od -An -tu4 -j $((0x1BE + 8)) -N4 "$CARD" | tr -d ' ')
    OFF=$((part_lba * 512))

    OUT_RC=0
    # xrun <dir> <label> <want-rc> <jnext args...>: jnext with <dir> as the current directory, output in $W/<label>.out
    xrun() {
        local dir=$1 label=$2 want=$3; shift 3
        OUT_RC=0
        ( cd "$dir" && JNEXT_CONFIG_DIR="$CFG" timeout --foreground --kill-after=5s 300s "$JNEXT" "$@" ) > "$W/$label.out" 2>&1 || OUT_RC=$?
        [[ "$OUT_RC" == "$want" ]] || faults+=("$label: exit $OUT_RC, wanted $want: $(head -c 300 "$W/$label.out")")
    }
    # norm_tree <dir>: every directory and file of a tree, path in lower case (the stored case differs
    # between readers of a short name) with the file's SHA-256
    norm_tree() {
        ( cd "$1" && {
            find . -mindepth 1 -type d | tr 'A-Z' 'a-z' | LC_ALL=C sort
            find . -type f | while IFS= read -r f; do
                printf '%s %s\n' "$(printf '%s' "$f" | tr 'A-Z' 'a-z')" "$(sha256sum < "$f" | cut -d' ' -f1)"
            done | LC_ALL=C sort
        } )
    }
    sha() { sha256sum "$1" | cut -d' ' -f1; }

    # FX-F01: a directory with long names, compared with mtools' copy of the same directory
    mkdir -p "$W/o1" "$W/ref1"
    sha_before=$(sha "$CARD")
    xrun "$W/o1" fx1 0 --sdcard "$CARD" --sdcard-file-extract "/demos/Colour Schemes"
    mcopy -s -n -i "$CARD@@$OFF" "::/demos/Colour Schemes" "$W/ref1" 2>/dev/null || faults+=("FX-F01: mcopy could not read the directory")
    [[ -d "$W/o1/Colour Schemes/Browser" ]] || faults+=("FX-F01: the stored long names are not the local names: $(ls "$W/o1" | head -c 200)")
    [[ -n "$(norm_tree "$W/ref1")" ]] || faults+=("FX-F01: the reference tree is empty")
    [[ "$(norm_tree "$W/o1")" == "$(norm_tree "$W/ref1")" ]] || faults+=("FX-F01: the extracted tree differs from mtools' copy")
    [[ "$(sha "$CARD")" == "$sha_before" ]] || faults+=("FX-F01: the card changed")

    # FX-F02: a FAT16 (no partition table) and a FAT32 (partitioned) card made by mformat, files by mcopy
    head -c 3000 /dev/urandom > "$W/Long Name Here.bin"
    printf 'hello\n' > "$W/small.txt"
    truncate -s 24M "$W/f16.img"
    mformat -i "$W/f16.img" -T 49152 :: >/dev/null 2>&1 || faults+=("FX-F02: mformat FAT16 failed")
    mcopy -i "$W/f16.img" "$W/Long Name Here.bin" "$W/small.txt" :: >/dev/null 2>&1 || faults+=("FX-F02: mcopy into FAT16 failed")
    truncate -s 80M "$W/f32.img"
    sfdisk -q "$W/f32.img" <<< 'start=2048, type=c' >/dev/null 2>&1 || faults+=("FX-F02: sfdisk failed")
    mformat -i "$W/f32.img@@1048576" -F -T $((80 * 2048 - 2048)) :: >/dev/null 2>&1 || faults+=("FX-F02: mformat FAT32 failed")
    mmd -i "$W/f32.img@@1048576" "::/Some Dir" >/dev/null 2>&1 || faults+=("FX-F02: mmd failed")
    mcopy -i "$W/f32.img@@1048576" "$W/Long Name Here.bin" "$W/small.txt" "::/Some Dir/" >/dev/null 2>&1 || faults+=("FX-F02: mcopy into FAT32 failed")
    [[ "$(od -An -c -j 54 -N 5 "$W/f16.img" | tr -d ' ')" == "FAT16" ]] || faults+=("FX-F02: the first fixture is not FAT16")
    [[ "$(od -An -c -j $((1048576 + 82)) -N 5 "$W/f32.img" | tr -d ' ')" == "FAT32" ]] || faults+=("FX-F02: the second fixture is not FAT32")
    mkdir -p "$W/o2a" "$W/o2b"
    xrun "$W/o2a" fx2a 0 --sdcard "$W/f16.img" --sdcard-file-extract "/Long Name Here.bin"
    cmp -s "$W/Long Name Here.bin" "$W/o2a/Long Name Here.bin" || faults+=("FX-F02: the FAT16 file differs")
    xrun "$W/o2b" fx2b 0 --sdcard "$W/f32.img" --sdcard-file-extract "/Some Dir"
    cmp -s "$W/Long Name Here.bin" "$W/o2b/Some Dir/Long Name Here.bin" || faults+=("FX-F02: the FAT32 long-named file differs")
    cmp -s "$W/small.txt" "$W/o2b/Some Dir/small.txt" || faults+=("FX-F02: the FAT32 short-named file differs")

    # FX-F03: the default card (no --sdcard)
    mkdir -p "$W/o3"
    xrun "$W/o3" fx3 0 --sdcard-file-extract "/demos/Colour Schemes"
    [[ "$(norm_tree "$W/o3")" == "$(norm_tree "$W/ref1")" ]] || faults+=("FX-F03: the default card's extraction differs from mtools' copy")

    # FX-F04: a writes file
    A="$W/a.img"; J="$W/a.jsdw"
    cp --reflink=auto "$MASTER" "$A"
    head -c 5000 /dev/urandom > "$W/added.bin"
    xrun "$W" fx4add 0 --sdcard "$A" --sdcard-writes "$J" --sdcard-file-add "$W/added.bin" --sdcard-file-dest /EXTRACT-ME.BIN
    mkdir -p "$W/o4a" "$W/o4b"
    before=$(sha "$A")
    xrun "$W/o4a" fx4a 0 --sdcard "$A" --sdcard-writes "$J" --sdcard-file-extract /EXTRACT-ME.BIN
    cmp -s "$W/added.bin" "$W/o4a/EXTRACT-ME.BIN" || faults+=("FX-F04: the file in the writes file was not extracted with its bytes")
    grep -qF "read through $J" "$W/fx4a.out" || faults+=("FX-F04: the report does not say it read through the writes file")
    xrun "$W/o4b" fx4b 2 --sdcard "$A" --sdcard-file-extract /EXTRACT-ME.BIN
    [[ "$(sha "$A")" == "$before" ]] || faults+=("FX-F04: the image changed")

    # FX-F05: statuses
    mkdir -p "$W/o5"
    xrun "$W/o5" fx5miss 2 --sdcard "$CARD" --sdcard-file-extract /NO-SUCH-FILE.BIN
    [[ -z "$(ls -A "$W/o5")" ]] || faults+=("FX-F05: a missing path left something behind")
    xrun "$W/o5" fx5ok 0 --sdcard "$CARD" --sdcard-file-extract /LICENSE.md
    xrun "$W/o5" fx5exists 4 --sdcard "$CARD" --sdcard-file-extract /LICENSE.md
    # a card held by another jnext: refused with 5 and the lock message
    HOLD_LOG="$W/holder.log"
    JNEXT_CONFIG_DIR="$CFG" "$JNEXT" --headless --machine 48k --rewind-buffer-size 0 \
        --sdcard "$CARD" --delayed-automatic-exit 60 > "$HOLD_LOG" 2>&1 &
    HOLD_PID=$!
    held=0
    for _ in $(seq 150); do   # up to 30 s: it has taken its lock once it reports the card
        if grep -qF "SD card image mounted" "$HOLD_LOG" 2>/dev/null; then held=1; break; fi
        kill -0 "$HOLD_PID" 2>/dev/null || break
        sleep 0.2
    done
    [[ "$held" == 1 ]] || faults+=("FX-F05: the holder never reported its card mounted")
    mkdir -p "$W/o5b"
    xrun "$W/o5b" fx5held 5 --sdcard "$CARD" --sdcard-file-extract /LICENSE.md
    grep -qF "is in use by another jnext" "$W/fx5held.out" || faults+=("FX-F05: the lock message is missing: $(head -c 300 "$W/fx5held.out")")
    [[ -z "$(ls -A "$W/o5b")" ]] || faults+=("FX-F05: a held card left something behind")
    kill "$HOLD_PID" 2>/dev/null || true
    wait "$HOLD_PID" 2>/dev/null || true

    # FX-F06: usage
    mkdir -p "$W/o6"
    xrun "$W/o6" fx6twice 1 --sdcard "$CARD" --sdcard-file-extract /A.BIN --sdcard-file-extract /B.BIN
    xrun "$W/o6" fx6empty 1 --sdcard "$CARD" --sdcard-file-extract ""
    xrun "$W/o6" fx6load 1 --sdcard "$CARD" --load "$W/none.nex" --sdcard-file-extract /LICENSE.md
    xrun "$W/o6" fx6add 1 --sdcard "$CARD" --sdcard-file-add "$W/small.txt" --sdcard-file-extract /LICENSE.md
    xrun "$W/o6" fx6nosd 1 --no-sdcard --sdcard-file-extract /LICENSE.md
    grep -qF -- "--no-sdcard cannot be combined with --sdcard-file-extract" "$W/fx6nosd.out" || faults+=("FX-F06: --no-sdcard is not refused by name: $(head -c 300 "$W/fx6nosd.out")")
    xrun "$W/o6" fx6nosd-flat 1 --no-sdcard --sdcard-writes-flatten "$W/o6/x.img"
    grep -qF -- "--no-sdcard cannot be combined with --sdcard-writes-flatten" "$W/fx6nosd-flat.out" || faults+=("FX-F06: --no-sdcard with --sdcard-writes-flatten is not refused by name")
    xrun "$W/o6" fx6nosd-exp 1 --no-sdcard --sdcard-export-changes "$W/o6/x"
    grep -qF -- "--no-sdcard cannot be combined with --sdcard-export-changes" "$W/fx6nosd-exp.out" || faults+=("FX-F06: --no-sdcard with --sdcard-export-changes is not refused by name")
    xrun "$W/o6" fx6nosd-imp 1 --no-sdcard --sdcard-import-changes "$W/o6/x"
    grep -qF -- "--no-sdcard cannot be combined with --sdcard-import-changes" "$W/fx6nosd-imp.out" || faults+=("FX-F06: --no-sdcard with --sdcard-import-changes is not refused by name")
    [[ -z "$(ls -A "$W/o6")" ]] || faults+=("FX-F06: a refused usage left something behind")
    mkdir -p "$W/o6b"
    xrun "$W/o6b" fx6ro 0 --sdcard "$CARD" --sdcard-readonly --sdcard-file-extract /LICENSE.md
    [[ -s "$W/o6b/LICENSE.md" ]] || faults+=("FX-F06: --sdcard-readonly extracted nothing")

    rm -rf "$CFG"
    if [[ ${#faults[@]} -eq 0 ]]; then
        pass_row " (a long-named directory equals mtools' copy; FAT16 partition-less and FAT32 partitioned mformat cards; the default card; a writes file; statuses 2, 4 and 5 for a held card; usage errors; --no-sdcard refuses it and the rescue verbs by name)"
    else
        fail_row " (${#faults[@]} fault(s) in --sdcard-file-extract)"
        printf '      %s\n' "${faults[@]}"
    fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
