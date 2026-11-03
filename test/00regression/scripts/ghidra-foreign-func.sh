#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable -- the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# ghidra-foreign-func (GH #389, E23-26): jnext's Ghidra symbol files are read by GHIDRA 12.2
# itself -- its own ImportSymbolsScript.py, run by analyzeHeadless (provisioned by
# test/provision-ghidra.sh, `make ghidra`, which `make regression` runs; it needs a JDK 25,
# java-25-openjdk-devel) -- and what Ghidra made of them is read back with a helper that only
# lists the labels. The script was found by RUNNING it to stop at the first line whose address
# space the program lacks (NullPointerException; Ghidra still exits 0), which is why the export
# is one file per space and why this row pins that behaviour as its control.
#
# THE RUN, one analyzeHeadless: a 64K zero image as `z80:LE:16:default`; MakeOverlay.py creates
# the overlay P04 at ram:0x8000 (the user's step before importing a page's file);
# ImportSymbolsScript.py reads P04.ghidra.txt and then view.ghidra.txt; DumpLabels.py lists the
# labels. Every P04 line must be `LABEL n P04::<hex>` and every view line `LABEL n ram:<hex>`,
# nothing else, and no Exception or Traceback before the control starts. The CONTROL, after
# a mark in the log: a file whose second line names a space the program lacks (P1E) ends in the
# NullPointerException and has created only its first label.
# Ghidra's user directories are under $TMP_DIR (HOME, XDG_*, java.io.tmpdir), nothing in ~.
if want ghidra-foreign-func; then
    begin_func ghidra-foreign-func

    gf_dir="$TMP_DIR/ghidra-foreign"
    rm -rf "$gf_dir"; mkdir -p "$gf_dir/home" "$gf_dir/cache" "$gf_dir/tmp" "$gf_dir/proj"
    gf_faults=()
    gf_root=$(bash "$PROJECT_DIR/test/provision-ghidra.sh" --print-path) || gf_root=""
    gf_scripts="$PROJECT_DIR/test/00regression/ghidra"
    gf_nex="$PROJECT_DIR/test/00regression/nex/dsl_demo.nex"
    gf_map="$PROJECT_DIR/test/00regression/nex/dsl_demo.map"
    gf_java_major=$(env -i PATH="$PATH" LANG=C LC_ALL=C java -version 2>&1 | sed -n '1s/^[^"]*"\([0-9]*\).*/\1/p') || gf_java_major=""

    if [[ -z "$gf_root" || ! -x "$gf_root/support/analyzeHeadless" ]]; then
        fail_row " (Ghidra is not provisioned: run 'make ghidra' ('make regression' does); a missing reader is a FAIL, not a SKIP)"
    elif [[ -z "$gf_java_major" || "$gf_java_major" -lt 25 ]]; then
        fail_row " (Ghidra 12.2 needs a JDK 25 or newer, found '${gf_java_major:-none}': install java-25-openjdk-devel)"
    else
        timeout --foreground --kill-after=5s 90s "$JNEXT" --headless "${SD_CARD_ARGS[@]}" --machine next \
            --load "$gf_nex" --map "$gf_map" --coverage --export-ghidra "$gf_dir/G" \
            --delayed-automatic-exit-frames 100 >"$gf_dir/jnext.log" 2>&1 || true
        if [[ ! -s "$gf_dir/G/P04.ghidra.txt" || ! -s "$gf_dir/G/view.ghidra.txt" ]]; then
            gf_faults+=("jnext wrote no P04.ghidra.txt/view.ghidra.txt: $(ls "$gf_dir/G" 2>&1 | tr '\n' ' ' || true)")
        else
            # GH #349: z88dk module-local labels repeat (`end_x` in two library routines). The export writes a
            # name once per file, and jnext's own importer (which refuses a name at two addresses) reads both files.
            cp "$gf_map" "$gf_dir/dup.map"
            printf '%s\n' "end_x                           = \$80CA ; addr, local, , asm_zx_cls_wc_attr, code_user, dsl_demo.asm:90" \
                "end_x                           = \$8103 ; addr, local, , asm_zx_cls_wc_pix, code_user, dsl_demo.asm:120" >>"$gf_dir/dup.map"
            timeout --foreground --kill-after=5s 90s "$JNEXT" --headless "${SD_CARD_ARGS[@]}" --machine next \
                --load "$gf_nex" --map "$gf_dir/dup.map" --coverage --export-ghidra "$gf_dir/G2" \
                --delayed-automatic-exit-frames 100 >"$gf_dir/jnext-dup.log" 2>&1 || true
            if [[ $(grep -c '^end_x ' "$gf_dir/G2/P04.ghidra.txt" || true) -ne 1 ||
                  $(grep -c '^end_x ' "$gf_dir/G2/view.ghidra.txt" || true) -ne 1 ]]; then
                gf_faults+=("a duplicated label name is not written exactly once in P04.ghidra.txt and view.ghidra.txt")
            fi
            for f in P04 view; do
                rc=0
                timeout --foreground --kill-after=5s 90s "$JNEXT" --headless "${SD_CARD_ARGS[@]}" --machine next \
                    --load "$gf_nex" --import-ghidra "$gf_dir/G2/$f.ghidra.txt" --delayed-automatic-exit-frames 20 \
                    >"$gf_dir/jnext-imp-$f.log" 2>&1 || rc=$?
                [[ $rc -eq 0 ]] || gf_faults+=("--import-ghidra of the duplicate-name export's $f file exit $rc")
            done

            # The control: two valid labels around a space the program lacks.
            printf '%s\n' "ctl_first P04:0x8010 l" "ctl_second P1E:0x8010 l" "ctl_third P04:0x8018 l" >"$gf_dir/control.txt"
            head -c 65536 /dev/zero >"$gf_dir/zero.bin"
            rc=0
            env -i PATH="$PATH" LANG=C LC_ALL=C HOME="$gf_dir/home" XDG_CONFIG_HOME="$gf_dir/home/.config" \
                XDG_CACHE_HOME="$gf_dir/cache" JAVA_TOOL_OPTIONS="-Djava.io.tmpdir=$gf_dir/tmp" \
                timeout --foreground --kill-after=10s 400s "$gf_root/support/analyzeHeadless" \
                "$gf_dir/proj" row -import "$gf_dir/zero.bin" -processor "z80:LE:16:default" \
                -loader BinaryLoader -noanalysis -deleteProject \
                -scriptPath "$gf_scripts;$gf_root/Ghidra/Extensions/Jython/ghidra_scripts" \
                -postScript MakeOverlay.py P04 0x8000 0x2000 \
                -postScript ImportSymbolsScript.py "$gf_dir/G/P04.ghidra.txt" \
                -postScript ImportSymbolsScript.py "$gf_dir/G/view.ghidra.txt" \
                -postScript DumpLabels.py positive \
                -postScript Mark.py control \
                -postScript ImportSymbolsScript.py "$gf_dir/control.txt" \
                -postScript DumpLabels.py after-control \
                >"$gf_dir/ghidra.log" 2>&1 || rc=$?
            [[ $rc -eq 0 ]] || gf_faults+=("analyzeHeadless exit $rc")

            # Split the log at the mark: what came before is the real import.
            awk '/^.*JNEXT-ROW MARK control/ {exit} {print}' "$gf_dir/ghidra.log" >"$gf_dir/log.main"
            awk 'f {print} /JNEXT-ROW MARK control/ {f=1}' "$gf_dir/ghidra.log" >"$gf_dir/log.control"
            if grep -qE 'Exception|Traceback' "$gf_dir/log.main"; then
                gf_faults+=("the real import logged an exception: $(grep -E 'Exception|Traceback' "$gf_dir/log.main" | head -n 1 || true)")
            fi
            if ! grep -q 'NullPointerException' "$gf_dir/log.control"; then
                gf_faults+=("control: a space the program lacks no longer aborts the script (D-G1 is obsolete?)")
            fi

            # Expected: every line of the two files, in Ghidra's spelling.
            sed -E 's/^(\S+) (P[0-9A-Fa-f]{2}):0x([0-9A-Fa-f]{4}) l$/\1 \2::\L\3/' "$gf_dir/G/P04.ghidra.txt" >"$gf_dir/want.p04"
            sed -E 's/^(\S+) 0x([0-9A-Fa-f]{4}) l$/\1 ram:\L\2/' "$gf_dir/G/view.ghidra.txt" >"$gf_dir/want.view"
            sed -n '/JNEXT-ROW DUMP positive/,/JNEXT-ROW MARK control/{s/^JNEXT-ROW LABEL //p}' "$gf_dir/ghidra.log" \
                | sort >"$gf_dir/got.positive"
            cat "$gf_dir/want.p04" "$gf_dir/want.view" | sort >"$gf_dir/want.positive"
            if ! cmp -s "$gf_dir/want.positive" "$gf_dir/got.positive"; then
                gf_faults+=("Ghidra's labels differ from the files' lines: $(diff "$gf_dir/want.positive" "$gf_dir/got.positive" | head -n 4 | tr '\n' ' ' || true)")
            fi
            [[ -s "$gf_dir/want.p04" && -s "$gf_dir/want.view" ]] || gf_faults+=("an empty export file")
            # The control created the first label and none after the abort.
            sed -n '/JNEXT-ROW DUMP after-control/,$ s/^JNEXT-ROW LABEL //p' "$gf_dir/ghidra.log" | sort >"$gf_dir/got.control"
            if [[ $(comm -13 "$gf_dir/got.positive" "$gf_dir/got.control" | tr '\n' ',') != "ctl_first P04::8010," ]]; then
                gf_faults+=("control: expected only ctl_first to be created, got: $(comm -13 "$gf_dir/got.positive" "$gf_dir/got.control" | tr '\n' ',')")
            fi
        fi
    fi

    if [[ ${#gf_faults[@]} -gt 0 ]]; then
        fail_row " ($(IFS=';'; echo "${gf_faults[*]}"))"
    else
        pass_row " (Ghidra 12.2's ImportSymbolsScript places every P04 label in its overlay and every view label in ram, and the space it lacks aborts it: $(wc -l <"$gf_dir/want.positive") labels)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
