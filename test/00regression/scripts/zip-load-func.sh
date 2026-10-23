#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #392 - a program inside a .zip archive loads from the command line.
# Oracle: the issue (an archive loads the one program inside it, byte-for-byte
# the same as the bare file), the tap-demo screenshot row's program, and the
# refusals the user guide promises (several programs, no program, not a ZIP).
# The archives are foreign-writer fixtures (test/fixtures/zip/make-fixtures.sh).
if want zip-load-func; then
    begin_func zip-load-func
    zfix="$PROJECT_DIR/test/fixtures/zip"
    zproblem=""
    zrun() {   # zrun <expected-status: ok|fail> <outfile> <jnext args...>
        local want_rc=$1 out=$2 rc=0
        shift 2
        timeout --foreground --kill-after=5s 60s "$JNEXT" --headless \
            "${SD_CARD_ARGS[@]}" "$@" >"$out" 2>&1 || rc=$?
        if [[ $want_rc == ok && $rc -ne 0 ]]; then
            zproblem="exit $rc, wanted 0: $*"; return 1
        fi
        if [[ $want_rc == fail && $rc -eq 0 ]]; then
            zproblem="exit 0, wanted non-zero: $*"; return 1
        fi
        return 0
    }

    # 1. the archive and the bare file show the same screen.
    shot_args=(--delayed-screenshot-frames 300 --delayed-automatic-exit-frames 310)
    if zrun ok "$TMP_DIR/zip.log" --machine 48k --load "$zfix/beeper.tap.zip" \
            --delayed-screenshot "$TMP_DIR/zip.png" "${shot_args[@]}" &&
       zrun ok "$TMP_DIR/bare.log" --machine 48k --load "$SCRIPT_DIR/tap/beeper_demo.tap" \
            --delayed-screenshot "$TMP_DIR/bare.png" "${shot_args[@]}"; then
        if [[ ! -s "$TMP_DIR/zip.png" ]]; then
            zproblem="the archive run wrote no screenshot"
        elif ! cmp -s "$TMP_DIR/zip.png" "$TMP_DIR/bare.png"; then
            zproblem="beeper.tap.zip and beeper_demo.tap show different screens"
        fi
    fi

    # 2. a bare positional archive, NEX inside (data-descriptor member).
    if [[ -z $zproblem ]] &&
       zrun ok "$TMP_DIR/nex.log" --machine next "$zfix/lores.nex.zip" --delayed-automatic-exit 2; then
        grep -q "NEX: loaded" "$TMP_DIR/nex.log" || zproblem="lores.nex.zip did not load the NEX"
    fi

    # 3. several programs: refused, both named.
    if [[ -z $zproblem ]] &&
       zrun fail "$TMP_DIR/two.log" --machine 48k --load "$zfix/twotapes.zip" --delayed-automatic-exit 2; then
        grep -q "SIDEA.TAP" "$TMP_DIR/two.log" && grep -q "SIDEB.TAP" "$TMP_DIR/two.log" ||
            zproblem="twotapes.zip refusal does not name both programs"
    fi

    # 4. no program.
    if [[ -z $zproblem ]] &&
       zrun fail "$TMP_DIR/nop.log" --machine 48k --load "$zfix/noprog.zip" --delayed-automatic-exit 2; then
        grep -q "no program" "$TMP_DIR/nop.log" || zproblem="noprog.zip refusal does not say 'no program'"
    fi

    # 5. text named .zip.
    if [[ -z $zproblem ]]; then
        echo "this is not a zip file" >"$TMP_DIR/notzip.zip"
        if zrun fail "$TMP_DIR/bad.log" --machine 48k --load "$TMP_DIR/notzip.zip" --delayed-automatic-exit 2; then
            grep -q "not a ZIP archive" "$TMP_DIR/bad.log" || zproblem="a text .zip was not refused as 'not a ZIP archive'"
        fi
    fi

    # 6. every extracted file is gone once the runs have exited.
    if [[ -z $zproblem && -d "$JNEXT_CONFIG_DIR/archive-load" ]]; then
        left=$(find "$JNEXT_CONFIG_DIR/archive-load" -type f | head -3)
        [[ -z $left ]] || zproblem="extracted files left behind: $left"
    fi

    if [[ -n $zproblem ]]; then
        fail_row " ($zproblem)"
        for f in zip bare nex two nop bad; do
            [[ -f "$TMP_DIR/$f.log" ]] && show_output "jnext $f" "$(tail -5 "$TMP_DIR/$f.log")"
        done
    else
        pass_row " (archives load like the bare file; several/none/not-a-zip refused; scratch cleaned)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
