#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/dzrp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../dzrp-functions.inc"

# GH #12 (dzrp-frontend.md §7.2; §6, R-1) — CMD_READ_STATE after a pause returns
# a JNXB token and CMD_WRITE_STATE of it restores the registers exactly; after a
# breakpoint (mid-frame) READ_STATE is refused empty; WRITE_STATE of 0 bytes,
# of garbage and of a token this session never issued each answer, notify
# NTF_PAUSE 255 "no state to restore" and change nothing — and CONTINUE still
# runs afterwards (no corruption latch).
if want dzrp-state-func; then
    dzrp_row dzrp-state-func state
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
