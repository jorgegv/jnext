#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/dzrp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../dzrp-functions.inc"

# GH #12 (dzrp-frontend.md §4.3) — --dzrp-port against the REAL binary. main.cpp's
# parse loop is linked into no unit suite, so only a row proves it:
#
#   1. a PORT that is not a whole number from 0 to 65535 — too big, trailing
#      junk, negative, empty — is a usage error with the documented message and
#      a non-zero exit: a port that silently became another would be a server
#      the user cannot find;
#   2. a port that cannot be bound (another jnext already holds it) is a
#      STARTUP error, named in the log, with a non-zero exit — a server the user
#      asked for and cannot reach is not a warning scrolled past;
#   3. without the flag nothing listens.
if want dzrp-cli-func; then
    begin_func dzrp-cli-func
    fails=()
    cli_run() {
        LANG=C timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine 48k \
            "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 2 "$@" 2>&1
    }
    for bad in 65536 70000 11000x -1 ""; do
        out=$(cli_run --dzrp-port "$bad") && rc=0 || rc=$?
        if [[ $rc -eq 0 ]]; then
            fails+=("--dzrp-port '$bad' was accepted")
        elif ! grep -qF -- "--dzrp-port: PORT must be a number from 0 to 65535, not \"$bad\"" <<<"$out"; then
            fails+=("--dzrp-port '$bad' was refused for the wrong reason")
        fi
    done

    log="$TMP_DIR/dzrp-cli-func.log"
    if dzrp_launch "$log"; then
        out=$(cli_run --dzrp-port "$DZRP_PORT") && rc=0 || rc=$?
        if [[ $rc -eq 0 ]]; then
            fails+=("a second jnext on port $DZRP_PORT started anyway")
        elif ! grep -q "dzrp: cannot listen on 127.0.0.1:$DZRP_PORT" <<<"$out" ||
             ! grep -q -- "--dzrp-port $DZRP_PORT: the DZRP server cannot listen" <<<"$out"; then
            fails+=("a port in use was refused without naming it")
        fi
    else
        fails+=("the first jnext never listened")
    fi
    dzrp_stop

    out=$(cli_run) && rc=0 || rc=$?
    if [[ $rc -ne 0 ]] || grep -q "dzrp:" <<<"$out"; then
        fails+=("without --dzrp-port: exit $rc, or a dzrp line in the log")
    fi

    if (( ${#fails[@]} == 0 )); then
        pass_row " (65536, 70000, 11000x, -1 and '' refused by name; a port in use is a named startup error; no flag, nothing listens)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
