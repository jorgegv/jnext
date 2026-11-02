#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable -- the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# skoolkit-foreign-func (GH #389, E23-26): jnext's skoolkit control files are read by
# SKOOLKIT 10.1 itself -- sna2skool.py and skool2asm.py, provisioned by
# test/provision-skoolkit.sh (`make skoolkit`, which `make regression` runs) -- and what
# they make of the files is checked by skoolkit-check.py, never by a reader of ours. A
# control file jnext's own importer accepts can still be wrong for skoolkit (the grammar
# is its, and a bad line is only a WARNING there); this row is the proof it is not.
#
# A1  --machine 48k, an sjasmplus --sym of three ROM labels (unpaged, at instruction
#     starts the run executes), --coverage, a snapshot at frame 100, --export-skoolkit D.
#     `sna2skool.py -H -c D snap.sna` exits 0 with nothing on stderr but "Using control
#     files"; every `@ $A label=N` of D is `@label=N` right before the line at $A; the
#     block letter of every run start matches; `skool2asm.py -H` exits 0 and emits `N:`.
# A2  --machine next --load dsl_demo.nex --map dsl_demo.map --coverage (z88dk labels bound
#     to the page at $8000; the program has 11 `ED 91` NEXTREGs): `sna2skool.py -H -o 0x8000
#     D/8000-P04.bin` picks D/8000-P04.ctl and ONLY it (no -c), warns of nothing, every label
#     lands, and the Z80N NEXTREGs are kept whole as `DEFB $ED,$91,$xx,$yy` (skoolkit is Z80
#     only: unsplit, they would be cut to `DEFB $ED,$91`).
# A3  the Qt (offscreen) and SDL (dummy driver) loop owners write the same directory as the
#     headless one, byte for byte (a frontend whose exit forgot the export wrote nothing).
if want skoolkit-foreign-func; then
    begin_func skoolkit-foreign-func

    sf_dir="$TMP_DIR/skoolkit-foreign"
    rm -rf "$sf_dir"; mkdir -p "$sf_dir"
    sf_faults=()
    sf_root=$(bash "$PROJECT_DIR/test/provision-skoolkit.sh" --print-path) || sf_root=""
    sf_check="$PROJECT_DIR/test/00regression/skoolkit-check.py"
    sf_nex="$PROJECT_DIR/test/00regression/nex/dsl_demo.nex"
    sf_map="$PROJECT_DIR/test/00regression/nex/dsl_demo.map"

    # sf_py <script> <args...>: a skoolkit tool, with an environment of our own (the output
    # is parsed, so LANG=C and nothing inherited).
    sf_py() {
        env -i PATH="$PATH" LANG=C LC_ALL=C timeout --foreground --kill-after=5s 60s python3 "$@"
    }
    sf_jnext() {   # sf_jnext <binary> <qt|plain> <log> <jnext args...>
        local bin=$1 fe=$2 log=$3; shift 3
        if [[ $fe == qt ]]; then
            QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 90s "$bin" --silent \
                "${SD_CARD_ARGS[@]}" "$@" >"$log" 2>&1 || true
        elif [[ $fe == sdl ]]; then
            env -u WAYLAND_DISPLAY SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
                timeout --foreground --kill-after=5s 90s "$bin" --silent \
                "${SD_CARD_ARGS[@]}" "$@" >"$log" 2>&1 || true
        else
            timeout --foreground --kill-after=5s 90s "$bin" --headless \
                "${SD_CARD_ARGS[@]}" "$@" >"$log" 2>&1 || true
        fi
    }

    if [[ -z "$sf_root" || ! -f "$sf_root/sna2skool.py" ]]; then
        fail_row " (skoolkit is not provisioned: run 'make skoolkit' ('make regression' does)); a missing reader is a FAIL, not a SKIP)"
    elif [[ ! -x "$JNEXT_SDL" ]]; then
        fail_row " (SDL-only binary not built: $JNEXT_SDL; run 'make sdl-release')"
    else
        # ---- A1: a 48K snapshot with several control files combined by -c DIR
        printf '%s\n' "rom_start: EQU 0x00000000" "rst38: EQU 0x00000038" "key_scan: EQU 0x0000028E" \
            >"$sf_dir/a1.sym"
        sf_jnext "$JNEXT" plain "$sf_dir/a1.log" --machine 48k --map "$sf_dir/a1.sym" --coverage \
            --delayed-snapshot "$sf_dir/a1.sna" --delayed-snapshot-frames 100 \
            --export-skoolkit "$sf_dir/D1" --delayed-automatic-exit-frames 100
        if ! grep -qE -- "--export-skoolkit .*: [0-9]+ files, 3 labels" "$sf_dir/a1.log"; then
            sf_faults+=("A1: jnext did not log the export")
        elif [[ ! -s "$sf_dir/a1.sna" ]]; then
            sf_faults+=("A1: no snapshot")
        else
            rc=0
            sf_py "$sf_root/sna2skool.py" -H -c "$sf_dir/D1" "$sf_dir/a1.sna" \
                >"$sf_dir/a1.skool" 2>"$sf_dir/a1.err" || rc=$?
            [[ $rc -eq 0 ]] || sf_faults+=("A1: sna2skool exit $rc")
            if grep -qvE '^Using control files: ' "$sf_dir/a1.err"; then
                sf_faults+=("A1: sna2skool said more than 'Using control files': $(head -n 2 "$sf_dir/a1.err" | tr '\n' ' ')")
            fi
            if out=$(sf_py "$sf_check" labels "$sf_dir/a1.skool" "$sf_dir"/D1/*.ctl 2>&1); then :; else
                sf_faults+=("A1: $out")
            fi
            rc=0
            sf_py "$sf_root/skool2asm.py" -H "$sf_dir/a1.skool" >"$sf_dir/a1.asm" 2>"$sf_dir/a1.asmerr" || rc=$?
            [[ $rc -eq 0 ]] || sf_faults+=("A1: skool2asm exit $rc")
            if out=$(sf_py "$sf_check" asm "$sf_dir/a1.asm" "$sf_dir"/D1/*.ctl 2>&1); then :; else
                sf_faults+=("A1: $out")
            fi
        fi

        # ---- A2: a Next program, one control file picked up by default from its .bin
        sf_jnext "$JNEXT" plain "$sf_dir/a2.log" --machine next --load "$sf_nex" --map "$sf_map" --coverage \
            --export-skoolkit "$sf_dir/D2" --delayed-automatic-exit-frames 100
        if [[ ! -f "$sf_dir/D2/8000-P04.ctl" || ! -f "$sf_dir/D2/8000-P04.bin" ]]; then
            sf_faults+=("A2: no 8000-P04.ctl/.bin: $(ls "$sf_dir/D2" 2>&1 | tr '\n' ' ')")
        else
            rc=0
            sf_py "$sf_root/sna2skool.py" -H -o 0x8000 "$sf_dir/D2/8000-P04.bin" \
                >"$sf_dir/a2.skool" 2>"$sf_dir/a2.err" || rc=$?
            [[ $rc -eq 0 ]] || sf_faults+=("A2: sna2skool exit $rc")
            if [[ $(cat "$sf_dir/a2.err") != "Using control file: $sf_dir/D2/8000-P04.ctl" ]]; then
                sf_faults+=("A2: sna2skool did not pick exactly D2/8000-P04.ctl without warnings: $(head -n 3 "$sf_dir/a2.err" | tr '\n' ' ')")
            fi
            if out=$(sf_py "$sf_check" labels "$sf_dir/a2.skool" "$sf_dir/D2/8000-P04.ctl" 2>&1); then :; else
                sf_faults+=("A2: $out")
            fi
            if out=$(sf_py "$sf_check" z80n "$sf_dir/a2.skool" 11 2>&1); then :; else
                sf_faults+=("A2: $out")
            fi
        fi

        # ---- A3: the three loop owners write the same files
        printf '%s\n' "rom_start: EQU 0x00000000" "rst38: EQU 0x00000038" >"$sf_dir/a3.sym"
        for fe in plain qt sdl; do
            bin=$JNEXT; [[ $fe == sdl ]] && bin=$JNEXT_SDL
            sf_jnext "$bin" "$fe" "$sf_dir/a3-$fe.log" --machine 48k --map "$sf_dir/a3.sym" --coverage \
                --export-skoolkit "$sf_dir/D3-$fe" --delayed-automatic-exit-frames 20
            [[ -f "$sf_dir/D3-$fe/0000-CPU.ctl" ]] || sf_faults+=("A3: $fe wrote no export")
        done
        for fe in qt sdl; do
            if ! diff -r "$sf_dir/D3-plain" "$sf_dir/D3-$fe" >"$sf_dir/a3-$fe.diff" 2>&1; then
                sf_faults+=("A3: $fe's export differs from the headless one: $(head -n 2 "$sf_dir/a3-$fe.diff" | tr '\n' ' ')")
            fi
        done
    fi

    if [[ ${#sf_faults[@]} -gt 0 ]]; then
        fail_row " ($(IFS=';'; echo "${sf_faults[*]}"))"
    else
        pass_row " (skoolkit 10.1 reads the exports: a 48K snapshot with -c DIR, a Next page by default pickup with Z80N kept whole, and the Qt and SDL owners write the same files)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
