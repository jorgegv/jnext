#!/usr/bin/env bash
# One functional test of the regression suite. Sourced by regression.sh (the
# driver); also directly executable — the lib then self-initializes and
# standalone_summary prints the totals.
# shellcheck source=test/00regression/test-functions.inc
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/../test-functions.inc"
# shellcheck source=test/00regression/dzrp-functions.inc
source "$(dirname "${BASH_SOURCE[0]}")/../dzrp-functions.inc"

# GH #12 (dzrp-frontend.md §7.2; row 2, §4.1) — CMD_INIT holds the machine (no
# frames while held); CMD_CLOSE releases the client's own pause, so frames run
# again (FRAMES advances); CMD_INIT again on the same connection is served; a
# client that closes and a new one that connects: frames ran in between, and
# the new client is served.
if want dzrp-close-resume-func; then
    dzrp_row dzrp-close-resume-func close-resume
fi

[[ "${BASH_SOURCE[0]}" != "$0" ]] || standalone_summary
