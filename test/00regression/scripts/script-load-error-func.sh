#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #26 WP4 (§6.5, §6.6) — what does not load stops jnext at startup with
# exit 1 and the reason, before the machine runs: a parse error (file:line:
# column), a port range straddling 0xFF, an `@symbol` no --map defines, an
# unreadable --script, an unloadable --map; and the usage errors of
# --script-key (a bad value; without --headless).
if want script-load-error-func; then
    begin_func script-load-error-func
    fails=()
    jrun() {
        LANG=C timeout --foreground --kill-after=5s 60s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 50 "$@" 2>&1
    }
    expect_1() {   # NAME PATTERN ARGS...
        local name=$1 pat=$2
        shift 2
        local out rc
        out=$(jrun "$@") && rc=0 || rc=$?
        if [[ $rc -ne 1 ]] || ! grep -qE -- "$pat" <<<"$out"; then
            fails+=("$name: exit $rc")
        elif grep -q "\[jds F:" <<<"$out"; then
            fails+=("$name: something ran")
        fi
    }
    printf 'on frame 3 do\n  log "x" +\nend\n' > "$TMP_DIR/sle-parse.jds"
    printf 'on io_write 0xF0..0x110 do log "x" end\n' > "$TMP_DIR/sle-straddle.jds"
    printf 'on execute @nowhere do log "x" end\n' > "$TMP_DIR/sle-sym.jds"
    printf 'on frame 1 do log "ok" end\n' > "$TMP_DIR/sle-ok.jds"
    expect_1 parse "SCRIPT ERROR .*sle-parse.jds:2:11: " --script "$TMP_DIR/sle-parse.jds"
    expect_1 "parse, second of two" "sle-parse.jds: not loaded" \
        --script "$TMP_DIR/sle-ok.jds" --script "$TMP_DIR/sle-parse.jds"
    expect_1 straddle "sle-straddle.jds:1:13: .*GH #222" --script "$TMP_DIR/sle-straddle.jds"
    expect_1 symbol "SCRIPT ERROR .*sle-sym.jds:1:" --script "$TMP_DIR/sle-sym.jds"
    expect_1 missing "--script $TMP_DIR/nope.jds: cannot be read" --script "$TMP_DIR/nope.jds"
    expect_1 map "--map $TMP_DIR/nope.map: cannot be loaded" \
        --map "$TMP_DIR/nope.map" --script "$TMP_DIR/sle-ok.jds"
    expect_1 "key value" "--script-key: FRAME must be a whole number" \
        --script "$TMP_DIR/sle-ok.jds" --script-key 5 9
    out=$(LANG=C QT_QPA_PLATFORM=offscreen timeout --foreground --kill-after=5s 30s "$JNEXT" \
        --machine 48k "${SD_CARD_ARGS[@]}" --script "$TMP_DIR/sle-ok.jds" --script-key 5 1 \
        --delayed-automatic-exit-frames 5 2>&1) && rc=0 || rc=$?
    if [[ $rc -ne 1 ]] || ! grep -q -- "--script-key requires --headless" <<<"$out"; then
        fails+=("--script-key without --headless: exit $rc")
    fi
    if [[ ${#fails[@]} -eq 0 ]]; then
        pass_row " (parse, straddle, symbol, file, map and --script-key errors: exit 1, positioned, nothing ran)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
