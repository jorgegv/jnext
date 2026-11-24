#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #555 - the rescue verbs on real cards, validated by FOREIGN tools (mtools, cmp):
#   --sdcard-writes-flatten, --sdcard-export-changes (+ --sdcard-export-base) and
#   --sdcard-import-changes, run through the real binary on private clones of the run's card.
# Oracle: the issue's rescue requirement and its plan: flatten = the card with the writes laid over
# it; export = the files you added or changed plus CHANGES.txt; import = the inverse, never deleting,
# replacing, restoring the attributes, all or nothing, into the image or into a writes file.
if want sdcard-rescue-func; then
    begin_func sdcard-rescue-func

    faults=()
    missing=()
    for tool in mdir mcopy mattrib sfdisk; do
        command -v "$tool" &>/dev/null || missing+=("$tool")
    done
    if [[ ${#missing[@]} -gt 0 ]]; then
        fail_row " (foreign validators absent: ${missing[*]} - install mtools and util-linux)"
    else
    export MTOOLS_SKIP_CHECK=1
    W="$RUN_DIR/private/sdcard-rescue"
    rm -rf "$W"; mkdir -p "$W/src"
    MASTER="$RUN_DIR/sdcard/cspect-next-1gb-fixed.img"
    BASE="$W/base.img"; A="$W/a.img"
    cp --reflink=auto "$MASTER" "$BASE"
    cp --reflink=auto "$MASTER" "$A"
    part_lba=$(od -An -tu4 -j $((0x1BE + 8)) -N4 "$A" | tr -d ' ')
    OFF=$((part_lba * 512))

    printf 'one-direct\n'  > "$W/src/one.bin"
    printf 'two-direct\n'  > "$W/src/two.bin"
    printf 'three-overlay\n' > "$W/src/three.bin"
    run() {   # run <label> <expected-exit> <jnext args...>: output in $W/<label>.out
        local label=$1 want=$2 rc=0; shift 2
        timeout --foreground --kill-after=5s 300s "$JNEXT" "$@" > "$W/$label.out" 2>&1 || rc=$?
        [[ "$rc" == "$want" ]] || faults+=("$label: exit $rc, wanted $want: $(head -c 300 "$W/$label.out")")
    }
    sha() { sha256sum "$1" | cut -d' ' -f1; }

    # card A: two direct additions (one made hidden by a FOREIGN writer), then one into an overlay
    run add1 0 --sdcard "$A" --sdcard-file-add "$W/src/one.bin" --sdcard-file-dest /RESC/one.bin
    run add2 0 --sdcard "$A" --sdcard-file-add "$W/src/two.bin" --sdcard-file-dest /RESC/two.bin
    mattrib +h -i "$A@@$OFF" ::/RESC/two.bin || faults+=("mattrib +h failed")
    J="$W/a.jsdw"
    run add3 0 --sdcard "$A" --sdcard-writes "$J" --sdcard-file-add "$W/src/three.bin" --sdcard-file-dest /RESC/three.bin

    # flatten: the foreign reader finds the overlay's file in OUT; an existing OUT is refused (3)
    FLAT="$W/flat.img"
    run flat 0 --sdcard "$A" --sdcard-writes "$J" --sdcard-writes-flatten "$FLAT"
    mcopy -i "$FLAT@@$OFF" ::/RESC/three.bin "$W/flat-three.out" 2>/dev/null || faults+=("flatten: the overlay's file is not in OUT")
    cmp -s "$W/src/three.bin" "$W/flat-three.out" || faults+=("flatten: the overlay's file differs in OUT")
    mcopy -i "$FLAT@@$OFF" ::/RESC/one.bin "$W/flat-one.out" 2>/dev/null || faults+=("flatten: the direct file is not in OUT")
    run flat-again 3 --sdcard "$A" --sdcard-writes "$J" --sdcard-writes-flatten "$FLAT"

    # export: the machine's view (A + J) against the image the card was made from
    DIR="$W/export"
    run export 0 --sdcard "$A" --sdcard-writes "$J" --sdcard-export-base "$BASE" --sdcard-export-changes "$DIR"
    for f in one two three; do
        cmp -s "$W/src/$f.bin" "$DIR/RESC/$f.bin" || faults+=("export: RESC/$f.bin differs from its source")
    done
    [[ "$(grep -c '^A' "$DIR/CHANGES.txt")" == 3 ]] || faults+=("export: CHANGES.txt does not list the three added files: $(head -c 300 "$DIR/CHANGES.txt")")
    grep -qP '^A\tH\t' "$DIR/CHANGES.txt" || faults+=("export: the hidden attribute of two.bin is not in CHANGES.txt")
    run export-again 3 --sdcard "$A" --sdcard-writes "$J" --sdcard-export-base "$BASE" --sdcard-export-changes "$DIR"   # not empty

    # the round trip: import into a fresh copy of the base
    B="$W/b.img"
    cp --reflink=auto "$BASE" "$B"
    run import 0 --sdcard "$B" --sdcard-import-changes "$DIR"
    for f in one two three; do
        mcopy -i "$B@@$OFF" "::/RESC/$f.bin" "$W/imp-$f.out" 2>/dev/null || faults+=("import: RESC/$f.bin is not on the card")
        cmp -s "$W/src/$f.bin" "$W/imp-$f.out" || faults+=("import: RESC/$f.bin differs from its source")
    done
    attrs=$(mattrib -i "$B@@$OFF" ::/RESC/two.bin 2>/dev/null || true)
    grep -q 'H' <<<"$attrs" || faults+=("import: the hidden attribute was not restored")
    ! mcopy -i "$B@@$OFF" ::/CHANGES.TXT "$W/ct.out" 2>/dev/null || faults+=("import: CHANGES.txt was copied onto the card")

    # importing again replaces, and says so
    run import-again 0 --sdcard "$B" --sdcard-import-changes "$DIR"
    grep -q '  replaced /RESC/one.bin' "$W/import-again.out" || faults+=("import-again: the report does not say replaced: $(head -c 300 "$W/import-again.out")")

    # a directory without CHANGES.txt is refused (2) and the card is byte-identical
    cp -r "$DIR" "$W/export-noct"; rm -f "$W/export-noct/CHANGES.txt"
    before=$(sha "$B")
    run import-noct 2 --sdcard "$B" --sdcard-import-changes "$W/export-noct"
    [[ "$(sha "$B")" == "$before" ]] || faults+=("import-noct: the card changed")

    # with a writes file the image is untouched and the file holds the writes
    C="$W/c.img"; J2="$W/c.jsdw"
    cp --reflink=auto "$BASE" "$C"
    before=$(sha "$C")
    run import-journal 0 --sdcard "$C" --sdcard-writes "$J2" --sdcard-import-changes "$DIR"
    [[ "$(sha "$C")" == "$before" && -s "$J2" ]] || faults+=("import-journal: the image changed, or the writes file is empty")

    # usage errors
    run u-flat 1 --sdcard "$A" --sdcard-writes-flatten "$W/x.img"
    run u-two 1 --sdcard "$A" --sdcard-export-changes "$W/e2" --sdcard-import-changes "$DIR"
    run u-base 1 --sdcard "$A" --sdcard-export-base "$BASE"

    rm -rf "$W"
    if [[ ${#faults[@]} -eq 0 ]]; then
        pass_row " (flatten, export, import and the round trip on real cards, checked with mtools and cmp; CHANGES.txt not copied; replaced reported; no CHANGES.txt refused with the card untouched; into a writes file the image is untouched)"
    else
        fail_row " (${#faults[@]} fault(s) in the rescue verbs)"
        printf '      %s\n' "${faults[@]}"
    fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
