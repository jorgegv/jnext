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

# GH #548 (--debugger with --inject): an injected raw binary lands as its own program and the
# machine stops at the injected PC (here 0x8000, the row's own --inject-pc), the program not having
# run. Same client and held-PC check as debugger-headless-nofile-func. The loop owners' inject
# landing is wired separately from --load's, which only this row exercises end to end.
if want debugger-headless-inject-func; then
    bin="$TMP_DIR/debugger-inject.bin"
    printf '\x00\x00\x18\xfe' >"$bin"        # NOP, NOP, JR $
    dbgstart_dzrp_row debugger-headless-inject-func 0x8000 \
        "$JNEXT" --headless --machine 48k "${SD_CARD_ARGS[@]}" --debugger --dzrp-port 0 \
        --inject "$bin" --inject-org 0x8000 --inject-pc 0x8000
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
