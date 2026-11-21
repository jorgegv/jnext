#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# GH #545 — --debug-channel against the REAL binary. The flag serves a debugger
# protocol to a browser page's channel, so every other build refuses it at once
# (main.cpp's parse loop, cli::page_only_refusal): a server the user asked for
# and cannot reach is a startup failure, not a warning scrolled past.
#
#   1. each of dzrp, zrcp and gdb: exit 1, stderr names the flag and the
#      socket flag to use instead (--dzrp-port);
#   2. an unknown protocol: exit 1 (refused by the page-only rule before the
#      value is looked at, so the reason is the same);
#   3. nothing listens: the log has no "listening on" line.
if want debug-channel-cli-func; then
    begin_func debug-channel-cli-func
    fails=()
    for proto in dzrp zrcp gdb foo; do
        out=$(LANG=C timeout --foreground --kill-after=5s 30s "$JNEXT" --headless --machine 48k \
                "${SD_CARD_ARGS[@]}" --delayed-automatic-exit-frames 2 \
                --debug-channel "$proto" 2>&1) && rc=0 || rc=$?
        if [[ $rc -ne 1 ]]; then
            fails+=("--debug-channel $proto exited $rc, expected 1")
        elif ! grep -qF -- "--debug-channel:" <<<"$out" ||
             ! grep -qF -- "--dzrp-port" <<<"$out"; then
            fails+=("--debug-channel $proto was refused without naming the flag and --dzrp-port")
        elif grep -q "listening on" <<<"$out"; then
            fails+=("--debug-channel $proto refused but something listened")
        fi
    done
    if (( ${#fails[@]} == 0 )); then
        pass_row " (dzrp, zrcp, gdb and an unknown protocol: exit 1, the reason names --debug-channel and --dzrp-port)"
    else
        fail_row " (${fails[*]})"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
