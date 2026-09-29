#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/dzrp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../dzrp-functions.inc"

# GH #12 (dzrp-frontend.md §7.2) — the DZRP framing on a live jnext, through
# jnext's own independent client (tools/cspect_dzrp). CMD_LOOPBACK from 1 to
# 8192 bytes: the command's length counts the payload only, the response's
# counts from the seq byte, and the row reads the length field itself to prove
# both. One byte past the 8192 maximum is declined in-band and the stream stays
# in sync; 300 commands cross the seq wrap 255 -> 1. Served before CMD_INIT:
# LOOPBACK is machine-free.
if want dzrp-loopback-func; then
    dzrp_row dzrp-loopback-func loopback
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
