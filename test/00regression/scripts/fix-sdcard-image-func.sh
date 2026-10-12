#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #284 — tools/fix-sdcard-image.sh is the remedy the man page gives for an
# SD image the user supplies with --sdcard. Such an image, like the one jnext
# downloads, carries the 24.11 distribution's 16 KB /MACHINES/NEXT/zx81.rom,
# which the firmware boot menu cannot load. --sdcard-download-force cannot help
# that user (an explicit --sdcard always wins), so the script must make the
# same change jnext's provisioner makes (double_known_zx81_rom): the exact
# 16 KB file becomes itself twice, and any other zx81.rom is left alone.
#
# The known file is NOT in the repo. It is read at run time from this run's
# canonical SD image, which carries it (16 KB on an image of recipe 1, doubled
# on recipe 2), so the row also proves the script's hash literal names the
# file the distribution really ships. The script's literal and the C++
# kZx81Rom16kSha256 are two copies of one number; the row fails if they differ.
#
# Scratch images are small FAT32 volumes at the script's fixed partition offset
# (sector 63), formatted with 512-byte clusters so the script takes its
# no-reformat path: the zx81 step runs the same after either path. They live
# under $RUN_DIR, which the harness's own trap removes (no trap here, GH #153).
# Rows F and G (GH #330) cover the re-cluster path, see below.
if want fix-sdcard-image-func; then
    begin_func fix-sdcard-image-func

    faults=()
    missing=()
    for tool in mcopy mdir mformat minfo mmd sha256sum; do
        command -v "$tool" &>/dev/null || missing+=("$tool")
    done

    if [[ ${#missing[@]} -gt 0 ]]; then
        fail_row " (tools absent: ${missing[*]} — install mtools)"
    else
    export MTOOLS_SKIP_CHECK=1
    FIX="$PROJECT_DIR/tools/fix-sdcard-image.sh"
    W="$RUN_DIR/private/fix-sdcard-image"
    rm -rf "$W"; mkdir -p "$W"
    OFF=32256   # the script's PART_OFFSET: sector 63

    # --- 1: the script's hash literal is the C++ one ---
    script_sha=$(grep -oE '^ZX81_16K_SHA256=[0-9a-f]{64}$' "$FIX" | cut -d= -f2 || true)
    cpp_sha=$(grep -A1 'kZx81Rom16kSha256 =' "$PROJECT_DIR/src/core/sdcard_provisioner.cpp" \
              | grep -oE '[0-9a-f]{64}' || true)
    [[ $(wc -w <<< "$script_sha") == 1 && $(wc -w <<< "$cpp_sha") == 1 ]] \
        || faults+=("could not read exactly one hash literal from the script ('$script_sha') and the C++ ('$cpp_sha')")
    [[ "$script_sha" == "$cpp_sha" ]] \
        || faults+=("the script's ZX81_16K_SHA256 ($script_sha) is not kZx81Rom16kSha256 ($cpp_sha)")

    # --- 2: the known 16 KB file, from this run's canonical image ---
    CARD="$RUN_DIR/sdcard/cspect-next-1gb-fixed.img"
    part_lba=$(od -An -tu4 -j $((0x1BE + 8)) -N4 "$CARD" 2>/dev/null | tr -d ' ' || true)
    KNOWN="$W/known.rom"
    rm -f "$W/card-zx81.rom"
    LANG=C mcopy -n -i "$CARD@@$(( ${part_lba:-0} * 512 ))" ::/machines/next/zx81.rom \
        "$W/card-zx81.rom" </dev/null 2>/dev/null || true
    card_size=$(wc -c < "$W/card-zx81.rom" 2>/dev/null || echo 0)
    if [[ "$card_size" == 32768 ]] \
       && cmp -s <(head -c 16384 "$W/card-zx81.rom") <(tail -c 16384 "$W/card-zx81.rom"); then
        head -c 16384 "$W/card-zx81.rom" > "$KNOWN"
    elif [[ "$card_size" == 16384 ]]; then
        cp "$W/card-zx81.rom" "$KNOWN"
    else
        : > "$KNOWN"
    fi
    known_sha=$(LC_ALL=C sha256sum "$KNOWN" | cut -d' ' -f1)
    [[ -n "$cpp_sha" && "$known_sha" == "$cpp_sha" ]] \
        || faults+=("this run's SD image does not carry the 24.11 zx81.rom (size $card_size, 16 KB page sha $known_sha)")

    # mkimg <img> [zx81-content] — a scratch FAT32 volume at the script's
    # offset with /machines/next/48.rom and, when given, zx81.rom.
    mkimg() {
        rm -f "$1"
        truncate -s 48M "$1"
        LANG=C mformat -i "$1@@$OFF" -F -T $(( (48 * 1024 * 1024 - OFF) / 512 )) \
            -h 255 -s 63 -c 1 :: </dev/null >/dev/null 2>&1 || return 1
        LANG=C mmd -i "$1@@$OFF" ::/machines ::/machines/next </dev/null 2>/dev/null || return 1
        head -c 16384 /dev/zero | tr '\0' 'H' > "$W/48.rom"
        LANG=C mcopy -i "$1@@$OFF" "$W/48.rom" ::/machines/next/48.rom </dev/null || return 1
        [[ -z "${2:-}" ]] || LANG=C mcopy -i "$1@@$OFF" "$2" ::/machines/next/zx81.rom </dev/null
    }
    # run_fix <label> <src> <dst> — run the script in copy mode; its exit
    # status must be 0. Its output goes to $W/<label>.log, whose last line is
    # quoted on failure ($RUN_DIR does not outlive the run).
    run_fix() {
        local rc=0
        LANG=C timeout --kill-after=5s 120s bash "$FIX" "$2" "$3" > "$W/$1.log" 2>&1 || rc=$?
        [[ $rc == 0 ]] || faults+=("$1: the script exited $rc: $(tail -n1 "$W/$1.log")")
    }
    # zx81_of <img> <out> — extract zx81.rom; fails when it is absent.
    zx81_of() {
        rm -f "$2"
        LANG=C mcopy -n -i "$1@@$OFF" ::/machines/next/zx81.rom "$2" </dev/null 2>/dev/null
    }

    cat "$KNOWN" "$KNOWN" > "$W/known-x2.rom"
    if ! mkimg "$W/a.img" "$KNOWN"; then
        faults+=("harness: could not build a scratch FAT32 image with mtools")
    else
        # A — the known 16 KB file becomes the file twice; the source image
        #     (copy mode) and the sibling ROM are untouched.
        run_fix A "$W/a.img" "$W/a-fixed.img"
        zx81_of "$W/a-fixed.img" "$W/a.out" && cmp -s "$W/a.out" "$W/known-x2.rom" \
            || faults+=("A: the known 16 KB zx81.rom did not become 32 KB = the file twice ($(wc -c < "$W/a.out" 2>/dev/null || echo absent) bytes)")
        zx81_of "$W/a.img" "$W/a-src.out" && cmp -s "$W/a-src.out" "$KNOWN" \
            || faults+=("A: copy mode changed the SOURCE image's zx81.rom")
        rm -f "$W/a-48.out"
        LANG=C mcopy -n -i "$W/a-fixed.img@@$OFF" ::/machines/next/48.rom "$W/a-48.out" </dev/null 2>/dev/null || true
        cmp -s "$W/a-48.out" "$W/48.rom" || faults+=("A: the sibling 48.rom changed")
        grep -q "Doubling /MACHINES/NEXT/zx81.rom" "$W/A.log" \
            || faults+=("A: the script did not say it doubled the file")

        # B — idempotent: a second pass over the fixed image changes nothing.
        run_fix B "$W/a-fixed.img" "$W/b-fixed.img"
        zx81_of "$W/b-fixed.img" "$W/b.out" && cmp -s "$W/b.out" "$W/known-x2.rom" \
            || faults+=("B: a second run changed the doubled zx81.rom ($(wc -c < "$W/b.out" 2>/dev/null || echo absent) bytes)")
        grep -q "left as it is" "$W/B.log" || faults+=("B: a second run did not leave the 32 KB file alone")

        # C — a DIFFERENT 16 KB file (one byte off) is left alone.
        cp "$KNOWN" "$W/other.rom"
        printf '\x5a' | dd of="$W/other.rom" bs=1 seek=8000 conv=notrunc status=none
        mkimg "$W/c.img" "$W/other.rom" && run_fix C "$W/c.img" "$W/c-fixed.img"
        zx81_of "$W/c-fixed.img" "$W/c.out" && cmp -s "$W/c.out" "$W/other.rom" \
            || faults+=("C: a different 16 KB zx81.rom was changed")

        # D — another size starting with the known file (a prefix-only check
        #     would accept it) is left alone.
        { cat "$KNOWN"; printf '\0'; } > "$W/longer.rom"
        mkimg "$W/d.img" "$W/longer.rom" && run_fix D "$W/d.img" "$W/d-fixed.img"
        zx81_of "$W/d-fixed.img" "$W/d.out" && cmp -s "$W/d.out" "$W/longer.rom" \
            || faults+=("D: a 16385-byte zx81.rom starting with the known file was changed")

        # E — no zx81.rom: none is created.
        mkimg "$W/e.img" && run_fix E "$W/e.img" "$W/e-fixed.img"
        ! zx81_of "$W/e-fixed.img" "$W/e.out" \
            || faults+=("E: a zx81.rom was created on an image that had none")

        # --- F, G: the RE-CLUSTER path (GH #330) ------------------------------
        # A, B..E take the no-reformat path. A real under-clustered card is
        # re-formatted: the whole tree is extracted with `mcopy -s`, the
        # partition is re-formatted with 8 KB clusters and the tree is copied
        # back. mtools 4.0.49 (charsetConv.c:204 safe_iconv, an inverted errno
        # test) can name the extracted HOST files wrongly (macOS), and the copy
        # back would then write those names onto the card. The script therefore
        # verifies the extraction against the card before it re-formats.
        #
        # The image is sparse: 600 MB, partition at sector 63, 32 KB clusters
        # (-c 64, the shipped under-clustered shape), so CSIZE >= 32 and the
        # script re-clusters to 8 KB (>= 65525 clusters needs about 520 MB).
        # BIG: the tree on that image, laid out on the host first so its card
        # listing can be derived from it. No file name contains "_" (the defect
        # only appends text that starts with "_").
        BIG="$W/big-tree"
        mkdir -p "$BIG/machines/next" "$BIG/sub/nested" "$BIG/empty"
        head -c 16384 /dev/zero | tr '\0' 'H' > "$BIG/machines/next/48.rom"
        head -c 21000 /dev/urandom > "$BIG/sub/nested/A Long Nested Name.bin"
        head -c  4321 /dev/urandom > "$BIG/sub/deep.dat"
        head -c   999 /dev/urandom > "$BIG/top.bin"
        head -c    17 /dev/urandom > "$BIG/.dotfile"
        head -c    33 /dev/urandom > "$BIG/a[1].bin"
        big_files=(); big_want=""
        while IFS= read -r -d '' p; do
            rel=${p#"$BIG"/}
            [[ "$rel" != *_* ]] || faults+=("harness: fixture name '$rel' contains '_'")
            if [[ -d "$p" ]]; then big_want+="::/$rel/"$'\n'
            else big_want+="::/$rel"$'\n'; big_files+=("$rel"); fi
        done < <(find "$BIG" -mindepth 1 \( -type d -o -type f \) -print0)
        # what the script itself adds
        big_want+="::/machines/next/config.ini"$'\n'
        big_want=$(printf '%s' "$big_want" | LC_ALL=C sort)
        mkbig() {   # mkbig <img> — under-clustered card holding $BIG
            local sz=$((600 * 1024 * 1024)) rel d
            rm -f "$1"; truncate -s "$sz" "$1"
            LANG=C mformat -i "$1@@$OFF" -F -T $(( (sz - OFF) / 512 )) -h 255 -s 63 -c 64 :: \
                </dev/null >/dev/null 2>&1 || return 1
            while IFS= read -r -d '' d; do
                LANG=C mmd -i "$1@@$OFF" "::/${d#"$BIG"/}" </dev/null >/dev/null 2>&1 || return 1
            done < <(find "$BIG" -mindepth 1 -type d -print0 | LC_ALL=C sort -z)
            for rel in "${big_files[@]}"; do
                LANG=C mcopy -i "$1@@$OFF" "$BIG/$rel" "::/$rel" </dev/null >/dev/null 2>&1 || return 1
            done
        }
        big_listing() {   # big_listing <img> — sorted `mdir -/ -a -b` of the card
            LANG=C mdir -/ -a -b -i "$1@@$OFF" :: </dev/null 2>/dev/null | LC_ALL=C sort
        }
        if ! mkbig "$W/f.img"; then
            faults+=("harness: could not build the under-clustered scratch card")
        else
            # F — CONTROL: no defect, the card is re-clustered and the tree
            #     survives (also the first coverage of the re-cluster path).
            run_fix F "$W/f.img" "$W/f-fixed.img"
            f_got=$(big_listing "$W/f-fixed.img") || f_got=""
            if [[ "$f_got" != "$big_want" ]]; then
                f_diff=$(diff <(printf '%s\n' "$big_want") <(printf '%s\n' "$f_got") | head -6 | tr '\n' ';') || true
                faults+=("F: the re-clustered card lists a different tree: $f_diff")
            fi
            for rel in "${big_files[@]}"; do
                rm -f "$W/f.out"
                LANG=C mcopy -n -i "$W/f-fixed.img@@$OFF" "::/${rel//\[/\\[}" "$W/f.out" </dev/null 2>/dev/null \
                    && cmp -s "$W/f.out" "$BIG/$rel" \
                    || faults+=("F: '$rel' is missing or has different bytes after the re-cluster")
            done
            f_cs=$(LANG=C minfo -i "$W/f-fixed.img@@$OFF" :: </dev/null 2>/dev/null \
                   | sed -n 's/.*-c \([0-9][0-9]*\).*/\1/p' | head -1)
            [[ "$f_cs" == 16 ]] || faults+=("F: the re-clustered card has -c '$f_cs', not 16 (8 KB clusters)")
            rm -f "$W/f-fixed.img" "$W/f.out"

            # G — a defect-mimicking mcopy: after the bulk extraction it
            #     renames one host file (G-a) or drops one (G-b: the card has
            #     an entry no host file stands for). The script must refuse
            #     BEFORE it re-formats, with the source and the copy untouched.
            real_mcopy=$(command -v mcopy)
            mkdir -p "$W/wrapbin"
            cat > "$W/wrapbin/mcopy" <<WRAP
#!/bin/sh
"$real_mcopy" "\$@" || exit \$?
case " \$* " in *" -s "*) ;; *) exit 0 ;; esac
for last; do :; done
case "\$last" in :*) exit 0 ;; esac
case "\$GH330_MODE" in
  rename) mv "\${last}top.bin" "\${last}top.bin_file" ;;
  drop)   rm -f "\${last}top.bin" ;;
esac
WRAP
            chmod +x "$W/wrapbin/mcopy"
            for mode in rename drop; do
                rc=0
                GH330_MODE=$mode PATH="$W/wrapbin:$PATH" LANG=C timeout --kill-after=5s 120s \
                    bash "$FIX" "$W/f.img" "$W/g-$mode.img" > "$W/G-$mode.log" 2>&1 || rc=$?
                [[ $rc != 0 ]] || faults+=("G-$mode: the script accepted a mis-named extraction (exit 0)")
                grep -q "GH #330" "$W/G-$mode.log" \
                    || faults+=("G-$mode: no GH #330 refusal in the output: $(tail -n1 "$W/G-$mode.log")")
                [[ $mode != rename ]] || grep -q "top.bin_file" "$W/G-$mode.log" \
                    || faults+=("G-rename: the refusal does not name the bad entry")
                ! grep -q "Reformatting partition" "$W/G-$mode.log" \
                    || faults+=("G-$mode: the partition was re-formatted before the refusal")
                cmp -s "$W/f.img" "$W/g-$mode.img" \
                    || faults+=("G-$mode: the destination image was changed by a refused run")
                rm -f "$W/g-$mode.img"
            done
        fi
        rm -f "$W/f.img" "$W/f-fixed.img"

        # H — tools/extract-sdcard-nex.sh --with-siblings (same exposure, GH
        #     #330): a NEX in the card ROOT used to copy the whole card into
        #     the PARENT of the output directory. Only its siblings, in the
        #     output directory, are right.
        XN="$RUN_DIR/private/fix-sdcard-image/xn"
        mkdir -p "$XN"
        if mkimg "$XN/c.img" && LANG=C mmd -i "$XN/c.img@@$OFF" ::/GAMES </dev/null >/dev/null 2>&1 \
           && LANG=C mcopy -i "$XN/c.img@@$OFF" "$W/48.rom" ::/b.nex </dev/null \
           && LANG=C mcopy -i "$XN/c.img@@$OFF" "$W/48.rom" ::/readme.txt </dev/null \
           && LANG=C mcopy -i "$XN/c.img@@$OFF" "$W/48.rom" ::/GAMES/a.nex </dev/null \
           && LANG=C mcopy -i "$XN/c.img@@$OFF" "$W/48.rom" ::/GAMES/data.bin </dev/null; then
            LANG=C timeout --kill-after=5s 120s bash "$PROJECT_DIR/tools/extract-sdcard-nex.sh" \
                "$XN/out" -s -i "$XN/c.img" > "$XN/H.log" 2>&1 \
                || faults+=("H: extract-sdcard-nex.sh --with-siblings exited non-zero: $(tail -n1 "$XN/H.log")")
            for f in b.nex readme.txt GAMES/a.nex GAMES/data.bin; do
                [[ -f "$XN/out/$f" ]] || faults+=("H: '$f' was not extracted into the output directory")
            done
            [[ ! -e "$XN/GAMES" && ! -e "$XN/b.nex" && ! -e "$XN/readme.txt" ]] \
                || faults+=("H: files were written to the PARENT of the output directory")
        else
            faults+=("harness: could not build the extract-sdcard-nex scratch card")
        fi
        rm -rf "$XN"
    fi

    rm -rf "$W"

    if [[ ${#faults[@]} -eq 0 ]]; then
        pass_row " (the 24.11 16 KB zx81.rom becomes itself twice; a re-run, another 16 KB file, another size and no file are left alone; an under-clustered card is re-clustered with its tree intact, and a mis-named extraction is refused before the re-format)"
    else
        fail_row " (${#faults[@]} fault(s) in tools/fix-sdcard-image.sh)"
        printf '      %s\n' "${faults[@]}"
    fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
