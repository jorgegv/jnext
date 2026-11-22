#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/dzrp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../dzrp-functions.inc"
# shellcheck source=test/00regression/debugger-start-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../debugger-start-functions.inc"

# GH #548 (--break-at): runs to an address once instead of the default stop, and implies
# --debugger. On dsl_demo.nex (entry 0x8000) `--break-at _main` (the program's own map
# beside it: `_main = $80C4`, read from that line by the row) and `--break-at 80C4` both
# stop at 0x80C4 with the log saying `--break-at <text>`; `--break-at nosuch` is refused
# when the program is loaded, naming nosuch, exit 1, before the program has run (no
# `debugger: stopped at` line).
if want debugger-break-at-func; then
    begin_func debugger-break-at-func
    if dbgstart_have_python debugger-break-at-func; then
        fails=()
        main_hex=$(sed -n 's/^_main  *= \$\([0-9A-Fa-f]*\) ;.*/\1/p' "$DBGSTART_MAP" | head -1)
        [[ -n "$main_hex" ]] || fails+=("no _main line in $DBGSTART_MAP")
        if (( ${#fails[@]} == 0 )); then
            for spelling in _main "$main_hex"; do
                if ! dbgstart_dzrp_attempt "$TMP_DIR/debugger-break-at-func.log" "0x$main_hex" \
                        "$JNEXT" --headless "${SD_CARD_ARGS[@]}" --break-at "$spelling" --dzrp-port 0 \
                        --load "$DBGSTART_NEX"; then
                    fails+=("--break-at $spelling: $DBGSTART_MSG")
                elif ! grep -aq -- "(--break-at $spelling)" "$TMP_DIR/debugger-break-at-func.log"; then
                    fails+=("--break-at $spelling: the log does not say (--break-at $spelling)")
                fi
            done
            out=$(LANG=C timeout --foreground --kill-after=5s 60s "$JNEXT" --headless \
                "${SD_CARD_ARGS[@]}" --break-at nosuch --dzrp-port 0 --load "$DBGSTART_NEX" 2>&1) && rc=0 || rc=$?
            [[ $rc -eq 1 ]] || fails+=("--break-at nosuch exited $rc, not 1")
            grep -aq -- "--break-at nosuch: not an address, a symbol or a source line" <<<"$out" ||
                fails+=("--break-at nosuch: the refusal does not name nosuch")
            ! grep -aq "debugger: stopped at" <<<"$out" || fails+=("--break-at nosuch: the program stopped anyway")
        fi
        if (( ${#fails[@]} == 0 )); then
            pass_row " (_main and $main_hex stop at 0x$main_hex naming --break-at; nosuch is refused naming it, exit 1, nothing stopped)"
        else
            fail_row " (${fails[*]})"
        fi
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
