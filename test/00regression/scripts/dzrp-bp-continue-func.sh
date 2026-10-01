#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/dzrp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../dzrp-functions.inc"

# GH #12 (dzrp-frontend.md §7.2; §3.1, F7, F8, REQ-dzrp-7) — a breakpoint, a
# CONTINUE, and NTF_PAUSE reason 2 at its address with the bank byte (page 4 at
# slot 4 -> 5); a CONTINUE with a temporary at the SAME address is reported as
# a step (reason 0: the temporary wins); a banked breakpoint fires only with its
# page mapped at the PC's slot — page 4 hits, page 7 never does.
if want dzrp-bp-continue-func; then
    dzrp_row dzrp-bp-continue-func bp-continue
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
