#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #269 — `--sdcard-file-add FILE --sdcard-file-dest PATH` copies a host file
# into the FAT32 partition of the SD image and exits without emulating. GH #292
# extended it to whole directories, and made --sdcard-file-dest optional (no
# dest = the card root).
#
# THIS ROW IS THE ACCEPTANCE TEST, and the reason is specific: a filesystem
# writer validated only by its own reader is worthless. jnext shipped a .szx
# writer that round-tripped through its own loader perfectly and that real FUSE
# could not open. So nothing below reads the card with jnext's code. Every
# content assertion goes through an implementation this project did not write:
#
#   fsck.vfat  (dosfstools)  — is the FILESYSTEM still structurally sound?
#   mdir       (mtools)      — is the DIRECTORY ENTRY there, with the name asked
#                              for, under a foreign name/LFN decoder?
#   mcopy      (mtools)      — are the FILE BYTES identical coming back out?
#
# The unit suite (sdcard_file_add_test) covers the decisions — path validation,
# directory creation, the refuse-then-force policy, status codes — on small
# synthetic images with 512-byte clusters. This row covers the one thing it
# cannot: that the bytes written to a REAL 1 GB NextZXOS card, at its 8 KB
# cluster size, are FAT32 that other software accepts.
#
# Missing tools are a FAILURE, not a skip. A row that silently stops doing the
# foreign validation is the whole defect this row exists to prevent; CI installs
# mtools and dosfstools for exactly this reason.
if want sdcard-file-add-func; then
    begin_func sdcard-file-add-func

    faults=()
    missing=()
    for tool in mdir mcopy fsck.vfat; do
        command -v "$tool" &>/dev/null || missing+=("$tool")
    done

    if [[ ${#missing[@]} -gt 0 ]]; then
        fail_row " (foreign validators absent: ${missing[*]} — install mtools and dosfstools)"
    else
    # mtools refuses an image whose geometry it cannot infer; the partition is
    # addressed by byte offset via the @@ syntax, so that check is noise here.
    export MTOOLS_SKIP_CHECK=1

    # A PRIVATE card for this row, and a scratch area beside it. Same shape the
    # screenshot suite's `@private-sd` sentinel uses: a reflink clone of THIS
    # RUN's image inside $RUN_DIR, with $JNEXT_CONFIG_DIR pointed at it. Two
    # reasons it lives under $RUN_DIR and not $TMP_DIR: reflink only works
    # within one filesystem, and the partition extracted below for fsck.vfat is
    # a gigabyte — $TMP_DIR is tmpfs, i.e. RAM. The harness's own EXIT trap
    # removes $RUN_DIR; this row installs no trap of its own (GH #153).
    CFG="$RUN_DIR/private/sdcard-file-add"
    W="$CFG/work"
    mkdir -p "$CFG/sdcard" "$W"
    cp --reflink=auto "$RUN_DIR/sdcard/cspect-next-1gb-fixed.img" \
                      "$CFG/sdcard/cspect-next-1gb-fixed.img"
    CARD="$CFG/sdcard/cspect-next-1gb-fixed.img"

    # The partition's byte offset, READ from the MBR rather than assumed: mtools
    # needs it for @@, and fsck.vfat has no offset option at all, so the
    # partition is extracted to its own file before checking.
    part_lba=$(od -An -tu4 -j $((0x1BE + 8)) -N4 "$CARD" | tr -d ' ')
    part_sec=$(od -An -tu4 -j $((0x1BE + 12)) -N4 "$CARD" | tr -d ' ')
    OFF=$((part_lba * 512))
    if [[ ${part_lba:-0} -le 0 || ${part_sec:-0} -le 0 ]]; then
        faults+=("harness: could not read the MBR partition entry of the card")
        OFF=0
    fi

    fsck_card() {   # fsck_card <label> — extract the partition and check it
        rm -f "$W/part.img"
        # conv=sparse keeps the extracted gigabyte from actually occupying one:
        # the card is mostly holes and stays that way.
        dd if="$CARD" of="$W/part.img" bs=1M conv=sparse \
           skip="$OFF" count=$((part_sec * 512)) \
           iflag=skip_bytes,count_bytes status=none
        local rc=0
        fsck.vfat -n -V "$W/part.img" > "$W/fsck-$1.log" 2>&1 || rc=$?
        if [[ $rc -ne 0 ]]; then
            faults+=("$1: fsck.vfat exited $rc — the filesystem is damaged")
        fi
        # fsck.vfat can exit 0 having printed a complaint, so the log is read
        # too ("Free cluster summary wrong", "Dirty bit", a bad cluster chain).
        local why
        why=$(grep -iE 'wrong|corrupt|invalid|unterminated|truncat|mismatch|dirty bit' \
                   "$W/fsck-$1.log" | head -1) || why=""
        [[ -z "$why" ]] || faults+=("$1: fsck.vfat complained: $why")
        rm -f "$W/part.img"
    }

    # CONTROL. If the pristine card does not already pass fsck, every "fsck
    # passes" claim below is vacuous.
    fsck_card before

    # Distinct payloads per file, so reading back the wrong one is a failure and
    # not a coincidence. Random rather than patterned for the same reason.
    head -c 194816 /dev/urandom > "$W/drv-a.dsk"
    head -c   9999 /dev/urandom > "$W/long.bin"
    head -c  33333 /dev/urandom > "$W/deep.bin"
    head -c  12345 /dev/urandom > "$W/replacement.dsk"

    ADD_OUT=""
    ADD_RUNS=0
    run_add() {   # run_add <expected-exit> <label> [jnext args...]
        local want_rc=$1 label=$2 rc=0; shift 2
        ADD_RUNS=$((ADD_RUNS + 1))
        ADD_OUT=$(JNEXT_CONFIG_DIR="$CFG" timeout --foreground --kill-after=5s 120s \
            "$JNEXT" "$@" 2>&1) || rc=$?
        [[ "$rc" == "$want_rc" ]] \
            || faults+=("$label: expected exit $want_rc, got $rc")
    }

    # --- 1. the DRV-A.DSK case the feature exists for ------------------------
    run_add 0 "add-dsk" --sdcard-file-add "$W/drv-a.dsk" \
                        --sdcard-file-dest /NEXTZXOS/DRV-A.DSK
    grep -qF "Copied '$W/drv-a.dsk' to '/NEXTZXOS/DRV-A.DSK'" <<< "$ADD_OUT" \
        || faults+=("the successful copy did not report what it copied where")
    # A copy-and-exit mode must not boot anything. These two lines are printed
    # by every run that constructs the machine, and by no run that does not.
    # (Matching on "NextZXOS" instead would match this row's own destination
    # path, /NEXTZXOS/DRV-A.DSK, echoed in the success line.)
    grep -qE 'Initializing emulator|Headless mode initialized' <<< "$ADD_OUT" \
        && faults+=("--sdcard-file-add appears to have started emulating")

    # An 8.3-clean uppercase name must land as a PLAIN short entry: NextZXOS
    # finds DRV-A.DSK by short name, so a generated DRV-A~1.DSK would arrive on
    # the card and never be mounted. mdir prints a long name in a trailing
    # column only when one exists, so a bare "DRV-A    DSK" line is the proof.
    mdir -i "$CARD@@$OFF" ::/NEXTZXOS > "$W/mdir-nextzxos.txt" 2>&1 \
        || faults+=("mdir could not list /NEXTZXOS")
    grep -qE '^DRV-A +DSK +194816 ' "$W/mdir-nextzxos.txt" \
        || faults+=("mdir does not show DRV-A.DSK as a 194816-byte 8.3 entry")
    grep -qE '^DRV-A +DSK.*DRV-A' "$W/mdir-nextzxos.txt" \
        && faults+=("DRV-A.DSK was written with a long-name entry it does not need")
    mcopy -i "$CARD@@$OFF" ::/NEXTZXOS/DRV-A.DSK "$W/back-drv-a.dsk" 2>/dev/null \
        || faults+=("mcopy could not read /NEXTZXOS/DRV-A.DSK back")
    cmp -s "$W/drv-a.dsk" "$W/back-drv-a.dsk" \
        || faults+=("mcopy read back DIFFERENT bytes from /NEXTZXOS/DRV-A.DSK")

    # --- 2. a long file name -------------------------------------------------
    run_add 0 "add-lfn" --sdcard-file-add "$W/long.bin" \
                        --sdcard-file-dest "/A Long Demo Name.nex"
    mdir -i "$CARD@@$OFF" :: > "$W/mdir-root.txt" 2>&1 \
        || faults+=("mdir could not list the card root")
    grep -qF "A Long Demo Name.nex" "$W/mdir-root.txt" \
        || faults+=("mtools does not see the long name 'A Long Demo Name.nex'")
    mcopy -i "$CARD@@$OFF" "::/A Long Demo Name.nex" "$W/back-long.bin" 2>/dev/null \
        || faults+=("mcopy could not read the long-named file back")
    cmp -s "$W/long.bin" "$W/back-long.bin" \
        || faults+=("mcopy read back DIFFERENT bytes from the long-named file")

    # --- 3. directories that did not exist ------------------------------------
    run_add 0 "add-deep" --sdcard-file-add "$W/deep.bin" \
                         --sdcard-file-dest /JNEXTGH269/SUB/DEEP.BIN
    mcopy -i "$CARD@@$OFF" ::/JNEXTGH269/SUB/DEEP.BIN "$W/back-deep.bin" 2>/dev/null \
        || faults+=("mcopy could not read the file from the created directories")
    cmp -s "$W/deep.bin" "$W/back-deep.bin" \
        || faults+=("mcopy read back DIFFERENT bytes from the created directories")

    # --- 4. the overwrite policy ---------------------------------------------
    run_add 4 "refuse-overwrite" --sdcard-file-add "$W/replacement.dsk" \
                                 --sdcard-file-dest /NEXTZXOS/DRV-A.DSK
    rm -f "$W/after-refusal.dsk"
    mcopy -i "$CARD@@$OFF" ::/NEXTZXOS/DRV-A.DSK "$W/after-refusal.dsk" 2>/dev/null \
        || faults+=("mcopy could not read DRV-A.DSK after the refused overwrite")
    cmp -s "$W/drv-a.dsk" "$W/after-refusal.dsk" \
        || faults+=("the refused overwrite CHANGED the file on the card")
    run_add 0 "force-overwrite" --sdcard-file-add "$W/replacement.dsk" \
                                --sdcard-file-dest /NEXTZXOS/DRV-A.DSK \
                                --sdcard-file-force
    rm -f "$W/after-force.dsk"
    mcopy -i "$CARD@@$OFF" ::/NEXTZXOS/DRV-A.DSK "$W/after-force.dsk" 2>/dev/null \
        || faults+=("mcopy could not read DRV-A.DSK after the forced overwrite")
    cmp -s "$W/replacement.dsk" "$W/after-force.dsk" \
        || faults+=("--sdcard-file-force did not replace the file's contents")

    # --- 4b. a whole directory tree (GH #292) ---------------------------------
    # Nested directories, an EMPTY one, long names, a dot-file, a multi-cluster
    # file. mcopy -s reads the whole tree back with mtools' own FAT driver, and
    # diff -r compares it with the source — names, nesting and bytes at once.
    T="$W/tree"
    mkdir -p "$T/sub/nested" "$T/sub/empty"
    head -c 70000 /dev/urandom > "$T/sub/nested/A Long Nested Name.bin"
    head -c  4321 /dev/urandom > "$T/sub/deep.dat"
    head -c   999 /dev/urandom > "$T/top.bin"
    head -c    17 /dev/urandom > "$T/.dotfile"
    : > "$T/zero.bin"
    run_add 0 "add-tree" --sdcard-file-add "$T" --sdcard-file-dest /JNEXTGH292
    grep -qF "Copied directory '$T' to '/JNEXTGH292'" <<< "$ADD_OUT" \
        || faults+=("the tree copy did not report what it copied where")
    rm -rf "$W/back-tree"
    mcopy -s -n -i "$CARD@@$OFF" ::/JNEXTGH292 "$W/back-tree" 2>/dev/null \
        || faults+=("mcopy -s could not read the copied tree back")
    diff -r "$T" "$W/back-tree" > "$W/tree-diff.txt" 2>&1 \
        || faults+=("mtools reads back a DIFFERENT tree: $(head -1 "$W/tree-diff.txt")")
    # A refusal inside a tree leaves nothing behind: the FIFO is found before
    # anything is written, so the good file sorted ahead of it is not on the
    # card either.
    B="$W/badtree"
    mkdir -p "$B/sub"
    head -c 100 /dev/urandom > "$B/a-first.bin"
    mkfifo "$B/sub/pipe"
    run_add 2 "tree-with-fifo" --sdcard-file-add "$B" --sdcard-file-dest /JNEXTGH292BAD
    mdir -i "$CARD@@$OFF" ::/JNEXTGH292BAD > /dev/null 2>&1 \
        && faults+=("a refused tree copy left /JNEXTGH292BAD on the card")
    rm -f "$B/sub/pipe"

    # --- 4c. no --sdcard-file-dest: the card root (GH #292) ---------------------
    mkdir -p "$W/jnextgh292root"
    head -c 555 /dev/urandom > "$W/jnextgh292root/inroot.bin"
    run_add 0 "dir-no-dest" --sdcard-file-add "$W/jnextgh292root"
    rm -f "$W/back-inroot.bin"
    mcopy -n -i "$CARD@@$OFF" ::/jnextgh292root/inroot.bin "$W/back-inroot.bin" 2>/dev/null \
        || faults+=("a directory with no dest did not land in the card root")
    cmp -s "$W/jnextgh292root/inroot.bin" "$W/back-inroot.bin" \
        || faults+=("the directory copied to the root reads back DIFFERENT bytes")
    head -c 444 /dev/urandom > "$W/gh292file.bin"
    run_add 0 "file-no-dest" --sdcard-file-add "$W/gh292file.bin"
    grep -qF "Copied '$W/gh292file.bin' to '/gh292file.bin'" <<< "$ADD_OUT" \
        || faults+=("a file with no dest did not report landing at /gh292file.bin")
    rm -f "$W/back-gh292file.bin"
    mcopy -n -i "$CARD@@$OFF" ::/gh292file.bin "$W/back-gh292file.bin" 2>/dev/null \
        || faults+=("a file with no dest did not land in the card root")
    cmp -s "$W/gh292file.bin" "$W/back-gh292file.bin" \
        || faults+=("the file copied to the root reads back DIFFERENT bytes")
    # The pair may come in either order.
    run_add 0 "dest-before-add" --sdcard-file-dest /JNEXTGH292/ORDER.BIN \
                                --sdcard-file-add "$W/gh292file.bin"
    mcopy -n -i "$CARD@@$OFF" ::/JNEXTGH292/ORDER.BIN "$W/back-order.bin" 2>/dev/null \
        || faults+=("--sdcard-file-dest before --sdcard-file-add was not honoured")

    # --- 5. still a sound filesystem, and still a NextZXOS card ---------------
    fsck_card after
    mdir -i "$CARD@@$OFF" ::/MACHINES/NEXT > "$W/mdir-machines.txt" 2>&1 \
        || faults+=("mdir could not list /MACHINES/NEXT after the writes")
    grep -qiF "enNxtmmc.rom" "$W/mdir-machines.txt" \
        || faults+=("enNxtmmc.rom vanished from /MACHINES/NEXT after the writes")
    # Case-insensitively: the distribution stores these with the FAT "name is
    # lowercase" flags set, so mdir prints `48       rom`, not `48  ROM`.
    grep -qiE '^48 +rom +16384 ' "$W/mdir-machines.txt" \
        || faults+=("48.rom vanished from /MACHINES/NEXT after the writes")
    grep -qiE '^TBBLUE +FW +304640 ' "$W/mdir-root.txt" \
        || faults+=("TBBLUE.FW vanished from the card root after the writes")

    # --- 6. the failure statuses a script branches on -------------------------
    run_add 2 "missing-source" --sdcard-file-add "$W/does-not-exist" \
                               --sdcard-file-dest /X.BIN
    run_add 3 "bad-dest"       --sdcard-file-add "$W/long.bin" \
                               --sdcard-file-dest '/BAD:NAME'
    printf 'not an sd image\n' > "$W/junk.img"
    run_add 5 "bad-image"      --sdcard "$W/junk.img" \
                               --sdcard-file-add "$W/long.bin" \
                               --sdcard-file-dest /X.BIN
    run_add 1 "dest-without-add"  --sdcard-file-dest /X.BIN
    # GH #292 — one source per run: a repeat used to drop the first silently.
    run_add 1 "add-twice"         --sdcard-file-add "$W/long.bin" \
                                  --sdcard-file-add "$W/deep.bin"
    run_add 1 "dest-twice"        --sdcard-file-add "$W/long.bin" \
                                  --sdcard-file-dest /X.BIN --sdcard-file-dest /Y.BIN
    run_add 1 "empty-dest"        --sdcard-file-add "$W/long.bin" --sdcard-file-dest ""
    run_add 1 "force-without-add" --sdcard-file-force
    run_add 1 "add-with-readonly" --sdcard-readonly \
                                  --sdcard-file-add "$W/long.bin" \
                                  --sdcard-file-dest /X.BIN
    run_add 1 "add-with-load"     --sdcard-file-add "$W/long.bin" \
                                  --sdcard-file-dest /X.BIN \
                                  --load "$SCRIPT_DIR/nex/celeste.nex"
    run_add 1 "add-with-inject"   --sdcard-file-add "$W/long.bin" \
                                  --sdcard-file-dest /X.BIN \
                                  --inject "$W/long.bin"

    # --- 7. the shared-image warning ------------------------------------------
    # Writing the default-location image is allowed but loud, because every run
    # given no --sdcard boots it. With an explicit --sdcard it must stay quiet,
    # or the warning becomes noise nobody reads.
    run_add 0 "default-warns" --sdcard-file-add "$W/long.bin" \
                              --sdcard-file-dest /WARNCHK.BIN
    grep -q "DEFAULT SD-card image" <<< "$ADD_OUT" \
        || faults+=("writing the default-location image did not warn that it is shared")
    # A SECOND card, at a path that is not the default one, so "explicit" and
    # "default" are genuinely different images — pointing --sdcard at the
    # default path is still the default image, and would still (correctly) warn.
    cp --reflink=auto "$CARD" "$W/explicit.img"
    run_add 0 "explicit-quiet" --sdcard "$W/explicit.img" \
                               --sdcard-file-add "$W/long.bin" \
                               --sdcard-file-dest /WARNCHK2.BIN
    grep -q "DEFAULT SD-card image" <<< "$ADD_OUT" \
        && faults+=("an explicit --sdcard still printed the shared-image warning")
    # The warning must also survive a different SPELLING of the same image. A
    # string comparison passes the two rows above and fails both of these, and
    # the person it fails is the one about to clobber the shared card.
    run_add 0 "default-warns-dotpath" \
        --sdcard "$CFG/sdcard/./cspect-next-1gb-fixed.img" \
        --sdcard-file-add "$W/long.bin" --sdcard-file-dest /WARNCHK3.BIN
    grep -q "DEFAULT SD-card image" <<< "$ADD_OUT" \
        || faults+=("a './' spelling of the default image skipped the shared-image warning")
    ln -sfn "$CARD" "$W/card-link.img"
    run_add 0 "default-warns-symlink" \
        --sdcard "$W/card-link.img" \
        --sdcard-file-add "$W/long.bin" --sdcard-file-dest /WARNCHK4.BIN
    grep -q "DEFAULT SD-card image" <<< "$ADD_OUT" \
        || faults+=("a symlink to the default image skipped the shared-image warning")
    # Both of those really did write the shared card, which is the point of
    # warning about them.
    mcopy -i "$CARD@@$OFF" ::/WARNCHK3.BIN "$W/w3.out" 2>/dev/null \
        || faults+=("the './' spelling did not write the default image after all")
    mcopy -i "$CARD@@$OFF" ::/WARNCHK4.BIN "$W/w4.out" 2>/dev/null \
        || faults+=("the symlink did not write the default image after all")

    # ...and --sdcard really chose that image: the file is in it and not in the
    # default one. Without this, "quiet" would also be satisfied by writing the
    # default image and simply not saying so.
    mcopy -i "$W/explicit.img@@$OFF" ::/WARNCHK2.BIN "$W/back-explicit.bin" 2>/dev/null \
        || faults+=("--sdcard did not write into the image it names")
    mcopy -i "$CARD@@$OFF" ::/WARNCHK2.BIN "$W/leaked.bin" 2>/dev/null \
        && faults+=("--sdcard wrote into the DEFAULT image as well as the one it names")

    rm -rf "$W"

    if [[ ${#faults[@]} -eq 0 ]]; then
        pass_row " (mcopy byte-identical, mdir names exact, fsck.vfat clean, $ADD_RUNS invocations)"
    else
        fail_row " (${#faults[@]} fault(s) writing to the SD image)"
        printf '      %s\n' "${faults[@]}"
    fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
