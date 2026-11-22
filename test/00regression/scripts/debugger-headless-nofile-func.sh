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

# GH #548 (--debugger): with no program the machine starts paused at power-on, PC 0,
# before the first instruction. A DZRP client (dzrp-peer.py `startup`) attaches to the
# paused machine, reads PC 0 three times 0.5 s apart (a held machine does not move), then
# CONTINUE runs it. --dzrp-port 0 (an OS-chosen port) so rows can run in parallel; the
# log must carry `debugger: stopped at $0000 (power-on)`.
if want debugger-headless-nofile-func; then
    dbgstart_dzrp_row debugger-headless-nofile-func 0x0000 \
        "$JNEXT" --headless --machine 48k "${SD_CARD_ARGS[@]}" --debugger --dzrp-port 0
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
