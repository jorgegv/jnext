#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #292, end to end — a DIRECTORY TREE put on the card by --sdcard-file-add
# is one the emulated NextZXOS can walk.
#
# The guest types `.ls gh292/sub` at the NextZXOS command line: a listing of a
# directory two levels below one jnext created, holding a long-named file
# (shown by its generated 8.3 name), an 8.3 file, a subdirectory and an empty
# directory. Its screen is compared with the screen of the SAME tree written by
# mtools — a FAT driver this project did not write — so the oracle is not
# jnext's own reader:
#
#   jnext   the tree written by --sdcard-file-add     -> baseline
#   mtools  the same tree written by mmd + mcopy      -> must EQUAL jnext
#   alt     jnext again, one file a different size    -> must DIFFER, a little
#
# mtools stamps entries with the current time and .ls prints the date, so the
# mtools card is written under SOURCE_DATE_EPOCH = 2025-06-01, the fixed date
# jnext's FatFs writes (FF_NORTC_*). Equal screens then mean the guest saw the
# same names, sizes, dates and nesting on both cards.
#
# `alt` is what makes `jnext == mtools` mean something. If the guest could not
# reach gh292/sub at all, both would show the same error and still be equal;
# `alt` differs only in one file's size, so it changes the listing ONLY if the
# guest really listed that directory — by a few digits, not a whole screen.
#
# Missing mtools is a FAILURE, not a skip, as in sdcard-file-add-func.
if want sdcard-file-add-tree-func; then
    begin_func sdcard-file-add-tree-func

    faults=()
    missing=()
    for tool in mmd mcopy; do
        command -v "$tool" &>/dev/null || missing+=("$tool")
    done

    if [[ ${#missing[@]} -gt 0 ]]; then
        fail_row " (mtools absent: ${missing[*]})"
    else
    export MTOOLS_SKIP_CHECK=1
    # Same private-card shape as the other SD rows, under $RUN_DIR so the
    # clones are reflinks and the harness's own EXIT trap removes them. This
    # row installs no trap of its own (GH #153).
    BASE="$RUN_DIR/private/sdcard-file-add-tree"
    W="$BASE/work"
    mkdir -p "$W"

    make_tree() {   # make_tree <dir> <size of SHORT.BIN>
        mkdir -p "$1/sub/deeper" "$1/sub/empty"
        head -c "$2" /dev/zero > "$1/sub/SHORT.BIN"
        head -c 777  /dev/zero > "$1/sub/A Long Name.txt"
        head -c 5    /dev/zero > "$1/sub/deeper/x.bin"
    }
    make_tree "$W/tree" 1234
    make_tree "$W/alt"  4321

    new_card() {    # new_card <name> — a private clone; prints its path
        mkdir -p "$BASE/$1/sdcard"
        cp --reflink=auto "$RUN_DIR/sdcard/cspect-next-1gb-fixed.img" \
                          "$BASE/$1/sdcard/cspect-next-1gb-fixed.img"
        printf '%s\n' "$BASE/$1/sdcard/cspect-next-1gb-fixed.img"
    }

    add_tree() {    # add_tree <name> <host tree> — via the feature under test
        local rc=0
        JNEXT_CONFIG_DIR="$BASE/$1" timeout --foreground --kill-after=5s 120s \
            "$JNEXT" --sdcard-file-add "$2" --sdcard-file-dest /GH292 \
            > "$W/add-$1.log" 2>&1 || rc=$?
        [[ $rc -eq 0 ]] || faults+=("$1: --sdcard-file-add exited $rc")
    }

    boot_ls() {     # boot_ls <name> — boot, type `.ls gh292/sub`, shoot
        local rc=0
        # The menu keys are boot-nextzxos-dotls's; '/' is SYMBOL SHIFT + V.
        JNEXT_CONFIG_DIR="$BASE/$1" timeout --foreground --kill-after=5s 180s \
            "$JNEXT" --headless --machine next --rtc "$NEXTZXOS_RTC" \
                     --delayed-keypress-frames 400 space \
                     --delayed-keypress-frames 470 down \
                     --delayed-keypress-frames 500 enter \
                     --delayed-keypress-frames 560 . \
                     --delayed-keypress-frames 575 l \
                     --delayed-keypress-frames 590 s \
                     --delayed-keypress-frames 605 space \
                     --delayed-keypress-frames 620 g \
                     --delayed-keypress-frames 635 h \
                     --delayed-keypress-frames 650 2 \
                     --delayed-keypress-frames 665 9 \
                     --delayed-keypress-frames 680 2 \
                     --delayed-keypress-frames 695 sym+v \
                     --delayed-keypress-frames 710 s \
                     --delayed-keypress-frames 725 u \
                     --delayed-keypress-frames 740 b \
                     --delayed-keypress-frames 760 enter \
                     --delayed-screenshot "$W/$1.png" \
                     --delayed-screenshot-frames 950 \
                     --delayed-automatic-exit-frames 970 \
            > "$W/boot-$1.log" 2>&1 || rc=$?
        [[ $rc -eq 0 ]] || faults+=("$1: the NextZXOS boot exited $rc")
        [[ -f "$W/$1.png" ]] || faults+=("$1: no screenshot was produced")
    }

    # jnext's own copy.
    new_card jnext > /dev/null
    add_tree jnext "$W/tree"
    boot_ls jnext

    # The same tree through mtools, one entry at a time in jnext's order (byte
    # order: uppercase first, a directory before its contents), at jnext's
    # fixed FatFs date. `.ls` sorts its listing anyway; matching the order
    # keeps the two cards' directories alike entry for entry.
    card=$(new_card mtools)
    part_lba=$(od -An -tu4 -j $((0x1BE + 8)) -N4 "$card" | tr -d ' ')
    img="$card@@$((part_lba * 512))"
    if ! SOURCE_DATE_EPOCH=$(date -d '2025-06-01 00:00:00' +%s) bash -c '
            set -e
            mmd   -i "$1" ::/GH292 ::/GH292/sub
            mcopy -i "$1" "$2/sub/A Long Name.txt" ::/GH292/sub/
            mcopy -i "$1" "$2/sub/SHORT.BIN" ::/GH292/sub/
            mmd   -i "$1" ::/GH292/sub/deeper
            mcopy -i "$1" "$2/sub/deeper/x.bin" ::/GH292/sub/deeper/
            mmd   -i "$1" ::/GH292/sub/empty
        ' _ "$img" "$W/tree" > "$W/mtools.log" 2>&1; then
        faults+=("mtools could not write the reference tree: $(head -1 "$W/mtools.log")")
    fi
    boot_ls mtools

    # The sensitivity control.
    new_card alt > /dev/null
    add_tree alt "$W/alt"
    boot_ls alt

    if [[ -f "$W/jnext.png" && -f "$W/mtools.png" && -f "$W/alt.png" ]]; then
        d_mtools=$(png_diff "$W/jnext.png" "$W/mtools.png")
        d_alt=$(png_diff "$W/jnext.png" "$W/alt.png")
        [[ "$d_mtools" -eq 0 ]] \
            || faults+=("the guest lists jnext's tree differently from mtools' ($d_mtools px)")
        if [[ "$d_alt" -eq 0 ]]; then
            faults+=("a different file size did not change the listing — the guest never listed gh292/sub")
        elif [[ "$d_alt" -gt 2000 ]]; then
            faults+=("a different file size changed $d_alt pixels — that is a different screen, not a few digits")
        fi
    else
        faults+=("one or more boots produced no screenshot")
    fi

    if [[ ${#faults[@]} -eq 0 ]]; then
        pass_row " (NextZXOS .ls of the copied tree matches mtools'; size control ${d_alt} px)"
    else
        fail_row " (${#faults[@]} fault(s) reading a copied tree from the guest)"
        printf '      %s\n' "${faults[@]}"
    fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
