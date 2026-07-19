#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"

# Bare-filename CLI test: `jnext <file>` must load the file exactly as
# `--load <file>` does, while a mistyped flag must still be an error rather than
# being silently swallowed as a filename.
if want cli-bare-file-func; then
    begin_func cli-bare-file-func
    bare_nex="$SCRIPT_DIR/nex/celeste.nex"
    bare_out=$(timeout --foreground --kill-after=5s 20s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" "$bare_nex" --delayed-automatic-exit 2 2>&1) || true
    # A typo'd flag must NOT be taken as a filename. These runs are EXPECTED to
    # exit non-zero, so capture the status in an if — a bare command would trip
    # the script's `set -e`.
    if timeout --foreground --kill-after=5s 20s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --hedless --delayed-automatic-exit 1 >/dev/null 2>&1
    then typo_rc=0; else typo_rc=1; fi
    # --load plus a bare file is ambiguous and must be rejected.
    if timeout --foreground --kill-after=5s 20s "$JNEXT" --headless \
        "${SD_CARD_ARGS[@]}" --load "$bare_nex" "$bare_nex" --delayed-automatic-exit 1 >/dev/null 2>&1
    then both_rc=0; else both_rc=1; fi
    if ! echo "$bare_out" | grep -q "NEX: loaded"; then
        fail_row " (bare filename did not load the NEX)"
    elif [[ $typo_rc -eq 0 ]]; then
        fail_row " (a mistyped flag was accepted as a filename)"
    elif [[ $both_rc -eq 0 ]]; then
        fail_row " (--load plus a bare file was not rejected)"
    else
        pass_row " (bare filename loads; typo and --load+file rejected)"
    fi
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
