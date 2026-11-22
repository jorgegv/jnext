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

# GH #548 (--debugger): a version 1 .z80 the row writes (PC at header bytes 6-7) starts paused at the PC it restores.
# Same client and held-PC check as debugger-headless-nofile-func.
if want debugger-headless-z80-func; then
    dbgstart_snapshot_row debugger-headless-z80-func z80
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
